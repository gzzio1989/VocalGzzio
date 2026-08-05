// bench_plugin.cpp — 本物の VocalGzzioProcessor を丸ごと動かす負荷・遅延ベンチ (v2.8.1)
//
//  部品ごとのDSPテストと違い、ここは**製品そのもの**(processBlock 全チェーン)を測る。
//
//  ● 遅延: インパルスを流して出力のピーク位置を見る。
//      - 出荷状態〜ゼロ遅延系の機能ON → ピークは入れた場所のまま(=遅延0サンプル)
//      - ピッチ補正/ボイス変換ON → 申告した latencySamples とピークのずれが一致するか
//  ● 負荷: グッジオさんの環境(44.1kHz / 124サンプル = 締切2.81ms)で、
//      1ブロックの処理時間の「平均」と「最悪」を測る。最悪が締切に近いほど
//      プチノイズの危険。メモリ確保ゼロ化(v2.8.0)の効果は最悪値に出る。
//
//  ビルド: tools/run_bench.sh 参照(ビルド済みの libVocalGzzio_SharedCode.a に直接リンク)
#include "../Source/PluginProcessor.h"
#include <chrono>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cmath>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

// パラメータを実数値で流し込む(正規化はレンジから)
static void setP (VocalGzzioProcessor& p, const char* id, float value)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (value));
}

// 歌声もどき(倍音+ビブラート)。負荷測定はゲートが開いた状態で測らないと意味がない。
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

// 1シナリオを測る: 事前に params を流し込み → prepareToPlay → 20秒ぶん回す
struct Result { double avgPct, worstPct, p99Pct; int latencyRep, latencyMeas; };

