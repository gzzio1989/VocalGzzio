// v5.8.0 自動検証スイート:
//  1. REAPER歌唱時プツプツ音ゼロ検証（無音⇄歌唱再開・ピッチグライド・128サンプル等の任意ブロック長）
//  2. REAPER 128サンプル @ 48kHz (2.67ms予算) リアルタイムCPU負荷＆DAWライブ超低負荷モード検証
//  3. DawRealtimeHealthMonitor（プラグイン側超過 vs DAW/ASIOホスト側途切れ切り分け）＆状態保存検証
//  4. 全21テーマ配色（0..20）の WCAG 2.1 AA コントラスト比検証
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
int gFailures = 0;

void check (bool cond, const char* msg)
{
    if (! cond)
    {
        std::fprintf (stderr, "FAIL: %s\n", msg);
        ++gFailures;
    }
}

void setParam (VocalGzzioProcessor& proc, const char* id, float plainVal)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (proc.apvts.getParameterRange (id).convertTo0to1 (plainVal));
}

void drainStartupSelfTest (VocalGzzioProcessor& proc, int blockSize)
{
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    for (int i = 0; i < 64 && proc.isSelfTestRunning(); ++i)
    {
        buf.clear();
        proc.processBlock (buf, midi);
    }
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    // =========================================================================
    // 1. 歌唱時プツプツ音（波形不連続・クリック）ゼロ検証
    // =========================================================================
    for (double sr : { 44100.0, 48000.0 })
    {
        VocalGzzioProcessor proc;
        proc.setPlayConfigDetails (2, 2, sr, 2048);
        proc.prepareToPlay (sr, 2048);
        drainStartupSelfTest (proc, 256);

        for (int m = 0; m < gz::ModuleChain::Count; ++m)
            setParam (proc, gz::ModuleChain::paramId (m), 1.0f);
        setParam (proc, "dn_on",        1.0f);
        setParam (proc, "denoise",      58.0f);
        setParam (proc, "gate",        -52.0f);
        setParam (proc, "lowcut",       90.0f);
        setParam (proc, "mud",          -3.0f);
        setParam (proc, "harsh",        -2.5f);
        setParam (proc, "comp1",        35.0f);
        setParam (proc, "comp2",        52.0f);
        setParam (proc, "deess",        42.0f);
        setParam (proc, "presence",      3.0f);
        setParam (proc, "air",           2.8f);
        setParam (proc, "drive",        28.0f);
        setParam (proc, "at_on",         1.0f);
        setParam (proc, "at_amount",    72.0f);
        setParam (proc, "at_speed",     35.0f);
        setParam (proc, "argon_velvet", 65.0f);
        setParam (proc, "argon_focus",  60.0f);
        setParam (proc, "argon_silk",   55.0f);
        setParam (proc, "argon_halo",   40.0f);
        proc.setJpopPolishMode (2);
        proc.setMasteringPrismMode (4);

        const int kBlockSeq[] = { 128, 64, 17, 1, 128, 513, 128, 2048, 128 };
        double phase = 0.0;
        long totalSamples = 0;
        const long targetSamples = (long) (sr * 3.2);
        float prev1 = 0.0f, prev2 = 0.0f;
        bool haveHist = false;
        float maxAccel = 0.0f;
        bool allFinite = true;
        int seqIdx = 0;

        while (totalSamples < targetSamples)
        {
            const int n = kBlockSeq[seqIdx % 9];
            ++seqIdx;
            juce::AudioBuffer<float> buf (2, n);
            juce::MidiBuffer midi;

            for (int i = 0; i < n; ++i)
            {
                const double t = (double) (totalSamples + i) / sr;
                // 0.4秒周期で「無音ブレス区間 ⇄ 急峻ではない自然な歌唱立ち上がり＋ピッチグライド」を反復
                const double cyc = std::fmod (t, 0.45);
                double env = 0.0;
                if (cyc >= 0.09 && cyc < 0.40)
                {
                    const double fadeIn  = juce::jlimit (0.0, 1.0, (cyc - 0.09) / 0.012);
                    const double fadeOut = juce::jlimit (0.0, 1.0, (0.40 - cyc) / 0.015);
                    env = fadeIn * fadeOut;
                }
                const double f0 = 185.0 + 135.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 1.35 * t)
                                + 6.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.6 * t);
                phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                if (phase > juce::MathConstants<double>::twoPi)
                    phase -= juce::MathConstants<double>::twoPi;

                const float breathNoise = 0.004f * (float) std::sin (t * 17321.0 + std::sin (t * 419.0));
                const float voice = (float) (env * (0.28 * std::sin (phase)
                                                  + 0.15 * std::sin (phase * 2.0)
                                                  + 0.08 * std::sin (phase * 3.0)
                                                  + 0.04 * std::sin (phase * 4.0)
                                                  + 0.02 * std::sin (phase * 7.0))) + breathNoise;
                buf.setSample (0, i, voice);
                buf.setSample (1, i, voice);
            }

            proc.processBlock (buf, midi);
            if (proc.isSelfTestRunning())
            {
                haveHist = false;
                totalSamples += n;
                continue;
            }

            const float* outL = buf.getReadPointer (0);
            const float* outR = buf.getReadPointer (1);
            for (int i = 0; i < n; ++i)
            {
                const float y = outL[i];
                if (! std::isfinite (y) || ! std::isfinite (outR[i]) || std::abs (y) > 1.25f)
                    allFinite = false;
                if (haveHist && totalSamples + i > (long) (sr * 0.15))
                {
                    const float accel = std::abs (y - 2.0f * prev1 + prev2);
                    if (accel > maxAccel)
                        maxAccel = accel;
                }
                prev2 = prev1;
                prev1 = y;
                haveHist = (totalSamples + i >= 2);
            }
            totalSamples += n;
        }

        check (allFinite, "v5.8.0 Singing + silence transitions produce finite bounded samples across all block sizes");
        check (maxAccel < 0.34f, "v5.8.0 Singing + silence transitions have zero biquad/comb click discontinuities");
        proc.releaseResources();
    }

    // =========================================================================
    // 2. REAPER 128サンプル @ 48kHz (2666.7 us予算) リアルタイムCPU負荷ベンチマーク
    // =========================================================================
    {
        VocalGzzioProcessor proc;
        proc.setPlayConfigDetails (2, 2, 48000.0, 128);
        proc.prepareToPlay (48000.0, 128);
        drainStartupSelfTest (proc, 128);

        for (int m = 0; m < gz::ModuleChain::Count; ++m)
            setParam (proc, gz::ModuleChain::paramId (m), 1.0f);
        setParam (proc, "dn_on",        1.0f);
        setParam (proc, "denoise",      50.0f);
        setParam (proc, "at_on",        1.0f);
        setParam (proc, "at_amount",    75.0f);
        setParam (proc, "comp2",        50.0f);
        setParam (proc, "deess",        35.0f);
        setParam (proc, "argon_velvet", 55.0f);
        setParam (proc, "argon_focus",  50.0f);
        setParam (proc, "argon_silk",   50.0f);
        setParam (proc, "argon_halo",   35.0f);
        proc.setJpopPolishMode (2);

        proc.setMasteringPrismMode (4);

        juce::AudioBuffer<float> buf (2, 128);
        juce::MidiBuffer midi;
        std::vector<double> normTimesUs, ecoTimesUs;
        normTimesUs.reserve (600);
        ecoTimesUs.reserve  (600);
        double ph = 0.0;
        for (int round = 0; round < 640; ++round)
        {
            for (int mode = 0; mode < 2; ++mode)
            {
                proc.setDawLiveEcoMode (mode == 1);
                for (int i = 0; i < 128; ++i)
                {
                    ph += 2.0 * juce::MathConstants<double>::pi * 246.94 / 48000.0;
                    const float s = (float) (0.28 * std::sin (ph) + 0.14 * std::sin (ph * 2.0) + 0.07 * std::sin (ph * 3.0));
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }
                const auto t0 = std::chrono::steady_clock::now();
                proc.processBlock (buf, midi);
                const auto t1 = std::chrono::steady_clock::now();
                if (round >= 40)
                {
                    const double us = std::chrono::duration<double, std::micro> (t1 - t0).count();
                    if (mode == 0) normTimesUs.push_back (us);
                    else           ecoTimesUs.push_back  (us);
                }
            }
        }
        auto statsOf = [] (std::vector<double>& v) -> std::pair<double, double>
        {
            std::sort (v.begin(), v.end());
            double sum = 0.0;
            for (double x : v) sum += x;
            return { sum / (double) v.size(), v[(size_t) ((v.size() * 99) / 100)] };
        };

        const auto [normMeanUs, normP99Us] = statsOf (normTimesUs);
        const auto [ecoMeanUs,  ecoP99Us]  = statsOf (ecoTimesUs);
        std::printf ("[v5.8.0 128-smp @ 48kHz Budget=2666.7us] Normal Mean=%.1fus P99=%.1fus | LiveEco Mean=%.1fus P99=%.1fus\n",
                     normMeanUs, normP99Us, ecoMeanUs, ecoP99Us);

        // 2666.7us予算に対して平均30%未満・P99も80%未満で余裕をもって完走し、LiveEcoはさらに軽量であること
        check (normMeanUs < 800.0 && normP99Us < 2100.0,
               "v5.8.0 Normal singing 128-sample callback stays comfortably inside 2.67ms ASIO budget");
        check (ecoMeanUs < normMeanUs && ecoMeanUs < 500.0,
               "v5.8.0 DAW Live Ultra-Low-CPU Mode achieves strictly lower 128-sample singing CPU load");
        proc.releaseResources();
    }

    // =========================================================================
    // 3. DawRealtimeHealthMonitor 切り分け診断精度 ＆ APVTS XML 保存復元検証
    // =========================================================================
    {
        gz::v580::DawRealtimeHealthMonitor mon;
        mon.prepare (48000.0, 128);
        mon.resetCounters();
        // 正常な128サンプル @ 48kHz (予算 2.67ms) コールバック
        for (int i = 0; i < 10; ++i)
        {
            mon.beginCallback (128, 48000.0);
            mon.endCallback();
        }
        check (mon.pluginOverruns() == 0 && mon.hostGapDropouts() == 0,
               "v5.8.0 DawRealtimeHealthMonitor reports 0 overruns/dropouts during normal fast callbacks");

        // ホスト側（他トラックやASIOドライバ詰まり）でコールバック間隔が 25ms 空いたケースを模擬
        std::this_thread::sleep_for (std::chrono::milliseconds (25));
        {
            mon.beginCallback (128, 48000.0);
            mon.endCallback();
        }
        check (mon.hostGapDropouts() >= 1 && mon.pluginOverruns() == 0,
               "v5.8.0 DawRealtimeHealthMonitor isolates DAW/ASIO host gap dropout without blaming plugin");

        // APVTS 状態保存＆復元 + レポート出力検証
        VocalGzzioProcessor p1, p2;
        p1.setDawLiveEcoMode (true);
        juce::MemoryBlock stateBlob;
        p1.getStateInformation (stateBlob);
        p2.setStateInformation (stateBlob.getData(), (int) stateBlob.getSize());
        check (p2.getDawLiveEcoMode(),
               "v5.8.0 dawLiveEcoMode round-trips cleanly through VST3 state save/load");
        const juce::String rep = p1.buildReportText();
        check (rep.contains (juce::String::fromUTF8 ("DAWライブ超低負荷  : 入"))
               && rep.contains (juce::String::fromUTF8 ("[DAW リアルタイム負荷・音途切れ切り分け診断]")),
               "v5.8.0 buildReportText includes DAW real-time CPU & dropout diagnostic fields");

        // REAPER オフライン書き出し / 巨大可変ブロック分割時 (numSamples > preparedBlockSize) に
        // サブブロック再帰によって偽の hostGapDropouts が発生しないことを検証
        {
            VocalGzzioProcessor pBig;
            pBig.prepareToPlay (48000.0, 128);
            pBig.resetDawHealthCounters();
            juce::AudioBuffer<float> hugeBuf (2, 16384); // > preparedBlockSize (8192)
            juce::MidiBuffer emptyMidi;
            for (int blk = 0; blk < 3; ++blk)
            {
                hugeBuf.clear();
                pBig.processBlock (hugeBuf, emptyMidi);
                if (blk < 2)
                    std::this_thread::sleep_for (std::chrono::milliseconds (30));
            }
            check (pBig.getDawHostDropouts() == 0,
                   "v5.8.0 Oversized host block splitting (>preparedBlockSize) causes 0 false hostGapDropouts");
        }
    }

    // =========================================================================
    // 4. 全21テーマ配色 (0..20) の WCAG 2.1 AA コントラスト比検証 ＆ ライトテーマ白文字統一検証
    // =========================================================================
    for (int tm = 0; tm <= 20; ++tm)
    {
        Palette::applyTheme (tm);
        const float cInkPanel     = Palette::wcagContrast (Palette::ink,     Palette::panel);
        const float cInkSoftPanel = Palette::wcagContrast (Palette::inkSoft, Palette::panel);
        const float cInkPanel2    = Palette::wcagContrast (Palette::ink,     Palette::panel2);
        const float cOnBlue       = Palette::wcagContrast (Palette::readableOn (Palette::blue),   Palette::blue);
        const float cOnYellow     = Palette::wcagContrast (Palette::readableOn (Palette::yellow), Palette::yellow);
        const float cOnGreen      = Palette::wcagContrast (Palette::readableOn (Palette::green),  Palette::green);

        if (cInkPanel < 7.0f || cInkSoftPanel < 4.5f || cInkPanel2 < 6.0f
            || cOnBlue < 4.5f || cOnYellow < 4.5f || cOnGreen < 4.5f)
        {
            std::fprintf (stderr,
                          "Theme %d contrast failure: ink/panel=%.2f inkSoft/panel=%.2f ink/panel2=%.2f blue=%.2f yellow=%.2f green=%.2f\n",
                          tm, cInkPanel, cInkSoftPanel, cInkPanel2, cOnBlue, cOnYellow, cOnGreen);
        }
        check (cInkPanel >= 7.0f,     "v5.8.0 Theme primary ink vs panel meets WCAG AAA (>= 7.0:1)");
        check (cInkSoftPanel >= 4.5f, "v5.8.0 Theme secondary inkSoft vs panel meets WCAG AA (>= 4.5:1)");
        check (cInkPanel2 >= 6.0f,    "v5.8.0 Theme primary ink vs panel2 meets high contrast (>= 6.0:1)");
        check (cOnBlue >= 4.5f && cOnYellow >= 4.5f && cOnGreen >= 4.5f,
               "v5.8.0 readableOn(blue/yellow/green) meets WCAG AA (>= 4.5:1)");

        // モダン・ライトテーマ (1, 2, 5, 10, 12, 13) では全アクティブボタンの文字色が純白(#FFFFFF)に完全統一され、
        // かつ白カード上のアクセントアーク・曲線も WCAG AA (4.5:1) を満たすことを検証
        if (tm == 1 || tm == 2 || tm == 5 || tm == 10 || tm == 12 || tm == 13)
        {
            const bool allWhiteOnActive =
                Palette::readableOn (Palette::blue)   == juce::Colours::white &&
                Palette::readableOn (Palette::yellow) == juce::Colours::white &&
                Palette::readableOn (Palette::green)  == juce::Colours::white &&
                Palette::readableOn (Palette::salmon) == juce::Colours::white &&
                Palette::readableOn (Palette::ice)    == juce::Colours::white;
            const bool allArcsContrastOnCard =
                Palette::wcagContrast (Palette::blue,   Palette::panel) >= 4.5f &&
                Palette::wcagContrast (Palette::yellow, Palette::panel) >= 4.5f &&
                Palette::wcagContrast (Palette::green,  Palette::panel) >= 4.5f &&
                Palette::wcagContrast (Palette::salmon, Palette::panel) >= 4.5f &&
                Palette::wcagContrast (Palette::ice,    Palette::panel) >= 4.5f;
            check (allWhiteOnActive && allArcsContrastOnCard,
                   "v5.8.0 Light theme active buttons uniformly use crisp white text and >=4.5:1 card arcs");
        }
    }
    Palette::applyTheme (0);

    if (gFailures == 0)
        std::printf ("PASS: dsp_v580_reaper_crackle_and_cpu (0 failures)\n");
    return gFailures == 0 ? 0 : 1;
}
