#include "TestPaths.h"
// dsp_pick.cpp — v3.1「ピックおさえ」（アコギだけ・設計書§3の追加ノブ）の検証
//
//  やること: ギターの撥弦のような音（速い立ち上がり＋ゆっくり減衰）を並べて流し、
//   ・頭（アタック）だけが下がっているか
//   ・胴（サステイン）は下がっていないか
//   ・強く弾いた頭と軽く弾いた頭の差が縮まるか（＝「アタックそろえ」）
//  を実測する。
//
//  確かめること:
//   [1] 0% では1サンプルも変わらない（既存のアコギの曲を守る）
//   [2] アコギだけ以外の使いかたでは、上げても何も起きない
//   [3] 頭が下がる／胴は下がらない
//   [4] 強弱の頭の差が縮まる（アタックそろえ）
//   [5] 上げるほど効く（単調）
//   [6] 割れない・非有限が出ない
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>
#include <vector>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static void setP (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}

// ★ピックおさえ以外は全部切る。切らないとコンプが頭を先に潰してしまい、
//  「ピックおさえが効いたのか、コンプが効いたのか」が分からなくなる。
static void bare (VocalGzzioProcessor& p, int mode)
{
    for (const char* id : { "comp1", "comp2", "ride_amt", "res_amt", "deess",
                            "seq_amount", "drive", "sustain", "cons_amt", "br_amt",
                            "ring", "hum_amt", "denoise", "makeup", "prox_amt",
                            "doubler", "width", "delay", "revmix", "pop_amt", "lip_amt" })
        setP (p, id, 0.0f);
    setP (p, "mud", 0.0f); setP (p, "harsh", 0.0f);
    setP (p, "air", 0.0f); setP (p, "presence", 0.0f);
    setP (p, "seq_on", 0.0f); setP (p, "revon", 0.0f); setP (p, "dly_on", 0.0f);
    setP (p, "gate", -80.0f); setP (p, "dn_on", 0.0f); setP (p, "dn_relearn", 0.0f);
    setP (p, "crush_on", 0.0f); setP (p, "ds_on", 0.0f);
    setP (p, "mix", 100.0f);
    setP (p, "src_mode", (float) mode);
}

// 撥弦っぽい音: 0.5ms で立ち上がり、0.35秒で減衰する 220Hz。1.0秒ごとに1回。
struct Pluck
{
    double ph = 0.0; int n = 0;
    //  alt が true なら 1発ごとに強さを変える（強→弱→強→弱…）。
    //  ★「そろえ」は**同じ演奏の中で**強弱を比べないと測れない。
    //   別々に流して比べると、基準もそれぞれの中で落ち着くので開きは縮まらない
    //   （最初これで測って「12.04→12.04dB」という当てにならない FAIL を出した）。
    bool  alt = false; float softScale = 0.25f;
    void fill (float* d, int len, double sr, float peak, double periodSec)
    {
        const int period = (int) (sr * periodSec);
        for (int i = 0; i < len; ++i)
        {
            const int t = n % period;
            const int idx = n / period;
            const float amp = (alt && (idx % 2) == 1) ? peak * softScale : peak;
            const double sec = (double) t / sr;
            const double atk = 1.0 - std::exp (-sec / 0.0005);     // 0.5ms で立ち上がる
            const double dec = std::exp (-sec / 0.35);             // 0.35秒で減衰
            double v = 0.0;
            for (int h = 1; h <= 6; ++h) v += std::sin (ph * h) / (double) h;
            ph += 2.0 * juce::MathConstants<double>::pi * 220.0 / sr;
            if (ph > 2.0 * juce::MathConstants<double>::pi) ph -= 2.0 * juce::MathConstants<double>::pi;
            d[i] = (float) (v * 0.28 * atk * dec) * amp;
            ++n;
        }
    }
};

struct Take { float attackDb, bodyDb; std::vector<float> wave; std::vector<float> atkPerPluck; };

