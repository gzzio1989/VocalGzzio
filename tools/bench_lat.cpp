// bench_lat.cpp — セッション用途の「遅延台帳」実測 (2026-08-05)
//
//  背景: SYNCROOMの快適ライン(合計~30ms)は「ネット+PC込みの実測値」で、
//  自分側に残る枠はその半分程度。UR12のAD/DA+ASIOバッファで既に7〜8ms消えるので、
//  プラグインに許される追加遅延は実質ゼロ。
//  → 「いらない機能を省いた構成」が本当に0サンプルか、遅延を足すのはどの機能で
//    何サンプルかを、機能ごとに1本ずつ測って台帳にする。
//
//  方法: bench_plugin.cpp と同じ。無音40ブロックで落ち着かせ→インパルス→ピーク位置。
//  ピッチシフト系はインパルスが粒に散るので、申告との一致は±1ホップ(256)で判定。
#include "PluginProcessor.h"
#include <chrono>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cmath>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static void setP (VocalGzzioProcessor& p, const char* id, float value)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (value));
}

struct Voice
{
    double ph[12] {}, vp = 0.0, sr = 44100.0;
    float next (double f0)
    {
        const double vib = 1.0 + 0.008 * std::sin (vp);
        vp += 2.0 * juce::MathConstants<double>::pi * 5.5 / sr;
        double s = 0.0;
        for (int h = 0; h < 12; ++h)
        {
            const double fh = f0 * vib * (h + 1); if (fh > sr * 0.45) break;
            s += std::sin (ph[h]) / (h + 1);
            ph[h] += 2.0 * juce::MathConstants<double>::pi * fh / sr;
            if (ph[h] > 2 * juce::MathConstants<double>::pi) ph[h] -= 2 * juce::MathConstants<double>::pi;
        }
        return (float) (0.20 * s / 1.7);
    }
};

// 遅延: 申告と実測(ピーク位置)。歌声を2秒流してから静音→インパルス。
static void measure (VocalGzzioProcessor& proc, double sr, int block, int* rep, int* meas)
{
    proc.prepareToPlay (sr, block);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    Voice v; v.sr = sr;
    for (int b = 0; b < (int) (2.0 * sr / block); ++b)      // 検出器を目覚めさせる
    {
        float* L = buf.getWritePointer (0); float* R = buf.getWritePointer (1);
        for (int n = 0; n < block; ++n) { const float s = v.next (196.0); L[n] = s; R[n] = s; }
        proc.processBlock (buf, midi);
    }
    for (int b = 0; b < 40; ++b) { buf.clear(); proc.processBlock (buf, midi); }
    const int kImp = 5;
    std::vector<float> out;
    for (int b = 0; b < 24; ++b)
    {
        buf.clear();
        if (b == 0) { buf.setSample (0, kImp, 1.0f); buf.setSample (1, kImp, 1.0f); }
        proc.processBlock (buf, midi);
        const float* L = buf.getReadPointer (0);
        for (int n = 0; n < block; ++n) out.push_back (L[n]);
    }
    int arg = 0; float best = 0.0f;
    for (int i = 0; i < (int) out.size(); ++i)
        if (std::fabs (out[(size_t) i]) > best) { best = std::fabs (out[(size_t) i]); arg = i; }
    *rep  = proc.getLatencySamples();
    *meas = arg - kImp;
}

