#include "TestPaths.h"
// dsp_dnlearnrep.cpp — 「Learn を頻繁に押しながら使うと、10分くらいでサーが出る」の実測。
//
//  ユーザ報告(2026-09-01):
//    「普段、頻繁に Learn 押しながらやっても、その症状は10分くらいで出るよ？」
//
//  ★これまでの検査は Learn を**1回しか押していなかった**。そこが現場と違う。
//
//  疑い: 学習の採用の門は「うるさすぎたら捨てる」しか無い。
//        （loudestMed > -45dBFS で tooLoud、p95-med > 12dB で notSteady）
//        「静かすぎたら捨てる」門が無いので、たまたま静かな1.5秒で押すと
//        床が実際より低く決まる。学びなおしOFFなら、それが**凍ったまま**。
//        床が低い → openThr(床×2.5) が本物のノイズより下 → ゲートが閉じない
//        → 喋りはじめに全帯域が1msで開いた瞬間、部屋のノイズがサーッと出る。
//        押す回数が増えるほど「静かな1.5秒」を引く確率が上がる ＝ 10分くらいで出る。
//
//  部屋の音は、平均は変えずに ±4dB でゆっくり(40秒周期)波打たせる。
//  現実の部屋（ファンの回転むら・空調・自分の呼吸）はこうなっている。
//  平均が変わらないので、「部屋がうるさくなったから」では説明がつかない。
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

static void flatten (VocalGzzioProcessor& p)
{
    for (const char* id : { "comp1", "comp2", "ride_amt", "res_amt", "deess",
                            "seq_amount", "drive", "sustain", "cons_amt", "br_amt",
                            "ring", "hum_amt", "denoise", "makeup", "prox_amt",
                            "doubler", "width", "delay", "revmix" })
        setP (p, id, 0.0f);
    setP (p, "mud", 0.0f);      setP (p, "harsh", 0.0f);
    setP (p, "air", 0.0f);      setP (p, "presence", 0.0f);
    setP (p, "seq_on", 0.0f);   setP (p, "revon", 0.0f);
    setP (p, "dly_on", 0.0f);   setP (p, "gate", -80.0f);
}

// 5kHz 以上だけを見る測定用フィルタ（1次HPを8段。母音の裾を -91dBFS まで落とす）
struct HiMeas
{
    static constexpr int N = 8;
    float z[N] = {};
    float hp (float x)
    { const float a = 0.55f; float y = x;
      for (int i = 0; i < N; ++i) { z[i] = a * (z[i] + y); y = y - z[i]; }
      return y; }
};

struct Sample { double min; double quietAttenDb; double onsetAttenDb; double floor3; int learns; };

static std::vector<Sample> run (bool relearn, double learnEverySec, double minutes)
{
    const double SR = 44100.0;
    const int    bs = 128;

    VocalGzzioProcessor p;
    flatten (p);
    setP (p, "dn_on", 1);
    setP (p, "denoise", 60);
    setP (p, "dn_relearn", relearn ? 1.0f : 0.0f);
    p.prepareToPlay (SR, bs);

    juce::AudioBuffer<float> buf (2, bs);
    juce::MidiBuffer midi;

    unsigned rs = 7u;
    float roomAmp = 0.004f;
    auto room = [&rs, &roomAmp]
    { rs = rs * 1664525u + 1013904223u;
      return (float) ((int) (rs >> 9) - 4194304) / 4194304.0f * roomAmp; };
    double ph = 0.0;
    auto voice = [&ph]
    { double v = 0.0;
      for (int h = 1; h <= 10; ++h) v += std::sin (ph * h) / h;
      ph += 2.0 * juce::MathConstants<double>::pi * 196.0 / 44100.0;
      return (float) (0.10 * v); };

    const int nA     = (int) (2.5 * SR);
    const int nQuiet = (int) (6.5 * SR);
    const int nTalk  = (int) (1.5 * SR);
    const int nCycle = nQuiet + nTalk;
    const int onsetWin = (int) (0.150 * SR);
    const int total  = nA + (int) (minutes * 60.0 * SR);
    const int learnEvery = (int) (learnEverySec * SR);

    HiMeas mOut, mIn;
    std::vector<Sample> out;
    double eOnset = 0.0, eQuiet = 0.0; int nOnset = 0, nQuietN = 0;
    double iOnset = 0.0, iQuiet = 0.0;      // 入力側（同じ窓・同じフィルタ）
    int learns = 0;
    std::vector<float> inSamp ((size_t) bs);

    for (int done = 0; done < total; done += bs)
    {
        // ★Learn を繰り返し押す。押すのは「だまっている」ところ（人がそうするから）。
        if (done >= nA)
        {
            const int k = (done - nA) % nCycle;
            if (((done - nA) / learnEvery) != ((done - nA - bs) / learnEvery)
                && k < nQuiet - (int) (2.0 * SR))
            { p.requestDenoiseLearn(); ++learns; }
        }

        // 部屋の音: 平均は -48dBFS のまま、±4dB で 40秒周期に波打つ。
        {
            const double t = (double) done / SR;
            const double db = -48.0 + 4.0 * std::sin (2.0 * juce::MathConstants<double>::pi * t / 40.0);
            roomAmp = (float) (std::pow (10.0, db / 20.0) * std::sqrt (3.0));
        }

        auto* L = buf.getWritePointer (0);
        auto* R = buf.getWritePointer (1);
        for (int i = 0; i < bs; ++i)
        {
            const int t = done + i;
            float v = room();
            if (t >= nA) { const int k = (t - nA) % nCycle; if (k >= nQuiet) v += voice(); }
            inSamp[(size_t) i] = v;
            L[i] = v; R[i] = v;
        }
        p.processBlock (buf, midi);

        for (int i = 0; i < bs; ++i)
        {
            const int t = done + i;
            const float ho = mOut.hp (buf.getSample (0, i));
            const float hi = mIn .hp (inSamp[(size_t) i]);
            if (t < nA) continue;
            const int k = (t - nA) % nCycle;
            if (k >= nQuiet - onsetWin && k < nQuiet)
            { eQuiet += (double) ho * ho; iQuiet += (double) hi * hi; ++nQuietN; }
            if (k >= nQuiet && k < nQuiet + onsetWin)
            { eOnset += (double) ho * ho; iOnset += (double) hi * hi; ++nOnset; }
            if (k == nQuiet + onsetWin && nOnset > 0)
            {
                double f3 = 0.0;
                { juce::MemoryBlock mb; p.getStateInformation (mb);
                  if (auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()))
                      if (auto* dn = xml->getChildByName ("DENOISE"))
                          f3 = dn->getDoubleAttribute ("f3", 0.0); }
                // ★出力を入力でわる = そのとき部屋がどれだけ大きくても消える。
                //   これが「ノイズ除去がどれだけ効いているか」そのもの。
                out.push_back ({ (t - nA) / SR / 60.0,
                                 10.0 * std::log10 ((eQuiet + 1e-30) / (iQuiet + 1e-30)),
                                 10.0 * std::log10 ((eOnset + 1e-30) / (iOnset + 1e-30)),
                                 f3, learns });
                eOnset = eQuiet = iOnset = iQuiet = 0.0; nOnset = nQuietN = 0;
            }
        }
    }
    return out;
}

