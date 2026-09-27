#include "TestPaths.h"
// dsp_srcmode.cpp — v2.10.0 #77 アコギ用 / #78 しゃべり配信専用（音源モード）の検証
//
//  音源モードは「プリセット」ではなく、**同じツマミが見るところを持ち替える**作り。
//  だから確かめるべきは「値が入っているか」ではなく、
//  **その帯域が本当に動いているか**。ここでは正弦波を流して実測する。
//
//  確かめること:
//   1. うた(既定)は今までどおり — 明示的に0を入れても出音が1ビットも変わらない
//   2. こもりノブの効き所が変わる — うたは300Hz、アコギは220Hz、しゃべりは350Hz
//   3. きらめきの棚が変わる — アコギは9kHz(うたは11kHz)なので、9kHz で差が出る
//   4. ディエッサーが探す帯域が変わる — アコギは3kHz台のピック音に反応する
//   5. アコギではピッチ補正・ハモリを通さない → 申告遅延が0のまま
//   6. しゃべりでも明示的にONにしたひびき・やまびこが鳴る
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

// 指定の周波数の正弦波を流し、出てきた音のその周波数での大きさ(dB)を返す。
// 直交検波なので、他の帯域が混ざっても目当ての成分だけを測れる。
// ★ダイナミクス系を全部切る。切らないと、8dB の EQ 差が圧縮で 0.3dB まで
//   潰されて「効き所が変わったか」が測れない（最初これで誤 FAIL を出した）。
static void flatten (VocalGzzioProcessor& p)
{
    for (const char* id : { "comp1", "comp2", "ride_amt", "res_amt", "deess",
                            "seq_amount", "drive", "sustain", "cons_amt", "br_amt",
                            "ring", "hum_amt", "denoise", "makeup" })
        setP (p, id, 0.0f);
    setP (p, "mud", 0.0f);       // 既定は -3dB。基準を平らにしておく
    setP (p, "harsh", 0.0f);     // 既定は -1.5dB
    setP (p, "air", 0.0f);
    setP (p, "presence", 0.0f);
    setP (p, "seq_on", 0.0f);
    setP (p, "gate", -80.0f);
}

static float toneLevelDb (VocalGzzioProcessor& p, double hz, double amp = 0.25,
                          double seconds = 1.2)
{
    const double sr = 48000.0; const int block = 128;
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    const int total = (int) (sr * seconds);
    const int skip  = (int) (sr * 0.6);           // 立ち上がりは捨てる
    double ph = 0.0, sumI = 0.0, sumQ = 0.0; int cnt = 0;
    for (int done = 0; done < total; done += block)
    {
        auto* L = buf.getWritePointer (0); auto* R = buf.getWritePointer (1);
        for (int n = 0; n < block; ++n)
        {
            const float s = (float) (amp * std::sin (ph));
            ph += 2.0 * juce::MathConstants<double>::pi * hz / sr;
            if (ph > 2.0 * juce::MathConstants<double>::pi) ph -= 2.0 * juce::MathConstants<double>::pi;
            L[n] = s; R[n] = s;
        }
        p.processBlock (buf, midi);
        const int base = done;
        for (int n = 0; n < block; ++n)
        {
            if (base + n < skip) continue;
            const double t = 2.0 * juce::MathConstants<double>::pi * hz * (double) (base + n) / sr;
            const double y = buf.getReadPointer (0)[n];
            sumI += y * std::cos (t); sumQ += y * std::sin (t); ++cnt;
        }
    }
    if (cnt == 0) return -120.0f;
    const double mag = 2.0 * std::sqrt (sumI * sumI + sumQ * sumQ) / (double) cnt;
    return (float) (20.0 * std::log10 (mag + 1e-12));
}

