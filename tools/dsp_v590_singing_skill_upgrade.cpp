#include "TestPaths.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PresetDefs.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>
#include <cmath>
#include <vector>

// v5.9.0 歌唱力向上・初期プリセット刷新・ゼロ遅延ピッチ揺らぎダンパー統合検証テスト
static int gFail = 0;

#define CHECK(cond, ...) do { \
    if (cond) { std::printf ("  PASS: " __VA_ARGS__); std::printf ("\n"); } \
    else      { std::printf ("  FAIL: " __VA_ARGS__); std::printf ("\n"); ++gFail; } } while (0)

// ゼロ交差間隔から持続母音区間の瞬間F0標準偏差（セント単位）を高精度計測する補助関数
static float measurePitchDeviationCents (const std::vector<float>& sig, double sr, double nominalF0,
                                         int startSample, int endSample)
{
    std::vector<double> crossTimes;
    crossTimes.reserve (512);
    for (int n = juce::jmax (1, startSample); n < juce::jmin ((int) sig.size(), endSample); ++n)
    {
        const float y0 = sig[(size_t) (n - 1)];
        const float y1 = sig[(size_t) n];
        if (y0 < 0.0f && y1 >= 0.0f && (y1 - y0) > 1.0e-6f)
        {
            const double frac = (double) (-y0) / (double) (y1 - y0);
            crossTimes.push_back (((double) (n - 1) + frac) / sr);
        }
    }
    if (crossTimes.size() < 12)
        return 0.0f;

    std::vector<double> cents;
    cents.reserve (crossTimes.size() - 1);
    double sum = 0.0;
    for (size_t i = 1; i < crossTimes.size(); ++i)
    {
        const double period = crossTimes[i] - crossTimes[i - 1];
        if (period <= 0.0) continue;
        const double instHz = 1.0 / period;
        const double c = 1200.0 * std::log2 (instHz / nominalF0);
        cents.push_back (c);
        sum += c;
    }
    if (cents.empty())
        return 0.0f;
    const double mean = sum / (double) cents.size();
    double sq = 0.0;
    for (double c : cents)
        sq += (c - mean) * (c - mean);
    return (float) std::sqrt (sq / (double) cents.size());
}

