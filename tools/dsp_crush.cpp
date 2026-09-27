#include "TestPaths.h"
// dsp_crush.cpp — v3.0「つぶさない」（音量が上がっても潰れないモード）の検証。
//
//  ご依頼:「音量が上がったときに音が潰れないようにするモードも実装してください」
//
//  「潰れる」を数字にする ------------------------------------------------------
//  声を張ったときの「詰まった感じ」は、**入れた音の起伏が、出てきた音では
//  小さくなっている**ことです。だから測るのは1つ。
//
//      伝達率 = (出てきた音の 山と谷の差) ÷ (入れた音の 山と谷の差)   [dB/dB]
//
//  1.0 なら「入れたぶんだけ出ている」。0.4 なら「6dB 張っても 2.4dB しか
//  出ていない」＝**張っても抜けない**。これが指摘の正体です。
//
//  ここで確かめること:
//   [1] OFF のときは、いままでと**1サンプルも変わらない**
//   [2] ふつうの声量では ON/OFF でほとんど変わらない（常時かかる細工ではない）
//   [3] 張ったとき、ON のほうが伝達率が高い（＝潰れにくい）
//   [4] 波形の頭の丸まり（クレストファクタ）が ON のほうが保たれる
//   [5] 0 dBFS を超えない（つぶさない代わりにクリップさせては本末転倒）
//   [6] 守った量が数字で取れる（画面に出すため）
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static void setP (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}

// 「音量そろえ」まわりだけを効かせた、素直な歌用の設定
static void singSetup (VocalGzzioProcessor& p)
{
    for (const char* id : { "ride_amt", "res_amt", "deess", "seq_amount", "cons_amt",
                            "br_amt", "ring", "hum_amt", "denoise", "prox_amt",
                            "doubler", "width", "delay", "revmix" })
        setP (p, id, 0.0f);
    setP (p, "mud", 0.0f); setP (p, "harsh", 0.0f);
    setP (p, "air", 0.0f); setP (p, "presence", 0.0f);
    setP (p, "seq_on", 0.0f); setP (p, "revon", 0.0f); setP (p, "dly_on", 0.0f);
    setP (p, "gate", -80.0f); setP (p, "dn_on", 0.0f);
    setP (p, "makeup", 0.0f); setP (p, "mix", 100.0f);

    setP (p, "comp1", 60.0f);       // ピーク圧縮
    setP (p, "comp2", 60.0f);       // ならし圧縮
    setP (p, "drive", 40.0f);       // あたたかみ（tanh の飽和 = 頭が丸くなる）
    setP (p, "sustain", 30.0f);     // のび
    setP (p, "ride_amt", 60.0f);    // 音量キープ
}

// 声っぽい合成音。gainDb で声量を変える。
struct Sing
{
    double ph = 0.0;
    void fill (float* d, int n, double sr, float gainDb)
    {
        const float g = juce::Decibels::decibelsToGain (gainDb);
        for (int i = 0; i < n; ++i)
        {
            double v = 0.0;
            for (int h = 1; h <= 12; ++h) v += std::sin (ph * h) / (double) h;
            ph += 2.0 * juce::MathConstants<double>::pi * 220.0 / sr;
            d[i] = (float) (0.20 * v) * g;
        }
    }
};

struct Result { double rmsDb, peakDb, crestDb; };

// ★測り方をやり直した理由（記録）
//  はじめは「静かな声だけの3秒」と「張った声だけの3秒」を**別々のインスタンス**で
//  測っていた。ところが張り検出は「いつもの声量にくらべて今どうか」を見る作りなので、
//  片方しか鳴らさない測り方では基準が決まらず、**どちらも張っている**と出た
//  （守りの表示が 6.00 dB のまま動かず、そこで気づいた）。
//
//  そこで実際の場面どおりに測る: **ひとつのインスタンスで、いつもの声量で4秒
//  歌ってから、サビで張る**。これなら基準が「いつもの声量」に落ち着いてから
//  張りが来るので、製品が現場で見るものと同じになる。
struct Take { Result quiet, loud; };