// 「こもり」を -8dB にしたときの、その周波数での下がり方(dB, 正の値=下がった)
static float mudCutAt (int mode, double hz)
{
    float flat = 0.0f, cut = 0.0f;
    {   VocalGzzioProcessor p; flatten (p); setP (p, "src_mode", (float) mode);
        p.prepareToPlay (48000.0, 128); flat = toneLevelDb (p, hz); }
    {   VocalGzzioProcessor p; flatten (p); setP (p, "src_mode", (float) mode);
        setP (p, "mud", -8.0f);         // ★ツマミの範囲は -12..0 dB。マイナスが「削る」
        p.prepareToPlay (48000.0, 128); cut = toneLevelDb (p, hz); }
    return flat - cut;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    // autosave.xml が次のブロックに漏れると比べているものが変わる（dsp_dnlearn と同じ）
    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_srcmode");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } restore { autosave, backup };
    auto fresh = [&] { autosave.deleteFile(); };

    std::printf ("音源モード（うた / アコギ / しゃべり）の検証\n\n");

    // ---- 1. うたは今までどおり ----
    std::printf ("[1] うた(既定)の音が変わっていないか\n");
    {
        auto render = [&] (bool setMode, std::vector<float>& out)
        {
            fresh();
            VocalGzzioProcessor p;
            if (setMode) setP (p, "src_mode", 0.0f);
            setP (p, "hum_amt", 100); setP (p, "cons_amt", 35); setP (p, "comp2", 30);
            setP (p, "deess", 35); setP (p, "res_amt", 40); setP (p, "ride_amt", 35);
            setP (p, "br_amt", 40); setP (p, "ring", 30);
            p.prepareToPlay (48000.0, 128);
            const int block = 128;
            juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi;
            double ph = 0.0;
            for (int done = 0; done < 48000 * 2; done += block)
            {
                auto* L = buf.getWritePointer (0); auto* R = buf.getWritePointer (1);
                for (int n = 0; n < block; ++n)
                {
                    double s = 0.0;
                    for (int h = 1; h <= 8; ++h) s += std::sin (ph * h) / h;
                    ph += 2.0 * juce::MathConstants<double>::pi * 196.0 / 48000.0;
                    L[n] = R[n] = (float) (0.20 * s / 1.7);
                }
                p.processBlock (buf, midi);
                for (int n = 0; n < block; ++n) out.push_back (buf.getReadPointer (0)[n]);
            }
        };
        std::vector<float> a, b;
        render (false, a);   // 触らない（既定）
        render (true,  b);   // 明示的に「うた」を選ぶ
        double maxd = 0.0;
        const size_t n = juce::jmin (a.size(), b.size());
        for (size_t i = 0; i < n; ++i) maxd = juce::jmax (maxd, (double) std::abs (a[i] - b[i]));
        CHECK (maxd == 0.0, "「うた」を選んでも既定と1ビットも変わらない (最大差 %.3e)", maxd);
    }
    std::printf ("\n");

    // ---- 2. こもりの効き所 ----
    std::printf ("[2] こもりノブの効き所が音源で変わるか（-8dB を入れて実測）\n");
    {
        fresh();
        const float u220 = mudCutAt (0, 220.0), u300 = mudCutAt (0, 300.0), u350 = mudCutAt (0, 350.0);
        const float g220 = mudCutAt (1, 220.0), g300 = mudCutAt (1, 300.0);
        const float t300 = mudCutAt (2, 300.0), t350 = mudCutAt (2, 350.0);
        std::printf ("      うた  : 220Hz %.2f / 300Hz %.2f / 350Hz %.2f dB 下がる\n", u220, u300, u350);
        std::printf ("      アコギ: 220Hz %.2f / 300Hz %.2f dB 下がる\n", g220, g300);
        std::printf ("      しゃべり: 300Hz %.2f / 350Hz %.2f dB 下がる\n", t300, t350);
        CHECK (u300 > u220 + 1.0f, "うたは300Hzがいちばん下がる (300:%.2f > 220:%.2f)", u300, u220);
        CHECK (g220 > g300 + 1.0f, "アコギは220Hzがいちばん下がる (220:%.2f > 300:%.2f)", g220, g300);
        CHECK (t350 > t300 + 0.3f, "しゃべりは350Hz側が下がる (350:%.2f > 300:%.2f)", t350, t300);
        CHECK (g220 > u220 + 1.0f, "同じ220Hzでも、アコギの方がよく下がる (%.2f > %.2f)", g220, u220);
    }
    std::printf ("\n");

    // ---- 3. きらめきの棚 ----
    std::printf ("[3] きらめきの棚が音源で変わるか（+6dB を入れて 9kHz で実測）\n");
    {
        auto airLiftAt = [&] (int mode, double hz)
        {
            float flat = 0.0f, up = 0.0f;
            { fresh(); VocalGzzioProcessor p; flatten (p); setP (p, "src_mode", (float) mode);
              p.prepareToPlay (48000.0, 128); flat = toneLevelDb (p, hz); }
            { fresh(); VocalGzzioProcessor p; flatten (p); setP (p, "src_mode", (float) mode);
              setP (p, "air", 6.0f); p.prepareToPlay (48000.0, 128); up = toneLevelDb (p, hz); }
            return up - flat;
        };
        const float u9k = airLiftAt (0, 9000.0), g9k = airLiftAt (1, 9000.0);
        std::printf ("      9kHz での持ち上がり: うた %.2f dB / アコギ %.2f dB\n", u9k, g9k);
        CHECK (g9k > u9k + 0.5f, "アコギは棚が下(9kHz)にあるのでよく上がる (%.2f > %.2f)", g9k, u9k);
    }
    std::printf ("\n");

    // ---- 4. アコギでは声のための処理を通さない ----
    std::printf ("[4] アコギではピッチ補正・ハモリを通さない（遅延が増えない）\n");
    {
        fresh();
        VocalGzzioProcessor p;
        setP (p, "src_mode", 1.0f);              // アコギ
        setP (p, "at_on", 1); setP (p, "at_amount", 80);
        setP (p, "jn_on", 1); setP (p, "jn_mix", 55);
        setP (p, "vc_on", 1); setP (p, "vc_pitch", 3);
        p.prepareToPlay (48000.0, 128);
        juce::AudioBuffer<float> buf (2, 128); juce::MidiBuffer midi;
        buf.clear(); p.processBlock (buf, midi);
        CHECK (p.getLatencySamples() == 0,
               "申告遅延が 0 のまま (%d サンプル)", p.getLatencySamples());

        fresh();
        VocalGzzioProcessor q;                   // うたなら今までどおり遅延が出る
        setP (q, "at_on", 1); setP (q, "at_amount", 80);
        q.prepareToPlay (48000.0, 128);
        juce::AudioBuffer<float> b2 (2, 128); b2.clear(); juce::MidiBuffer m2;
        q.processBlock (b2, m2);
        CHECK (q.getLatencySamples() > 0,
               "うたでピッチ補正ONなら遅延は出る (%d サンプル)", q.getLatencySamples());
    }
    std::printf ("\n");

    // ---- 5. 用途を選んでも明示的なON操作を無効にしない ----
    std::printf ("[5] しゃべり配信でもONにしたひびき・やまびこが鳴るか\n");
    {
        auto tailAfterImpulse = [&] (int mode)
        {
            fresh();
            VocalGzzioProcessor p;
            setP (p, "src_mode", (float) mode);
            setP (p, "revon", 1); setP (p, "revmix", 60); setP (p, "revsize", 70);
            setP (p, "dly_on", 1); setP (p, "delay", 60);
            p.prepareToPlay (48000.0, 128);
            const int block = 128;
            juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi;
            double peakTail = 0.0;
            for (int done = 0; done < 48000 * 2; done += block)
            {
                buf.clear();
                if (done == 0) buf.setSample (0, 0, 0.9f), buf.setSample (1, 0, 0.9f);
                p.processBlock (buf, midi);
                if (done >= 24000)               // 0.5秒より後に残っている音＝残響
                    for (int n = 0; n < block; ++n)
                        peakTail = juce::jmax (peakTail, (double) std::abs (buf.getReadPointer (0)[n]));
            }
            return peakTail;
        };
        const double uta = tailAfterImpulse (0), talk = tailAfterImpulse (2);
        std::printf ("      0.5秒より後に残る音: うた %.3e / しゃべり %.3e\n", uta, talk);
        CHECK (uta > 1.0e-4, "うたでは残響が出る (%.3e)", uta);
        CHECK (talk > 1.0e-4, "しゃべりでも明示的なONに応じて残響が出る (%.3e)", talk);
    }
    std::printf ("\n");

    std::printf (gFail ? "== %d 件 FAIL ==\n" : "== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