static float bandRmsDb (const float* x, int n, double sr, float centerHz, float q)
{
    juce::dsp::IIR::Filter<float> bp;
    bp.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, centerHz, q);
    bp.reset();
    double sumSq = 0.0;
    const int skip = juce::jmin (n / 4, 2048);
    int cnt = 0;
    for (int i = 0; i < n; ++i)
    {
        const float y = bp.processSample (x[i]);
        if (i >= skip)
        {
            sumSq += (double) y * (double) y;
            ++cnt;
        }
    }
    const double rms = std::sqrt (sumSq / (double) juce::jmax (1, cnt));
    return juce::Decibels::gainToDecibels ((float) rms, -120.0f);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI guiInit;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    std::printf ("=== VocalGzzio v5.9.0 Singing-Skill & Pro Preset Upgrade Audit ===\n");

    // 1. VocalIntonationStabilizer 単体精度検査：2.1Hzフラつき制振 vs 5.8Hzビブラート保護 vs 子音保持率
    {
        auto runModulatedTest = [&] (double modHz, double depthCents) -> std::pair<float, float>
        {
            const int totalSamples = (int) (sr * 2.2);
            std::vector<float> dryL ((size_t) totalSamples, 0.0f);
            std::vector<float> wetL ((size_t) totalSamples, 0.0f);
            std::vector<float> wetR ((size_t) totalSamples, 0.0f);

            double phase = 0.0;
            std::vector<float> blockTrueF0;
            for (int n = 0; n < totalSamples; ++n)
            {
                const double t = (double) n / sr;
                const double modCents = depthCents * std::sin (juce::MathConstants<double>::twoPi * modHz * t);
                const double instF0 = 220.0 * std::pow (2.0, modCents / 1200.0);
                phase += juce::MathConstants<double>::twoPi * instF0 / sr;
                // 基本波優位の声帯波（ゼロ交差一意性のため高調波は緩やかに配合）
                const float s = (float) (0.55 * std::sin (phase) + 0.14 * std::sin (2.0 * phase));
                dryL[(size_t) n] = s;
                wetL[(size_t) n] = s;
                wetR[(size_t) n] = s;
                if ((n % blockSize) == blockSize / 2)
                    blockTrueF0.push_back ((float) instF0);
            }

            gz::v590::VocalIntonationStabilizer stab;
            stab.prepare (sr);
            int bIdx = 0;
            for (int pos = 0; pos + blockSize <= totalSamples; pos += blockSize, ++bIdx)
            {
                const float f0 = blockTrueF0[(size_t) juce::jmin (bIdx, (int) blockTrueF0.size() - 1)];
                stab.processBlock (wetL.data() + pos, wetR.data() + pos, blockSize, f0, 0.92f, 0.0f);
            }

            const int evalStart = (int) (sr * 0.55);
            const int evalEnd   = (int) (sr * 2.05);
            const float inSd  = measurePitchDeviationCents (dryL, sr, 220.0, evalStart, evalEnd);
            const float outSd = measurePitchDeviationCents (wetL, sr, 220.0, evalStart, evalEnd);
            return { inSd, outSd };
        };

        const auto [wobIn, wobOut] = runModulatedTest (2.1, 22.0);
        const float wobReductionPct = 100.0f * (1.0f - wobOut / juce::jmax (0.01f, wobIn));
        CHECK (wobReductionPct > 55.0f,
               "2.1Hz amateur pitch wobble damped by %.1f%% (%.2f -> %.2f cents RMS)",
               wobReductionPct, wobIn, wobOut);

        const auto [vibIn, vibOut] = runModulatedTest (5.8, 25.0);
        const float vibRetainPct = 100.0f * (vibOut / juce::jmax (0.01f, vibIn));
        CHECK (vibRetainPct > 85.0f,
               "5.8Hz expressive vibrato preserved at %.1f%% (%.2f -> %.2f cents RMS)",
               vibRetainPct, vibIn, vibOut);

        // 無声音（子音 s/t/k 高域バースト）のエネルギー100%保持検査
        gz::v590::VocalIntonationStabilizer stabCons;
        stabCons.prepare (sr);
        const int consLen = 4096;
        std::vector<float> consIn ((size_t) consLen, 0.0f);
        for (int n = 0; n < consLen; ++n)
        {
            const double t = (double) n / sr;
            consIn[(size_t) n] = (float) (0.25 * std::sin (juce::MathConstants<double>::twoPi * 5800.0 * t)
                                        + 0.18 * std::sin (juce::MathConstants<double>::twoPi * 8200.0 * t));
        }
        std::vector<float> consOutL = consIn, consOutR = consIn;
        for (int pos = 0; pos + blockSize <= consLen; pos += blockSize)
            stabCons.processBlock (consOutL.data() + pos, consOutR.data() + pos, blockSize, 0.0f, 0.0f, 0.0f);

        double eIn = 0.0, eOut = 0.0;
        for (int n = 256; n < consLen - 256; ++n)
        {
            eIn  += (double) consIn[(size_t) n] * consIn[(size_t) n];
            eOut += (double) consOutL[(size_t) n] * consOutL[(size_t) n];
        }
        const float consRetainPct = 100.0f * (float) std::sqrt (eOut / juce::jmax (1.0e-12, eIn));
        CHECK (consRetainPct > 98.5f && consRetainPct < 101.5f,
               "Unvoiced consonant HF burst retention = %.2f%%", consRetainPct);
    }

    // 2. 実機 VocalGzzioProcessor 統合検査：旧v5.8.0初期状態 vs v5.9.0 applyProSingingInitialSetup()
    {
        VocalGzzioProcessor legacyProc;
        VocalGzzioProcessor proProc;
        legacyProc.prepareToPlay (sr, blockSize);
        proProc.prepareToPlay (sr, blockSize);
        proProc.applyProSingingInitialSetup();

        CHECK (proProc.getLatencySamples() == 0,
               "applyProSingingInitialSetup keeps zero reported PDC latency (%d samples)",
               proProc.getLatencySamples());
        CHECK (proProc.isSingingSkillSuiteActive() && proProc.getIntonationStabOn(),
               "Singing-Skill Suite & Intonation Stabilizer active out-of-the-box");
        CHECK ((int) std::lround (proProc.apvts.getRawParameterValue ("rev_type")->load()) == 2
               && proProc.apvts.getRawParameterValue ("seq_on")->load() > 0.5f
               && proProc.apvts.getRawParameterValue ("crush_on")->load() > 0.5f,
               "Studio Plate reverb (type=2), Smart EQ ON, and Crush Guard ON configured");

        // 高音ハイトーン（392Hz G4 + 2.1Hz揺らぎ + ロングトーン + 無音テール）を通し、
        // at_on == OFF でも補助F0追跡から歌唱アシスト群とスタジオプレート残響が連動することを検証
        const int singSamples = (int) (sr * 1.4);
        const int tailSamples = (int) (sr * 0.5);
        const int totalLen    = singSamples + tailSamples;
        std::vector<float> legOut ((size_t) totalLen, 0.0f);
        std::vector<float> proOut ((size_t) totalLen, 0.0f);

        double ph = 0.0;
        juce::AudioBuffer<float> bufLeg (2, blockSize), bufPro (2, blockSize);
        juce::MidiBuffer midi;
        float maxSweetDb = 0.0f, maxIntStabCents = 0.0f, maxTailVibDb = 0.0f;

        for (int pos = 0; pos + blockSize <= totalLen; pos += blockSize)
        {
            for (int n = 0; n < blockSize; ++n)
            {
                float s = 0.0f;
                const int idx = pos + n;
                if (idx < singSamples)
                {
                    const double t = (double) idx / sr;
                    const double wob = (t < 0.85) ? (20.0 * std::sin (juce::MathConstants<double>::twoPi * 2.1 * t)) : 0.0;
                    const double f0  = 392.0 * std::pow (2.0, wob / 1200.0);
                    ph += juce::MathConstants<double>::twoPi * f0 / sr;
                    // こもり成分(310Hz付近)＋基本波＋倍音を持つ素の声モデル
                    s = (float) (0.18 * std::sin (ph)
                               + 0.10 * std::sin (2.0 * ph)
                               + 0.05 * std::sin (3.0 * ph)
                               + 0.03 * std::sin (8.0 * ph)
                               + 0.02 * std::sin (11.0 * ph)
                               + 0.07 * std::sin (juce::MathConstants<double>::twoPi * 305.0 * t));
                }
                bufLeg.setSample (0, n, s); bufLeg.setSample (1, n, s);
                bufPro.setSample (0, n, s); bufPro.setSample (1, n, s);
            }
            legacyProc.processBlock (bufLeg, midi);
            proProc.processBlock (bufPro, midi);

            maxSweetDb      = juce::jmax (maxSweetDb,      proProc.getPitchSweetSpotMeterDb());
            maxIntStabCents = juce::jmax (maxIntStabCents, proProc.getIntonationStabMeterCents());
            maxTailVibDb    = juce::jmax (maxTailVibDb,    proProc.getTailVibratoMeterDb());

            for (int n = 0; n < blockSize; ++n)
            {
                legOut[(size_t) (pos + n)] = bufLeg.getSample (0, n);
                proOut[(size_t) (pos + n)] = bufPro.getSample (0, n);
            }
        }

        CHECK (maxSweetDb > 0.4f && maxIntStabCents > 1.0f && maxTailVibDb > 0.4f,
               "Zero-latency F0 feeds PitchSweet(%.2fdB), IntonationStab(%.2fcents), TailVib(%.2fdB) with at_on=OFF",
               maxSweetDb, maxIntStabCents, maxTailVibDb);

        // ボーカル抜け感（3.2〜5kHz 対 300Hzこもり比）が旧初期状態より明確に向上していること
        const float legClarity = bandRmsDb (legOut.data(), singSamples, sr, 4000.0f, 0.9f)
                               - bandRmsDb (legOut.data(), singSamples, sr,  305.0f, 1.4f);
        const float proClarity = bandRmsDb (proOut.data(), singSamples, sr, 4000.0f, 0.9f)
                               - bandRmsDb (proOut.data(), singSamples, sr,  305.0f, 1.4f);
        CHECK (proClarity > legClarity + 1.2f,
               "Vocal presence-to-mud clarity improved: %.2f dB -> %.2f dB (+%.2f dB)",
               legClarity, proClarity, proClarity - legClarity);

        // フレーズ終了後のスタジオプレート余韻がダッキング解除とともに豊かに広がること
        double legTailSq = 0.0, proTailSq = 0.0;
        for (int i = singSamples + (int) (0.05 * sr); i < totalLen; ++i)
        {
            legTailSq += (double) legOut[(size_t) i] * legOut[(size_t) i];
            proTailSq += (double) proOut[(size_t) i] * proOut[(size_t) i];
        }
        const float legTailDb = juce::Decibels::gainToDecibels ((float) std::sqrt (legTailSq / tailSamples), -120.0f);
        const float proTailDb = juce::Decibels::gainToDecibels ((float) std::sqrt (proTailSq / tailSamples), -120.0f);
        CHECK (proTailDb > legTailDb + 2.5f,
               "Post-phrase Studio Plate bloom richer than legacy box: %.2f dB -> %.2f dB",
               legTailDb, proTailDb);

        legacyProc.releaseResources();
        proProc.releaseResources();
    }

    // 3. 未操作旧初期状態の自動マイグレーション安全性 ＆ Detail UI バグ修正（sceneTalk誤点灯解消・クイックボタン）検証
    {
        VocalGzzioProcessor pMigrate;
        CHECK (pMigrate.migrateUntouchedLegacyStateIfNeeded(),
               "Untouched legacy v5.8.0 factory state cleanly upgrades to v5.9.0 Pro Singing setup");

        VocalGzzioProcessor pCustom;
        if (auto* prm = pCustom.apvts.getParameter ("revmix"))
            prm->setValueNotifyingHost (pCustom.apvts.getParameterRange ("revmix").convertTo0to1 (28.0f));
        CHECK (! pCustom.migrateUntouchedLegacyStateIfNeeded(),
               "User-customized project state is strictly preserved without overwrite");

        VocalGzzioProcessor pUi;
        std::unique_ptr<juce::AudioProcessorEditor> ed (pUi.createEditor());
        CHECK (ed != nullptr, "Editor created cleanly");
        if (auto* vEd = dynamic_cast<VocalGzzioEditor*> (ed.get()))
        {
            auto& content = vEd->getContent();
            content.setOverview (false);
            int litScenes = 0;
            for (auto* ch : content.getChildren())
                if (auto* tb = dynamic_cast<juce::TextButton*> (ch))
                    if (tb->getRadioGroupId() == 2002 && tb->getToggleState())
                        ++litScenes;
            CHECK (litScenes == 0,
                   "No scene button falsely highlighted when ui_scene == -1 (litScenes=%d)", litScenes);

            auto* skillBtn = dynamic_cast<juce::TextButton*> (content.findChildWithID ("singSkillQuick"));
            auto* pitchBtn = dynamic_cast<juce::TextButton*> (content.findChildWithID ("quickPitch"));
            CHECK (skillBtn != nullptr && pitchBtn != nullptr && skillBtn->isVisible() && pitchBtn->isVisible(),
                   "Detail header quick-assist buttons (singSkillQuick & quickPitch) visible without overlap");
            if (skillBtn != nullptr && skillBtn->onClick)
            {
                skillBtn->onClick();
                CHECK (pUi.isSingingSkillSuiteActive() && skillBtn->getToggleState(),
                       "singSkillQuick click enables Singing-Skill Suite");
            }
            if (pitchBtn != nullptr && pitchBtn->onClick)
            {
                pitchBtn->onClick();
                CHECK (pUi.apvts.getRawParameterValue ("at_on")->load() > 0.5f
                       && (int) std::lround (pUi.apvts.getRawParameterValue ("at_scale")->load()) == 0
                       && pitchBtn->getToggleState(),
                       "quickPitch click enables Auto-Pitch with safe Chromatic scale when key unscanned");
            }
            pUi.applyProSingingInitialSetup();
            auto* revBox = dynamic_cast<juce::ComboBox*> (content.findChildWithID ("reverbType"));
            CHECK (revBox != nullptr && revBox->getSelectedItemIndex() == 2,
                   "reverbType ComboBox 1:1 maps rev_type=2 to index 2 (Plate) without normalization skew");
        }
    }

    std::printf ("=== Result: %s (%d failures) ===\n", gFail == 0 ? "ALL PASSED" : "FAILED", gFail);
    return gFail == 0 ? 0 : 1;
}