static double report (const char* title, const std::vector<Sample>& v)
{
    std::printf ("\n%s\n", title);
    if (v.empty()) { std::printf ("  (取れませんでした)\n"); return 0.0; }
    for (size_t i = 0; i < v.size(); i += juce::jmax<size_t> (1, v.size() / 10))
        std::printf ("    %5.1f分  Learn %2d回  だまりの削れ %6.2f dB  喋りはじめの削れ %6.2f dB  床3 %.3e\n",
                     v[i].min, v[i].learns, v[i].quietAttenDb, v[i].onsetAttenDb, v[i].floor3);
    // ★最初の1分（まだ Learn を押していない立ち上がり）は数に入れない。
    double best = 0.0, worst = -999.0; bool any = false;
    for (auto& s : v)
    {
        if (s.min < 1.0) continue;
        if (! any) { best = worst = s.quietAttenDb; any = true; }
        best  = juce::jmin (best,  s.quietAttenDb);   // いちばん削れている
        worst = juce::jmax (worst, s.quietAttenDb);   // いちばん削れていない
    }
    std::printf ("    → だまっている間の削れ: いちばん深い %.2f dB / いちばん浅い %.2f dB （差 %.2f dB）\n",
                 best, worst, worst - best);
    return worst - best;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_dnlearnrep");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } restore { autosave, backup };
    autosave.deleteFile();

    std::printf ("Learn を37秒ごとに押しながら30分。部屋の音は平均一定で ±4dB / 40秒周期。\n");
    std::printf ("平均が変わらないので「部屋がうるさくなったから」では説明がつかない。\n");

    const double wOff = report ("[1] 学びなおし OFF（v3.1 の既定）＝比較のための参考値", run (false, 37.0, 30.0));
    const double wOn  = report ("[2] 学びなおし ON （v4.0.0 の既定）＝出荷する設定", run (true,  37.0, 30.0));

    std::printf ("\n[3] 判定\n");
    std::printf ("    参考: OFF は %.2f dB 暴れる。Learn の窓は1.5秒しかなく、\n", wOff);
    std::printf ("          「静かすぎたら捨てる」門も無いので、たまたま静かな\n");
    std::printf ("          1.5秒で押すと床が低すぎるまま**凍る**。これが v3.1 の姿。\n");
    std::printf ("          （落とすのはここではなく、下の出荷設定のほう）\n\n");
    CHECK (wOn < 4.0,
           "出荷設定(学びなおしON): Learn を30回押しても削れが暴れない（差 %.2f dB < 4dB）", wOn);

    std::printf ("\n=======================================\n");
    std::printf (gFail == 0 ? "  dsp_dnlearnrep: ぜんぶ PASS\n" : "  dsp_dnlearnrep: %d 件 FAIL\n", gFail);
    std::printf ("=======================================\n");
    return gFail == 0 ? 0 : 1;
}
