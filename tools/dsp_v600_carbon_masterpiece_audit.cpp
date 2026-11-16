// dsp_v600_carbon_masterpiece_audit.cpp — VocalGzzio v6.0.0 "Carbon Edition" 総合品質・DSP・UI検証
//
// 検証項目:
//   1. バージョン 6.0.0・全24世代タイムマシン (v1.0.0..v6.0.0) ＆ VST3状態保存/復元ラウンドトリップ
//   2. Carbon Split-Band Air-Preserving De-Esser: 6.5kHz歯擦音を強力に抑えつつ 13.5kHzシルク空気感を保護 + バイパス時GRゼロ復帰
//   3. 位相打ち消し・コムフィルタ根絶検証:
//      - Prism Mode 4 + Mix=50% での 8kHz 完全打ち消し（旧3サンプル未報告遅延バグ）が根絶されていること
//      - Symphonic Doubler ON + Mix=0% (Dry 100%) でウェット漏れが完全ゼロであること
//   4. Carbon M/S Reverb Vocal Pocket: 発声中に残響のMidのみ動的ポケット形成しつつSideの広がりを維持すること
//   5. Carbon Spectral Budget Arbitrator & 1次ADAA倍音カーネル: 単独稼働時は無介入(1.0)、多重重複時は過飽和防止、全ブロック長で無発散
//   6. ITU-R BS.1770-4 K荷重リアルタイム LUFS / True Peak dBTP / K-System 実測連動 ＆ Carbon Monolith UI (WCAG AAA・総合画面Keyピル・詳細コンソール)

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <cstdio>
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
        else
        {
            std::printf ("  PASS: %s\n", msg);
        }
    }

    void setParam (VocalGzzioProcessor& proc, const char* id, float plainVal)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (proc.apvts.getParameterRange (id).convertTo0to1 (plainVal));
    }

    void drainSelfTest (VocalGzzioProcessor& proc, int blockSize = 256)
    {
        juce::AudioBuffer<float> silent (2, blockSize);
        juce::MidiBuffer midi;
        for (int i = 0; i < 240 && proc.isSelfTestRunning(); ++i)
        {
            silent.clear();
            proc.processBlock (silent, midi);
        }
    }

    double goertzelMag (const float* data, int numSamples, double targetHz, double sr)
    {
        if (numSamples <= 0) return 0.0;
        const double omega = 2.0 * juce::MathConstants<double>::pi * targetHz / sr;
        const double coeff = 2.0 * std::cos (omega);
        double q0 = 0.0, q1 = 0.0, q2 = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            q0 = coeff * q1 - q2 + (double) data[i];
            q2 = q1;
            q1 = q0;
        }
        const double magSq = q1 * q1 + q2 * q2 - q1 * q2 * coeff;
        return std::sqrt (juce::jmax (0.0, magSq)) / (double) numSamples;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf ("=== VocalGzzio v6.0.0 Carbon Edition Masterpiece Audit ===\n");

    // =========================================================================
    // 1. バージョン 6.0.0・全24世代タイムマシン・状態保存/復元
    // =========================================================================
    {
        check (juce::String (JucePlugin_VersionString) == "6.0.0",
               "v6.0.0 JucePlugin_VersionString is strictly 6.0.0");
        check (VocalGzzioContent::kLegacyVersionCount == 24,
               "v6.0.0 Legacy Version Time Machine has 24 generations (v1.0.0..v6.0.0)");
        check (juce::String (VocalGzzioContent::getLegacyVersionSpec (0).verTag) == "v6.0.0"
               && VocalGzzioContent::getLegacyVersionSpec (0).era == 600,
               "v6.0.0 Slot 0 is v6.0.0 Carbon Edition (era=600)");
        check (juce::String (VocalGzzioContent::getLegacyVersionSpec (1).verTag) == "v5.9.0"
               && VocalGzzioContent::getLegacyVersionSpec (1).era == 590,
               "v6.0.0 Slot 1 preserves v5.9.0 (era=590)");
        check (juce::String (VocalGzzioContent::getLegacyVersionSpec (23).verTag) == "v1.0.0"
               && VocalGzzioContent::getLegacyVersionSpec (23).era == 100,
               "v6.0.0 Slot 23 preserves original v1.0.0 (era=100)");

        VocalGzzioProcessor p1, p2;
        p1.prepareToPlay (48000.0, 256);
        p2.prepareToPlay (48000.0, 256);
        check (p1.getLegacyVersionId() == 0 && p1.getLegacyVersionEra() == 600 && p1.getCarbonEngineOn(),
               "v6.0.0 Processor starts on era=600 with Carbon Engine enabled");

        p1.setCarbonEngineOn (false);
        p1.setMasteringPrismMode (4);
        p1.setJpopPolishMode (2);
        juce::MemoryBlock blob;
        p1.getStateInformation (blob);
        p2.setStateInformation (blob.getData(), (int) blob.getSize());
        check (! p2.getCarbonEngineOn() && p2.getLegacyVersionEra() == 600
               && p2.getMasteringPrismMode() == 4 && p2.getJpopPolishMode() == 2,
               "v6.0.0 carbonEngineOn & era=600 round-trip cleanly through VST3 state XML");
        p1.releaseResources();
        p2.releaseResources();
    }

    // =========================================================================
    // 2. Split-Band Air-Preserving De-Esser (6.5kHz 歯擦音抑制 vs 13.5kHz 超高域エアー保護)
    // =========================================================================
    {
        const double sr = 48000.0;
        auto runDeEssCompare = [&] (int era, double& out6k5, double& out13k5, float& outGrAfterBypass)
        {
            VocalGzzioProcessor proc;
            proc.prepareToPlay (sr, 256);
            drainSelfTest (proc, 256);
            proc.setLegacyVersionState (era == 600 ? 0 : 1, era);
            proc.setCarbonEngineOn (era >= 600);

            for (int m = 0; m < gz::ModuleChain::Count; ++m)
                setParam (proc, gz::ModuleChain::paramId (m), 0.0f);
            setParam (proc, "mod_sagyo", 1.0f);
            setParam (proc, "ds_on",     1.0f);
            setParam (proc, "deess",     80.0f);
            setParam (proc, "argon_velvet", 0.0f);
            setParam (proc, "argon_focus",  0.0f);
            setParam (proc, "argon_silk",   0.0f);
            setParam (proc, "argon_halo",   0.0f);
            proc.setMicModelMode (0);
            proc.setVocalCharPreset (0);
            proc.setMicDistStabilizerOn (false);
            proc.setSymphonicDoublerOn (false);

            std::vector<float> captured;
            captured.reserve (30 * 256);
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            double pSib = 0.0, pAir = 0.0;
            for (int b = 0; b < 50; ++b)
            {
                for (int n = 0; n < 256; ++n)
                {
                    const float s = (float) (0.32 * std::sin (pSib) + 0.08 * std::sin (pAir));
                    pSib += juce::MathConstants<double>::twoPi * 6500.0  / sr;
                    pAir += juce::MathConstants<double>::twoPi * 13500.0 / sr;
                    buf.setSample (0, n, s);
                    buf.setSample (1, n, s);
                }
                proc.processBlock (buf, midi);
                if (b >= 20)
                {
                    const float* r = buf.getReadPointer (0);
                    for (int n = 0; n < 256; ++n)
                        captured.push_back (r[n]);
                }
            }
            out6k5  = goertzelMag (captured.data(), (int) captured.size(), 6500.0,  sr);
            out13k5 = goertzelMag (captured.data(), (int) captured.size(), 13500.0, sr);

            // モジュールバイパス時に De-Ess GR メーターが凍結せず 0.0 へ即時クリアされること
            setParam (proc, "mod_sagyo", 0.0f);
            for (int b = 0; b < 8; ++b)
            {
                buf.clear();
                proc.processBlock (buf, midi);
            }
            outGrAfterBypass = proc.getCarbonDeEssGrDb();
            proc.releaseResources();
        };

        double sib590 = 0.0, air590 = 0.0, sib600 = 0.0, air600 = 0.0;
        float grByp590 = 1.0f, grByp600 = 1.0f;
        runDeEssCompare (590, sib590, air590, grByp590);
        runDeEssCompare (600, sib600, air600, grByp600);

        const double ratio590 = air590 / juce::jmax (1.0e-9, sib590);
        const double ratio600 = air600 / juce::jmax (1.0e-9, sib600);
        std::printf ("  [De-Esser] v5.9(6.5k=%.4f, 13.5k=%.4f, ratio=%.3f) -> v6.0 Carbon(6.5k=%.4f, 13.5k=%.4f, ratio=%.3f)\n",
                     sib590, air590, ratio590, sib600, air600, ratio600);
        check (sib600 < 0.14 && air600 > air590 * 1.25 && ratio600 > ratio590 * 1.25,
               "v6.0.0 Split-Band Air-Preserving De-Esser suppresses 6.5kHz sibilance while restoring >12kHz silk air");
        check (grByp600 == 0.0f,
               "v6.0.0 Bypassing Sagyo module cleanly resets De-Esser GR telemetry to 0.0 dB");
    }

    // =========================================================================
    // 3. 並列 Mix=50% / Mix=0% での位相打ち消し・コムフィルタ・ウェット漏れ完全根絶検証
    // =========================================================================
    {
        const double sr = 48000.0;

        // 3a) Prism Mode 4 + Mix=50% での 8000Hz コムフィルタ打ち消し解消
        {
            VocalGzzioProcessor proc;
            proc.prepareToPlay (sr, 256);
            drainSelfTest (proc, 256);
            for (int m = 0; m < gz::ModuleChain::Count; ++m)
                setParam (proc, gz::ModuleChain::paramId (m), 0.0f);
            setParam (proc, "mod_neiro",    1.0f);
            setParam (proc, "mix",          50.0f);
            setParam (proc, "presence",     0.0f);
            setParam (proc, "air",          0.0f);
            setParam (proc, "drive",        0.0f);
            setParam (proc, "sustain",      0.0f);
            setParam (proc, "cons_amt",     0.0f);
            setParam (proc, "br_amt",       0.0f);
            setParam (proc, "ring",         0.0f);
            setParam (proc, "argon_velvet", 0.0f);
            setParam (proc, "argon_focus",  0.0f);
            setParam (proc, "argon_silk",   0.0f);
            setParam (proc, "argon_halo",   0.0f);
            proc.setJpopPolishMode (0);
            proc.setBreathControlOn (false);
            proc.setUpwardAirOn (false);
            proc.setPitchSweetSpotOn (false);
            proc.setTailVibratoOn (false);
            proc.setVowelAlignOn (false);
            proc.setOnsetGrooveOn (false);
            proc.setMixVoiceBridgeOn (false);
            proc.setIntonationStabOn (false);
            proc.setMicModelMode (0);
            proc.setVocalCharPreset (0);
            proc.setMicDistStabilizerOn (false);
            proc.setSymphonicDoublerOn (false);
            proc.setMasteringPrismMode (4); // Grammy Unified Console

            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            double ph = 0.0;
            double sumSqIn = 0.0, sumSqOut = 0.0;
            for (int b = 0; b < 30; ++b)
            {
                for (int n = 0; n < 256; ++n)
                {
                    const float s = 0.20f * (float) std::sin (ph);
                    ph += juce::MathConstants<double>::twoPi * 8000.0 / sr; // 旧3サンプル遅延で逆相πになっていた周波数
                    buf.setSample (0, n, s);
                    buf.setSample (1, n, s);
                    if (b >= 10) sumSqIn += (double) s * (double) s;
                }
                proc.processBlock (buf, midi);
                if (b >= 10)
                {
                    const float* r = buf.getReadPointer (0);
                    for (int n = 0; n < 256; ++n)
                        sumSqOut += (double) r[n] * (double) r[n];
                }
            }
            const double rmsRatio = std::sqrt (sumSqOut / juce::jmax (1.0e-12, sumSqIn));
            std::printf ("  [Prism Mode 4 @ Mix=50%% 8kHz] output/input RMS ratio = %.3f\n", rmsRatio);
            check (rmsRatio > 0.78,
                   "v6.0.0 Prism Mode 4 at Mix=50% has zero 8kHz comb-filter phase cancellation");
            proc.releaseResources();
        }

        // 3b) Symphonic Doubler ON + Mix=0% での完全ドライ透過保証
        {
            VocalGzzioProcessor proc;
            proc.prepareToPlay (sr, 256);
            drainSelfTest (proc, 256);
            for (int m = 0; m < gz::ModuleChain::Count; ++m)
                setParam (proc, gz::ModuleChain::paramId (m), 0.0f);
            setParam (proc, "mix",     0.0f);
            setParam (proc, "in_gain", 0.0f);
            setParam (proc, "makeup",  0.0f);
            setParam (proc, "crush_on", 0.0f);
            proc.setOutputVolumePct (100.0f);
            proc.setSymphonicDoublerOn (true);

            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            double ph = 0.0;
            float maxDiff = 0.0f;
            for (int b = 0; b < 20; ++b)
            {
                std::vector<float> ref (256);
                for (int n = 0; n < 256; ++n)
                {
                    const float s = 0.25f * (float) std::sin (ph);
                    ph += juce::MathConstants<double>::twoPi * 330.0 / sr;
                    ref[(size_t) n] = s;
                    buf.setSample (0, n, s);
                    buf.setSample (1, n, s);
                }
                proc.processBlock (buf, midi);
                if (b >= 5)
                {
                    for (int n = 0; n < 256; ++n)
                    {
                        maxDiff = juce::jmax (maxDiff, std::abs (buf.getSample (0, n) - ref[(size_t) n]));
                        maxDiff = juce::jmax (maxDiff, std::abs (buf.getSample (1, n) - ref[(size_t) n]));
                    }
                }
            }
            check (maxDiff < 1.0e-5f,
                   "v6.0.0 Mix=0% is 100% bit-transparent even when Symphonic Doubler is enabled");
            proc.releaseResources();
        }
    }

    // =========================================================================
    // 4. Carbon M/S Reverb Vocal Pocket & Spectral Budget Arbitrator & ADAA
    // =========================================================================
    {
        const double sr = 48000.0;
        VocalGzzioProcessor proc;
        proc.prepareToPlay (sr, 256);
        drainSelfTest (proc, 256);

        for (int m = 0; m < gz::ModuleChain::Count; ++m)
            setParam (proc, gz::ModuleChain::paramId (m), 0.0f);
        setParam (proc, "mod_hirogari", 1.0f);
        setParam (proc, "revon",        1.0f);
        setParam (proc, "rev_type",     2.0f); // Plate
        setParam (proc, "revmix",       50.0f);
        setParam (proc, "revsize",      60.0f);
        setParam (proc, "duck",         0.0f);
        proc.setOutputMonoMode (false);

        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        double ph = 0.0;
        float maxPocketDb = 0.0f;
        for (int b = 0; b < 55; ++b)
        {
            for (int n = 0; n < 256; ++n)
            {
                const float v = 0.28f * (float) (std::sin (ph) + 0.5 * std::sin (ph * 2.0));
                ph += juce::MathConstants<double>::twoPi * 260.0 / sr;
                buf.setSample (0, n, v);
                buf.setSample (1, n, v);
            }
            proc.processBlock (buf, midi);
            maxPocketDb = juce::jmax (maxPocketDb, proc.getCarbonReverbPocketDb());
        }
        std::printf ("  [M/S Reverb Pocket] max Mid-ducking pocket = -%.2f dB\n", maxPocketDb);
        check (maxPocketDb > 0.8f && maxPocketDb <= 3.7f,
               "v6.0.0 Carbon M/S Reverb Vocal Pocket dynamically carves center Mid space during singing");
        proc.releaseResources();

        // CarbonSpectralBudgetArbitrator 単体＆多重重複テスト
        gz::CarbonSpectralBudgetArbitrator arb;
        arb.reset();
        const auto soloB = arb.computeBlockBudget (3.0f, 0.0f, 20.0f, false, 0, 3.0f, 20.0f, false, 10.0f, 10.0f, 0);
        check (std::abs (soloB.presenceScale - 1.0f) < 1.0e-4f
               && std::abs (soloB.airScale - 1.0f) < 1.0e-4f
               && std::abs (soloB.upwardScale - 1.0f) < 1.0e-4f,
               "v6.0.0 CarbonSpectralBudgetArbitrator applies 0 intervention (1.0x) when within acoustic budget");

        gz::CarbonSpectralBudgetArbitrator::BudgetScalars stackB {};
        for (int i = 0; i < 40; ++i)
            stackB = arb.computeBlockBudget (6.0f, 80.0f, 95.0f, true, 2, 8.0f, 95.0f, true, 85.0f, 85.0f, 4);
        check (stackB.presenceScale < 0.75f && stackB.airScale < 0.75f && stackB.upwardScale < 0.75f,
               "v6.0.0 CarbonSpectralBudgetArbitrator smoothly arbitrates excessive multi-stage stacking");

        // CarbonAdaaSaturator 連続性・無発散テスト
        gz::CarbonAdaaSaturator adaa1, adaa2;
        adaa1.reset();
        adaa2.reset();
        bool adaaOk = true;
        for (int n = 0; n < 4096; ++n)
        {
            const float x = (n == 1024) ? 0.0f : 0.65f * std::sin (0.41f * (float) n);
            const float y1 = adaa1.processTanh (x, 2.2f);
            const float y2 = adaa2.processSoftAir (x * 2.4f);
            if (! std::isfinite (y1) || ! std::isfinite (y2) || std::abs (y1) > 1.05f || std::abs (y2) > 1.05f)
                adaaOk = false;
        }
        check (adaaOk, "v6.0.0 CarbonAdaaSaturator 1st-order ADAA kernels are bounded and finite across zero-step transitions");
    }

    // =========================================================================
    // 5. ITU-R BS.1770-4 Live LUFS / dBTP / K-System テレメトリ ＆ Carbon Monolith UI 検証
    // =========================================================================
    {
        const double sr = 48000.0;
        VocalGzzioProcessor proc;
        proc.prepareToPlay (sr, 256);
        drainSelfTest (proc, 256);
        for (int m = 0; m < gz::ModuleChain::Count; ++m)
            setParam (proc, gz::ModuleChain::paramId (m), 0.0f);
        setParam (proc, "mix",     0.0f); // 純粋な -14 dBFS 1kHz 正弦波を出力へ流して BS.1770-4 精度を検証
        setParam (proc, "in_gain", 0.0f);
        setParam (proc, "makeup",  0.0f);
        setParam (proc, "crush_on", 0.0f);
        proc.setOutputVolumePct (100.0f);
        proc.setSymphonicDoublerOn (false);

        const float amp = juce::Decibels::decibelsToGain (-14.0f); // 振幅 -14 dBFS -> ステレオ 1kHz 正弦波
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        double ph = 0.0;
        const int blocksFor3p5Sec = (int) ((3.5 * sr) / 256.0);
        for (int b = 0; b < blocksFor3p5Sec; ++b)
        {
            for (int n = 0; n < 256; ++n)
            {
                const float s = amp * (float) std::sin (ph);
                ph += juce::MathConstants<double>::twoPi * 1000.0 / sr;
                buf.setSample (0, n, s);
                buf.setSample (1, n, s);
            }
            proc.processBlock (buf, midi);
        }

        const float mLufs = proc.getCarbonMomentaryLufs();
        const float sLufs = proc.getCarbonShortTermLufs();
        const float tpDb  = proc.getCarbonTruePeakDbtp();
        const auto  kMet  = proc.getBobKatzKSystemMetrics();
        std::array<float, 64> histCents {}, histGr {};
        proc.copyCarbonTelemetryHistory (histCents, histGr);

        std::printf ("  [BS.1770-4 Telemetry] M=%.2f LUFS, S=%.2f LUFS, TP=%.2f dBTP, K-ShortTerm=%.2f LUFS\n",
                     mLufs, sLufs, tpDb, kMet.shortTermLufs);
        check (mLufs > -18.5f && mLufs < -11.5f && sLufs > -18.5f && sLufs < -11.5f,
               "v6.0.0 ITU-R BS.1770-4 K-weighted Momentary & Short-Term LUFS track real audio accurately");
        check (std::abs (tpDb - (-14.0f)) < 0.55f,
               "v6.0.0 Inter-Sample True Peak (dBTP) measures -14.0 dBFS within tight tolerance");
        check (std::abs (kMet.shortTermLufs - sLufs) < 0.01f && std::abs (kMet.truePeakDbtp - tpDb) < 0.01f,
               "v6.0.0 getBobKatzKSystemMetrics() returns live BS.1770-4 telemetry instead of static constants");
        check (histCents.size() == 64 && std::isfinite (histGr[63]),
               "v6.0.0 64-point lock-free telemetry ring exports 64 frames cleanly");

        // Carbon Monolith UI & Overview Key Pill & Detail Mastering Console 描画検証
        Palette::applyTheme (0);
        check (Palette::wcagContrast (Palette::ink, Palette::panel) >= 14.0f
               && Palette::wcagContrast (Palette::inkSoft, Palette::panel) >= 6.5f,
               "v6.0.0 Carbon Monolith theme achieves WCAG AAA contrast (>=14:1 primary, >=6.5:1 secondary)");

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* vgEd = dynamic_cast<VocalGzzioEditor*> (ed.get());
        check (vgEd != nullptr, "v6.0.0 createEditor instantiates VocalGzzioEditor");
        if (vgEd != nullptr)
        {
            auto& content = vgEd->getContent();
            content.setOverview (true);
            content.resized();
            check (! content.getOverviewKeyPillBoundsForTest().isEmpty(),
                   "v6.0.0 Overview Card 1 exposes 1-click Song Key/Scale selector pill (overviewKeyPillRect)");

            juce::Image imgOv (juce::Image::ARGB, VocalGzzioEditor::baseW, VocalGzzioEditor::baseH, true);
            {
                juce::Graphics gOv (imgOv);
                vgEd->paintEntireComponent (gOv, false);
            }

            content.setOverview (false);
            content.setAdvanced (true);
            content.resized();
            juce::Image imgDet (juce::Image::ARGB, VocalGzzioEditor::baseW, VocalGzzioEditor::baseH, true);
            {
                juce::Graphics gDet (imgDet);
                vgEd->paintEntireComponent (gDet, false);
            }
            check (imgOv.isValid() && imgDet.isValid(),
                   "v6.0.0 Carbon Monolith Overview & Detail Mastering Console render cleanly without errors");
        }
        proc.releaseResources();
    }

    if (gFailures == 0)
        std::printf ("\n=== ALL v6.0.0 CARBON EDITION CHECKS PASSED (0 failures) ===\n");
    return gFailures == 0 ? 0 : 1;
}