static Take measureTake (VocalGzzioProcessor& p, float quietDb, float loudDb, int bs)
{
    juce::AudioBuffer<float> buf (2, bs);
    juce::MidiBuffer midi;
    Sing s;
    const int nQuiet = (int) (44100 * 4);      // いつもの声量で4秒（基準が落ち着く）
    const int nLoud  = (int) (44100 * 3);      // そこから張って3秒
    const int qFrom  = (int) (44100 * 3);      // 静かな側は 3〜4秒目を測る
    const int lFrom  = nQuiet + (int) (44100 * 0.3);   // 張り側は 0.3秒目から測る

    double qs = 0.0, ls = 0.0; int qn = 0, ln = 0; float qp = 0.0f, lp = 0.0f;

    for (int done = 0; done < nQuiet + nLoud; done += bs)
    {
        const bool loud = (done >= nQuiet);
        float in[1024];
        s.fill (in, bs, 44100.0, loud ? loudDb : quietDb);
        for (int c = 0; c < 2; ++c)
            juce::FloatVectorOperations::copy (buf.getWritePointer (c), in, bs);
        p.processBlock (buf, midi);
        for (int i = 0; i < bs; ++i)
        {
            const int t = done + i;
            const float y = buf.getSample (0, i);
            if (t >= qFrom && t < nQuiet) { qs += (double) y * y; ++qn; qp = juce::jmax (qp, std::abs (y)); }
            if (t >= lFrom)               { ls += (double) y * y; ++ln; lp = juce::jmax (lp, std::abs (y)); }
        }
    }
    auto mk = [] (double sum, int n, float pk) -> Result
    {
        const double r = n ? std::sqrt (sum / (double) n) : 1e-9;
        const double rDb = 20.0 * std::log10 (std::max (r, 1e-9));
        const double pDb = 20.0 * std::log10 (std::max ((double) pk, 1e-9));
        return { rDb, pDb, pDb - rDb };
    };
    return { mk (qs, qn, qp), mk (ls, ln, lp) };
}