static Result runScenario (VocalGzzioProcessor& proc, double sr, int block, double seconds)
{
    proc.prepareToPlay (sr, block);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    Voice v; v.sr = sr;

    const double deadlineUs = 1.0e6 * block / sr;
    const int totalBlocks = (int) (seconds * sr / block);
    const int warmup = (int) (2.0 * sr / block);          // 2秒は準備運動(計測から除外)
    std::vector<double> us; us.reserve ((size_t) totalBlocks);

    for (int b = 0; b < totalBlocks; ++b)
    {
        float* L = buf.getWritePointer (0);
        float* R = buf.getWritePointer (1);
        for (int n = 0; n < block; ++n) { const float s = v.next (196.0); L[n] = s; R[n] = s; }
        const auto t0 = std::chrono::steady_clock::now();
        proc.processBlock (buf, midi);
        const auto t1 = std::chrono::steady_clock::now();
        if (b >= warmup)
            us.push_back (std::chrono::duration<double, std::micro> (t1 - t0).count());
    }
    std::sort (us.begin(), us.end());
    double sum = 0.0; for (double x : us) sum += x;
    Result r {};
    r.avgPct   = 100.0 * (sum / us.size()) / deadlineUs;
    r.worstPct = 100.0 * us.back() / deadlineUs;
    r.p99Pct   = 100.0 * us[(size_t) (us.size() * 0.99)] / deadlineUs;

    // ---- 遅延: 静けさ→1発のインパルス→ピーク位置 ----
    r.latencyRep = proc.getLatencySamples();
    {
        // 状態を落ち着かせる(コンプ等の包絡が動かないように小さく)
        for (int b = 0; b < 40; ++b) { buf.clear(); proc.processBlock (buf, midi); }
        const int kImp = 5;                    // ブロック内のこの位置に入れる
        std::vector<float> out; out.reserve (4096);
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
        r.latencyMeas = arg - kImp;
    }
    return r;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double sr = 44100.0; const int block = 124;     // 報告者の実環境
    std::printf ("全チェーン負荷・遅延ベンチ @ %.0f Hz / %d サンプル (締切 %.2f ms)\n\n",
                 sr, block, 1000.0 * block / sr);

    struct Scen { const char* name; void (*setup) (VocalGzzioProcessor&); int expectLat; int tol; };
    const Scen scens[] = {
        { "出荷状態(初期値のまま)", [] (VocalGzzioProcessor& p) { juce::ignoreUnused (p); }, 0, 0 },
        { "うた自動そうとう(ジー音100/ことば35/圧縮/リバーブ)", [] (VocalGzzioProcessor& p)
          { setP (p, "hum_amt", 100); setP (p, "cons_amt", 35); setP (p, "comp2", 30);
            setP (p, "deess", 35); setP (p, "revon", 1); setP (p, "revmix", 12);
            setP (p, "res_amt", 40); setP (p, "ride_amt", 35); }, 0, 0 },
        { "ピッチ補正80+こぶし70", [] (VocalGzzioProcessor& p)
          { setP (p, "at_on", 1); setP (p, "at_amount", 80); setP (p, "orn_amt", 70); }, 768, 0 },
        { "全部盛り(補正+5人ユニゾン+ボイス変換+空間+スマートEQ)", [] (VocalGzzioProcessor& p)
          { setP (p, "hum_amt", 100); setP (p, "cons_amt", 50); setP (p, "res_amt", 60);
            setP (p, "ride_amt", 50); setP (p, "at_on", 1); setP (p, "at_amount", 80);
            setP (p, "orn_amt", 70); setP (p, "jn_on", 1); setP (p, "jn_mix", 55);
            setP (p, "vc_on", 1); setP (p, "vc_pitch", 3); setP (p, "seq_on", 1);
            setP (p, "revon", 1); setP (p, "revmix", 15); setP (p, "rev_type", 6);
            setP (p, "dly_on", 1); setP (p, "delay", 20); setP (p, "cho_on", 1);
            setP (p, "cho_amt", 25); setP (p, "width", 20); setP (p, "doubler", 20);
            setP (p, "br_amt", 30); setP (p, "ring_amt", 30); }, 768, 256 },
        // ↑全部盛りは許容±256(=合成の1ホップ)。ボイス変換で+3半音ずらしたインパルスは
        //   もう一点の音ではなく粒の集まりになるので、ピークはホップの中のどこかに散る。
        //   「申告=768」が正しいことは、ひとつ上のピッチ補正シナリオ(ずれ0)で確認済み。
    };

    for (const auto& s : scens)
    {
        VocalGzzioProcessor proc;
        s.setup (proc);
        const Result r = runScenario (proc, sr, block, 20.0);
        std::printf ("%s\n", s.name);
        std::printf ("  負荷: 平均 %5.1f %% / 99%%点 %5.1f %% / 最悪 %5.1f %% (締切=100%%)\n",
                     r.avgPct, r.p99Pct, r.worstPct);
        std::printf ("  遅延: 申告 %d サンプル / 実測 %d サンプル\n", r.latencyRep, r.latencyMeas);
        CHECK (r.worstPct < 60.0, "最悪ブロックが締切の6割未満 (%.1f%%)", r.worstPct);
        CHECK (std::abs (r.latencyMeas - s.expectLat) <= s.tol,
               "実測遅延が想定どおり (%d, 想定%d±%d)", r.latencyMeas, s.expectLat, s.tol);
        CHECK (s.expectLat == 0 || std::abs (r.latencyRep - r.latencyMeas) <= s.tol,
               "申告と実測が一致 (申告%d/実測%d, 許容±%d)", r.latencyRep, r.latencyMeas, s.tol);
        std::printf ("\n");
    }

    // 低遅延モードの申告一致も1本
    {
        VocalGzzioProcessor proc;
        setP (proc, "at_on", 1); setP (proc, "at_amount", 80); setP (proc, "lowlat", 1);
        const Result r = runScenario (proc, sr, block, 6.0);
        std::printf ("低遅延モード+ピッチ補正\n  遅延: 申告 %d / 実測 %d\n", r.latencyRep, r.latencyMeas);
        CHECK (r.latencyRep == 384 && r.latencyMeas == 384, "低遅延=384サンプル (申告%d/実測%d)",
               r.latencyRep, r.latencyMeas);
        std::printf ("\n");
    }

    std::printf (gFail ? "== %d 件 FAIL ==\n" : "== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