// セッション構成の負荷も1本だけ(12秒)
static void loadStat (VocalGzzioProcessor& proc, double sr, int block, double* avg, double* p99, double* worst)
{
    proc.prepareToPlay (sr, block);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    Voice v; v.sr = sr;
    const double deadlineUs = 1.0e6 * block / sr;
    const int total = (int) (12.0 * sr / block), warm = (int) (2.0 * sr / block);
    std::vector<double> us; us.reserve ((size_t) total);
    for (int b = 0; b < total; ++b)
    {
        float* L = buf.getWritePointer (0); float* R = buf.getWritePointer (1);
        for (int n = 0; n < block; ++n) { const float s = v.next (196.0); L[n] = s; R[n] = s; }
        const auto t0 = std::chrono::steady_clock::now();
        proc.processBlock (buf, midi);
        const auto t1 = std::chrono::steady_clock::now();
        if (b >= warm) us.push_back (std::chrono::duration<double, std::micro> (t1 - t0).count());
    }
    std::sort (us.begin(), us.end());
    double sum = 0.0; for (double x : us) sum += x;
    *avg = 100.0 * (sum / us.size()) / deadlineUs;
    *p99 = 100.0 * us[(size_t) (us.size() * 0.99)] / deadlineUs;
    *worst = 100.0 * us.back() / deadlineUs;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double sr = 44100.0; const int block = 124;
    std::printf ("セッション遅延台帳 @ 44.1kHz / 124サンプル (1smp=%.3fms)\n\n", 1000.0 / sr);

    struct Scen { const char* name; void (*setup) (VocalGzzioProcessor&); int expect; bool exact; };
    const Scen scens[] = {
        { "出荷状態", [] (VocalGzzioProcessor& p) { juce::ignoreUnused (p); }, 0, true },
        { "セッション構成(ジー音/ことば/圧縮/艶/自動音量/軽リバーブ)", [] (VocalGzzioProcessor& p)
          { setP (p, "hum_amt", 100); setP (p, "cons_amt", 35); setP (p, "comp2", 30);
            setP (p, "deess", 35); setP (p, "res_amt", 40); setP (p, "ride_amt", 35);
            setP (p, "revon", 1); setP (p, "revmix", 12); }, 0, true },
        { "セッション構成+やまびこ/コーラス", [] (VocalGzzioProcessor& p)
          { setP (p, "hum_amt", 100); setP (p, "cons_amt", 35); setP (p, "comp2", 30);
            setP (p, "deess", 35); setP (p, "res_amt", 40); setP (p, "ride_amt", 35);
            setP (p, "revon", 1); setP (p, "revmix", 12);
            setP (p, "dly_on", 1); setP (p, "delay", 20); setP (p, "cho_on", 1); setP (p, "cho_amt", 25); }, 0, true },
        { "空間ぜんぶ(やまびこ/コーラス/ひろがり/かさね/シマー/リング)", [] (VocalGzzioProcessor& p)
          { setP (p, "revon", 1); setP (p, "revmix", 15); setP (p, "dly_on", 1); setP (p, "delay", 20);
            setP (p, "cho_on", 1); setP (p, "cho_amt", 25); setP (p, "width", 20);
            setP (p, "doubler", 20); setP (p, "br_amt", 30); setP (p, "ring_amt", 30); }, 0, true },
        { "ハモリ/5人ユニゾンのみ", [] (VocalGzzioProcessor& p)
          { setP (p, "jn_on", 1); setP (p, "jn_mix", 55); }, 0, true },
        { "ハモリ+ピッチ補正(主メロも遅れる組み合わせ)", [] (VocalGzzioProcessor& p)
          { setP (p, "jn_on", 1); setP (p, "jn_mix", 55);
            setP (p, "at_on", 1); setP (p, "at_amount", 80); }, 768, false },
        { "★セッションON + ピッチ補正80", [] (VocalGzzioProcessor& p)
          { setP (p, "session", 1); setP (p, "at_on", 1); setP (p, "at_amount", 80); }, 0, true },
        { "★セッションON + 全部盛り(補正/変換/ハモリ/空間ぜんぶ)", [] (VocalGzzioProcessor& p)
          { setP (p, "session", 1);
            setP (p, "hum_amt", 100); setP (p, "cons_amt", 50); setP (p, "res_amt", 60);
            setP (p, "ride_amt", 50); setP (p, "at_on", 1); setP (p, "at_amount", 80);
            setP (p, "orn_amt", 70); setP (p, "jn_on", 1); setP (p, "jn_mix", 55);
            setP (p, "vc_on", 1); setP (p, "vc_pitch", 3); setP (p, "seq_on", 1);
            setP (p, "revon", 1); setP (p, "revmix", 15); setP (p, "rev_type", 6);
            setP (p, "dly_on", 1); setP (p, "delay", 20); setP (p, "cho_on", 1);
            setP (p, "cho_amt", 25); setP (p, "width", 20); setP (p, "doubler", 20);
            setP (p, "br_amt", 30); setP (p, "ring_amt", 30); }, 0, true },
        { "こぶしのみ(補正OFF)", [] (VocalGzzioProcessor& p)
          { setP (p, "orn_amt", 70); }, 0, true },
        { "ピッチ補正80", [] (VocalGzzioProcessor& p)
          { setP (p, "at_on", 1); setP (p, "at_amount", 80); }, 768, false },
        // v2.9.0: 「低遅延モード」(768→384)は廃止したのでシナリオも削除。
        // 半分にしてもセッションでは足りず、低い声がざらつく代償だけが残るため。
        { "ボイス変換のみ(+3半音)", [] (VocalGzzioProcessor& p)
          { setP (p, "vc_on", 1); setP (p, "vc_pitch", 3); }, -1, false },   // 申告値と一致するかだけ見る
    };

    for (const auto& s : scens)
    {
        VocalGzzioProcessor proc;
        // ★単体起動版は前回の設定を自動保存から読み直す(= セッションONのまま
        //   終了すると次も入っている。製品としては正しい)。ベンチはそれに
        //   引きずられると全シナリオが0サンプルになってしまうので、
        //   毎回ここで明示的に落としてから setup を流す。
        setP (proc, "session", 0);
        s.setup (proc);
        int rep = -1, meas = -1;
        measure (proc, sr, block, &rep, &meas);
        // v2.9.0: 画面の「追加遅延」バッジが読む値。processBlock を回したあとに
        // 実測と一致していないと、バッジが嘘をつくことになる。
        const int badge = proc.addedLatencySamples();
        std::printf ("%s\n  申告 %d smp (%.1f ms) / 実測 %d smp (%.1f ms) / バッジ %d smp\n",
                     s.name, rep, rep * 1000.0 / sr, meas, meas * 1000.0 / sr, badge);
        CHECK (std::abs (badge - meas) <= 256,
               "バッジが実測と一致 (バッジ%d/実測%d)", badge, meas);
        if (s.exact)
            CHECK (rep == s.expect && meas == s.expect,
                   "遅延ゼロ (申告%d/実測%d)", rep, meas);
        else if (s.expect >= 0)
            CHECK (rep == s.expect && std::abs (meas - s.expect) <= 256,
                   "申告%dと実測%dが±256内で一致", rep, meas);
        else
            CHECK (std::abs (meas - rep) <= 256,
                   "申告%dと実測%dが±256内で一致(シフト系は粒に散る)", rep, meas);
        std::printf ("\n");
    }

    // セッション構成の負荷(参考)
    {
        VocalGzzioProcessor proc;
        setP (proc, "session", 0);
        setP (proc, "hum_amt", 100); setP (proc, "cons_amt", 35); setP (proc, "comp2", 30);
        setP (proc, "deess", 35); setP (proc, "res_amt", 40); setP (proc, "ride_amt", 35);
        setP (proc, "revon", 1); setP (proc, "revmix", 12);
        double avg = 0, p99 = 0, worst = 0;
        loadStat (proc, sr, block, &avg, &p99, &worst);
        std::printf ("セッション構成の負荷: 平均 %.1f %% / 99%%点 %.1f %% / 最悪 %.1f %% (締切2.81ms=100%%)\n\n",
                     avg, p99, worst);
        CHECK (p99 < 30.0, "99%%点が締切の3割未満 (%.1f%%)", p99);
    }

    std::printf (gFail ? "== %d 件 FAIL ==\n" : "== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