// [1] 用: 張った音だけを流して波形を集める（OFF 同士の一致を見るだけなので単純でよい）
static void collect (VocalGzzioProcessor& p, float gainDb, int bs, std::vector<float>& out)
{
    juce::AudioBuffer<float> buf (2, bs);
    juce::MidiBuffer midi;
    Sing s;
    for (int done = 0; done < (int) (44100 * 2); done += bs)
    {
        float in[1024];
        s.fill (in, bs, 44100.0, gainDb);
        for (int c = 0; c < 2; ++c)
            juce::FloatVectorOperations::copy (buf.getWritePointer (c), in, bs);
        p.processBlock (buf, midi);
        for (int i = 0; i < bs; ++i) out.push_back (buf.getSample (0, i));
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_crush");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct R { juce::File a, b;
        ~R() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } rst { autosave, backup };
    autosave.deleteFile();

    std::printf ("「つぶさない」（音量が上がっても潰れないモード）の検証\n\n");

    const int bs = 128;
    const float quietDb = -18.0f;     // いつもの声量
    const float loudDb  =  -6.0f;     // 張った声（+12 dB）

    // ---------------------------------------------------------------- [1]
    std::printf ("[1] OFF のときは今までと1サンプルも変わらないか\n");
    {
        std::vector<float> a, b;
        { VocalGzzioProcessor p; singSetup (p); setP (p, "crush_on", 0.0f);
          p.prepareToPlay (44100.0, bs); collect (p, loudDb, bs, a); }
        { VocalGzzioProcessor p; singSetup (p);   // crush_on は既定 OFF のまま触らない
          p.prepareToPlay (44100.0, bs); collect (p, loudDb, bs, b); }
        double mx = 0.0;
        for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
            mx = std::max (mx, (double) std::abs (a[i] - b[i]));
        CHECK (mx < 1e-9, "OFF とパラメータ未設定が一致 (最大差 %.3g)", mx);
    }

    // ---------------------------------------------------------------- [2][3][4]
    Take off_, on_;
    float onGuardQuiet = 0.0f, onGuardLoud = 0.0f;
    { VocalGzzioProcessor p; singSetup (p); setP (p, "crush_on", 0.0f);
      p.prepareToPlay (44100.0, bs); off_ = measureTake (p, quietDb, loudDb, bs); }
    { VocalGzzioProcessor p; singSetup (p); setP (p, "crush_on", 1.0f);
      p.prepareToPlay (44100.0, bs);
      // 守りの表示は、静かな区間の終わりと張り区間の終わりで拾う
      juce::AudioBuffer<float> tmp (2, bs); juce::MidiBuffer m; Sing s2;
      auto push = [&] (int samples, float db) {
          for (int d = 0; d < samples; d += bs) {
              float in[1024]; s2.fill (in, bs, 44100.0, db);
              for (int c = 0; c < 2; ++c)
                  juce::FloatVectorOperations::copy (tmp.getWritePointer (c), in, bs);
              p.processBlock (tmp, m); } };
      push ((int) (44100 * 4), quietDb); onGuardQuiet = p.getCrushGuardDb();
      push ((int) (44100 * 2), loudDb);  onGuardLoud  = p.getCrushGuardDb(); }
    { VocalGzzioProcessor p; singSetup (p); setP (p, "crush_on", 1.0f);
      p.prepareToPlay (44100.0, bs); on_ = measureTake (p, quietDb, loudDb, bs); }
    const Result offQ = off_.quiet, offL = off_.loud, onQ = on_.quiet, onL = on_.loud;

    const double inRise = loudDb - quietDb;                    // 入れた側の起伏 = 12 dB
    const double offRise = offL.rmsDb - offQ.rmsDb;
    const double onRise  = onL.rmsDb  - onQ.rmsDb;

    std::printf ("\n入れた側の起伏: %.1f dB（%.0f dB → %.0f dB）\n", inRise, quietDb, loudDb);
    std::printf ("  OFF: 出てきた起伏 %.2f dB（伝達率 %.0f%%）\n", offRise, 100.0 * offRise / inRise);
    std::printf ("  ON : 出てきた起伏 %.2f dB（伝達率 %.0f%%）\n", onRise,  100.0 * onRise  / inRise);

    std::printf ("\n[2] ふつうの声量では ON/OFF でほとんど変わらないか\n");
    const double quietDiff = std::abs (onQ.rmsDb - offQ.rmsDb);
    CHECK (quietDiff < 1.0, "いつもの声量での差 %.2f dB (<1.0 = 常時かかる細工ではない)", quietDiff);

    std::printf ("\n[3] 張ったとき、ON のほうが潰れないか\n");
    CHECK (onRise > offRise + 1.5,
           "張ったときの起伏が %.2f dB → %.2f dB へ改善 (+%.2f dB)",
           offRise, onRise, onRise - offRise);

    std::printf ("\n[4] 波形の頭の丸まり（山と実効値の差）が保たれるか\n");
    std::printf ("  OFF: ふつう %.2f dB / 張り %.2f dB（%.2f dB 失った）\n",
                 offQ.crestDb, offL.crestDb, offQ.crestDb - offL.crestDb);
    std::printf ("  ON : ふつう %.2f dB / 張り %.2f dB（%.2f dB 失った）\n",
                 onQ.crestDb, onL.crestDb, onQ.crestDb - onL.crestDb);
    CHECK ((onQ.crestDb - onL.crestDb) < (offQ.crestDb - offL.crestDb) + 0.01,
           "ON のほうが頭を丸めない (%.2f dB vs %.2f dB)",
           onQ.crestDb - onL.crestDb, offQ.crestDb - offL.crestDb);

    std::printf ("\n[5] 0 dBFS を超えないか（つぶさない代わりに割れては本末転倒）\n");
    CHECK (onL.peakDb < 0.0, "張ったときの山 %.2f dBFS (< 0)", onL.peakDb);

    // ---------------------------------------------------------------- [6]
    std::printf ("\n[6] 守った量が数字で取れるか（画面に出すため）\n");
    std::printf ("  いつもの声量: %.2f dB / 張ったとき: %.2f dB\n", onGuardQuiet, onGuardLoud);
    CHECK (onGuardQuiet < 1.0f, "いつもの声量では守りがほぼ 0 (%.2f dB)", onGuardQuiet);
    CHECK (onGuardLoud > onGuardQuiet + 2.0f,
           "張ったときだけ守りが増える (%.2f → %.2f dB)", onGuardQuiet, onGuardLoud);

    // ---------------------------------------------------------------- [7]
    std::printf ("\n[7] 使いかたごとのおすすめ（選び直したときの初期値）\n");
    //  画面で「音の種類」を選び直すと、この値に合わせてスイッチが動く。
    //  ここは純粋な対応表なので、表そのものを固定しておく。
    CHECK (crushRecommendedFor (0) == true,  "うた     → ON（大きく出した所が曲の山）");
    CHECK (crushRecommendedFor (1) == true,  "アコギ   → ON（強く弾いた所が曲の山）");
    CHECK (crushRecommendedFor (2) == false, "しゃべり → OFF（音量がそろっている方が聞き取りやすい）");

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