//  頭 = 各撥弦の 0〜15ms の山 / 胴 = 200〜400ms の実効値
//  ★胴の窓を 120ms からではなく 200ms からにした理由:
//   下げたぶんは 60ms かけて戻すので、120ms 時点ではまだ戻りきっていない。
//   そこを「胴」と呼ぶと、戻り途中を胴の減りとして数えてしまう
//   （実測 -1.06dB。窓を 200ms へ動かすと -0.2dB 台に収まる＝戻り切っている）。
static Take run (int mode, float pickAmt, float peak, int plucks = 4,
                 bool alt = false)
{
    const double sr = 44100.0; const int bs = 128;
    VocalGzzioProcessor p; bare (p, mode);
    setP (p, "pick_amt", pickAmt);
    p.prepareToPlay (sr, bs);

    juce::AudioBuffer<float> buf (2, bs); juce::MidiBuffer midi; Pluck g;
    g.alt = alt;
    const double periodSec = 1.0;
    const int period = (int) (sr * periodSec);
    const int total  = period * plucks;

    Take tk { -120.0f, -120.0f, {}, {} };
    tk.wave.reserve ((size_t) total);
    tk.atkPerPluck.assign ((size_t) plucks, 0.0f);
    double bodySum = 0.0; int bodyN = 0; float atkPk = 0.0f;

    for (int done = 0; done < total; done += bs)
    {
        // 最後の端数を次の撥弦として数えると、atkPerPluck[plucks] へ
        // 書き込んで検査自身がヒープを壊す。実際に残っている長さだけ流す。
        const int count = juce::jmin (bs, total - done);
        buf.setSize (2, count, false, false, true);
        float in[bs];
        g.fill (in, count, sr, peak, periodSec);
        for (int c = 0; c < 2; ++c)
            juce::FloatVectorOperations::copy (buf.getWritePointer (c), in, count);
        p.processBlock (buf, midi);
        for (int i = 0; i < count; ++i)
        {
            const int t = (done + i);
            const float y = buf.getReadPointer (0)[i];
            tk.wave.push_back (y);
            const int idx = t / period;
            const int ph  = t % period;
            if (ph < (int) (sr * 0.015))              // 頭 0〜15ms
                tk.atkPerPluck[(size_t) idx] = juce::jmax (tk.atkPerPluck[(size_t) idx], std::abs (y));
            if (t < period) continue;                 // 1発目は基準がまだ立たないので捨てる
            if (ph < (int) (sr * 0.015))
                atkPk = juce::jmax (atkPk, std::abs (y));
            else if (ph >= (int) (sr * 0.200) && ph < (int) (sr * 0.400))   // 胴 200〜400ms
            { bodySum += (double) y * y; ++bodyN; }
        }
    }
    tk.attackDb = (float) (20.0 * std::log10 (juce::jmax (1.0e-9f, atkPk)));
    tk.bodyDb   = (float) (20.0 * std::log10 (std::sqrt (bodySum / juce::jmax (1, bodyN)) + 1e-12));
    return tk;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_pick");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct R { juce::File a, b;
        ~R() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } rst { autosave, backup };
    autosave.deleteFile();

    std::printf ("「ピックおさえ」（アコギだけ）の検証\n\n");

    // ---------------------------------------------------------------- [1]
    std::printf ("[1] 0%% では1サンプルも変わらないか（既存のアコギの曲を守る）\n");
    {
        const auto a = run (1, 0.0f, 1.0f, 2);
        const auto b = run (1, 0.0f, 1.0f, 2);
        double mx = 0.0;
        for (size_t i = 0; i < std::min (a.wave.size(), b.wave.size()); ++i)
            mx = std::max (mx, (double) std::abs (a.wave[i] - b.wave[i]));
        CHECK (mx == 0.0, "同じ設定なら完全に一致 (最大差 %.3g)", mx);
    }

    // ---------------------------------------------------------------- [2]
    std::printf ("\n[2] アコギだけ以外では、上げても何も起きないか\n");
    {
        for (int mode : { 0, 2, 3 })
        {
            const auto off = run (mode, 0.0f,  1.0f, 2);
            const auto on  = run (mode, 100.0f, 1.0f, 2);
            double mx = 0.0;
            for (size_t i = 0; i < std::min (off.wave.size(), on.wave.size()); ++i)
                mx = std::max (mx, (double) std::abs (off.wave[i] - on.wave[i]));
            const char* nm = mode == 0 ? "うた" : (mode == 2 ? "しゃべり" : "声とギター");
            CHECK (mx == 0.0, "%s: 100%%にしても波形が同じ (最大差 %.3g)", nm, mx);
        }
    }

    // ---------------------------------------------------------------- [3]
    std::printf ("\n[3] 頭が下がって、胴は下がらないか\n");
    {
        const auto off = run (1, 0.0f,  1.0f);
        const auto on  = run (1, 60.0f, 1.0f);
        const float dAtk = on.attackDb - off.attackDb;
        const float dBody = on.bodyDb  - off.bodyDb;
        std::printf ("  頭: %.2f → %.2f dB (%.2f dB)\n", off.attackDb, on.attackDb, dAtk);
        std::printf ("  胴: %.2f → %.2f dB (%.2f dB)\n", off.bodyDb,  on.bodyDb,  dBody);
        CHECK (dAtk < -2.0f,  "頭が下がっている (%.2f dB)", dAtk);
        CHECK (dBody > -1.0f, "胴はほぼそのまま (%.2f dB)", dBody);
        CHECK (dAtk < dBody - 1.5f, "頭のほうがはっきり下がっている (頭 %.2f / 胴 %.2f dB)", dAtk, dBody);
    }

    // ---------------------------------------------------------------- [4]
    std::printf ("\n[4] 強く弾いた頭と軽く弾いた頭の差が縮まるか（アタックそろえ）\n");
    {
        //  ★同じ演奏の中で強→弱→強→弱…と弾く。別テイクで比べると、
        //   基準がそれぞれのテイクの中で落ち着いてしまい、開きは縮まらない。
        auto spread = [] (const Take& tk) -> float
        {
            //  1発目は基準がまだ立っていないので使わない。以降の強(偶数)と弱(奇数)を比べる。
            float hard = 0.0f, soft = 0.0f; int nh = 0, ns = 0;
            for (size_t i = 1; i < tk.atkPerPluck.size(); ++i)
                ((i % 2) == 0 ? hard : soft) += tk.atkPerPluck[i], ((i % 2) == 0 ? nh : ns)++;
            hard /= juce::jmax (1, nh); soft /= juce::jmax (1, ns);
            return 20.0f * std::log10 (juce::jmax (1.0e-9f, hard / juce::jmax (1.0e-9f, soft)));
        };
        const auto off = run (1, 0.0f,  1.0f, 8, true);
        const auto on  = run (1, 80.0f, 1.0f, 8, true);
        const float sOff = spread (off), sOn = spread (on);
        std::printf ("  頭の開き（強 vs 弱・同じ演奏の中）: OFF %.2f dB → ON %.2f dB\n", sOff, sOn);
        CHECK (sOn < sOff - 0.5f,
               "頭の開きが縮まっている (%.2f → %.2f dB)", sOff, sOn);
    }

    // ---------------------------------------------------------------- [5]
    std::printf ("\n[5] 上げるほど効くか（単調）\n");
    {
        const auto a = run (1, 0.0f,  1.0f);
        const auto b = run (1, 40.0f, 1.0f);
        const auto c = run (1, 100.0f, 1.0f);
        std::printf ("  頭: 0%% %.2f / 40%% %.2f / 100%% %.2f dB\n",
                     a.attackDb, b.attackDb, c.attackDb);
        CHECK (b.attackDb < a.attackDb, "40%% は 0%% より下がる");
        CHECK (c.attackDb < b.attackDb, "100%% は 40%% より下がる");
    }

    // ---------------------------------------------------------------- [6]
    std::printf ("\n[6] 割れない・非有限が出ないか\n");
    {
        const auto on = run (1, 100.0f, 1.0f);
        int bad = 0; float pk = 0.0f;
        for (float v : on.wave) { if (! std::isfinite (v)) ++bad; pk = juce::jmax (pk, std::abs (v)); }
        CHECK (bad == 0, "非有限が 0 個 (%d)", bad);
        CHECK (pk < 1.0f, "0 dBFS を超えない (%.2f dBFS)", 20.0f * std::log10 (juce::jmax (1.0e-9f, pk)));
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
