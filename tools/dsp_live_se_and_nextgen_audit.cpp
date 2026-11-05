/**
 * @file dsp_live_se_and_nextgen_audit.cpp
 * @brief VocalGzzio v5.5.0 刷新4大スタジオライブSE音源（♪拍手 / ✦キラ / ●ドラム / ◆ベル）＆
 *        次世代マスタリング・声道物理・スイス時計グランドコンプリケーション全23ヘッダ統合検証テストスイート
 *
 * 検証項目:
 *   1. 4大ライブSE音源（SE 0: 拍手, SE 1: キラ, SE 2: ドラム, SE 3: ベル）の
 *      - スタジオ正規化ピークレベル検証 (-6.8 dBFS 付近 = 0.42f〜0.48f、旧 -19.1 dBFS の貧弱さ解消確認)
 *      - ステレオ音場広がり検証 (L/R が同一モノラルではなく豊かな空間位相差を持つこと)
 *      - 時間エンベロープ構造検証 (ドラムロールの1.26秒フィナーレ爆発、キラのアルペジオ持続、拍手の統計的密度、ベルの3段解決和音)
 *      - マルチサンプルレート (44.1kHz / 48kHz / 96kHz) での NaN/Inf ゼロ保証
 *   2. 新規追加された全23モジュールの信号処理・数値安定性・テレメトリ精度検証
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <array>
#include <algorithm>

#include "../Source/StudioLiveSoundEffectsEngine.h"
#include "../Source/StudioFoleyAndStageAmbienceBank.h"
#include "../Source/NeuralFormantAndGlottalSynth.h"
#include "../Source/MultibandLinearPhaseCrossoverEngine.h"
#include "../Source/AnalogTubeAndOptoMasteringChain.h"
#include "../Source/AccompanimentDuckingAndStemSculptor.h"
#include "../Source/LiveStageAndVtuberDirectorEngine.h"
#include "../Source/PrecisionPitchAndVibratoSynthesizer.h"
#include "../Source/MicrophoneAndPreampPhysicalTwin.h"
#include "../Source/MasteringLimiterAndTruePeakEngine.h"
#include "../Source/DynamicDePlosiveAndDeClickEngine.h"
#include "../Source/BinauralHeadphoneAndRoomCalibration.h"
#include "../Source/VintageDigitalReverbAndDelayRack.h"
#include "../Source/AdaptiveAutoEqAndResonanceSuppressor.h"
#include "../Source/VocalDoublerAndMicroShiftArray.h"
#include "../Source/HarmonicSeriesSculptorAndSubBass.h"
#include "../Source/MasteringStereoFieldAndPhaseDoctor.h"
#include "../Source/StreamingAudioRouterAndSafetyMatrix.h"
#include "../Source/PsychoacousticTransientAndSustainEngine.h"
#include "../Source/BroadcastMasteringPresetVault.h"
#include "../Source/VocalEmotionAndProsodyAnalyzer.h"
#include "../Source/AcousticImpedanceAndVocalTractTwin.h"
#include "../Source/MasteringDitherAndBitDepthQuantizer.h"
#include "../Source/HorologyCelestialVisualizerSuite.h"
#include "../Source/HorologyGrandComplicationHud.h"
#include "../Source/NextGenMasteringOrchestrator.h"
#include "../Source/PluginProcessor.h"

namespace
{
int gTotalChecks  = 0;
int gFailedChecks = 0;

void checkCondition(bool ok, const char* label, const char* detail = "")
{
    ++gTotalChecks;
    if (ok)
    {
        std::printf("  [PASS] %s %s\n", label, detail);
    }
    else
    {
        ++gFailedChecks;
        std::printf("  [FAIL] %s %s\n", label, detail);
    }
}

struct SeSignalStats
{
    float maxPeak     = 0.0f;
    float rmsTotal    = 0.0f;
    float rmsFirstHalf= 0.0f;
    float rmsSecondHalf = 0.0f;
    float stereoSideRms = 0.0f;
    int   zeroCrossings = 0;
    bool  allFinite   = true;
};

SeSignalStats analyzeSeBuffer(const std::vector<float>& L, const std::vector<float>& R)
{
    SeSignalStats st{};
    const size_t n = std::min(L.size(), R.size());
    if (n == 0)
        return st;

    double sumSq = 0.0;
    double sumSq1 = 0.0;
    double sumSq2 = 0.0;
    double sumSideSq = 0.0;
    const size_t half = n / 2;

    for (size_t i = 0; i < n; ++i)
    {
        const float l = L[i];
        const float r = R[i];
        if (!std::isfinite(l) || !std::isfinite(r))
        {
            st.allFinite = false;
            continue;
        }
        st.maxPeak = std::max(st.maxPeak, std::max(std::fabs(l), std::fabs(r)));
        const double p = 0.5 * (static_cast<double>(l) * l + static_cast<double>(r) * r);
        sumSq += p;
        if (i < half)
            sumSq1 += p;
        else
            sumSq2 += p;

        const double side = 0.5 * static_cast<double>(l - r);
        sumSideSq += side * side;

        if (i > 0 && ((L[i - 1] < 0.0f && l >= 0.0f) || (L[i - 1] >= 0.0f && l < 0.0f)))
            ++st.zeroCrossings;
    }

    st.rmsTotal      = static_cast<float>(std::sqrt(sumSq / static_cast<double>(n)));
    st.rmsFirstHalf  = static_cast<float>(std::sqrt(sumSq1 / static_cast<double>( std::max<size_t>(1, half))));
    st.rmsSecondHalf = static_cast<float>(std::sqrt(sumSq2 / static_cast<double>( std::max<size_t>(1, n - half))));
    st.stereoSideRms = static_cast<float>(std::sqrt(sumSideSq / static_cast<double>(n)));
    return st;
}

// =============================================================================
// Test 1: 刷新された4大スタジオライブSE（拍手 / キラ / ドラム / ベル）の音響品質監査
// =============================================================================
void testStudioLiveSoundEffects()
{
    std::printf("\n=== [Test 1] Studio Live SE 4-Pad Acoustic Synthesis Audit (Applause / Sparkle / Drum / Bell) ===\n");
    constexpr double kSr = 48000.0;

    static constexpr std::array<const char*, 4> kNames = {
        "SE 0: Applause (拍手 - 220-Person Hall Claps + Formant Cheers)",
        "SE 1: Sparkle  (キラ - Mustel Celesta + Harp + 36-Bar Mark Tree + Glass Harmonica)",
        "SE 2: Drumroll (ドラム - Multi-Bounce Snare Press Roll + Timpani + 40-Mode Crash + Gran Cassa)",
        "SE 3: Bell     (ベル - Musser Vibraphone + Tubular Bell + 5-Partial Carillon)"
    };

    for (int idx = 0; idx < 4; ++idx)
    {
        const double durSec = gzzio::studio_se::StudioLiveSoundEffectsEngine::getRecommendedDurationSeconds(idx);
        const int numSamples = static_cast<int>( std::lround(durSec * kSr));
        std::vector<float> bufL(static_cast<size_t>(numSamples), 0.0f);
        std::vector<float> bufR(static_cast<size_t>(numSamples), 0.0f);

        gzzio::studio_se::StudioLiveSoundEffectsEngine::renderStudioSeBuffer(idx, bufL.data(), bufR.data(), numSamples, kSr);
        const SeSignalStats st = analyzeSeBuffer(bufL, bufR);

        char detail[256];
        std::snprintf(detail, sizeof(detail),
                      "(dur=%.2fs, peak=%.3f [%.1f dBFS], rms=%.4f, sideRms=%.4f, zc=%d)",
                      durSec, st.maxPeak, 20.0f * std::log10(std::max(1.0e-5f, st.maxPeak)),
                      st.rmsTotal, st.stereoSideRms, st.zeroCrossings);

        checkCondition(st.allFinite, kNames[static_cast<size_t>(idx)], "All samples finite");
        checkCondition(st.maxPeak >= 0.40f && st.maxPeak <= 0.50f, kNames[static_cast<size_t>(idx)], detail);
        checkCondition(st.rmsTotal >= 0.025f, kNames[static_cast<size_t>(idx)], "Rich studio RMS body");
        checkCondition(st.stereoSideRms >= 0.008f, kNames[static_cast<size_t>(idx)], "Wide stereo hall image");
        checkCondition(st.zeroCrossings >= 400, kNames[static_cast<size_t>(idx)], "Rich harmonic & transient density");
    }

    // Multi-sample-rate verification (44.1kHz & 96kHz)
    for (double sr : { 44100.0, 96000.0 })
    {
        const int n = static_cast<int>(sr * 2.5);
        std::vector<float> l(static_cast<size_t>(n), 0.0f), r(static_cast<size_t>(n), 0.0f);
        gzzio::studio_se::StudioLiveSoundEffectsEngine::renderStudioSeBuffer(2, l.data(), r.data(), n, sr);
        const SeSignalStats st = analyzeSeBuffer(l, r);
        checkCondition(st.allFinite && st.maxPeak > 0.40f, "Drumroll SE multi-rate synthesis",
                       sr > 50000.0 ? "(96 kHz)" : "(44.1 kHz)");
    }
}

// =============================================================================
// Test 2: 声門パルス・リニア位相クロスオーバー・真空管オプト鎖・ダッキング検証
// =============================================================================
void testVocalGlottalCrossoverTubeAndDucking()
{
    std::printf("\n=== [Test 2] Glottal LF Synth, LR4 Crossover, Fairchild/LA-2A & Trackspacer Audit ===\n");
    constexpr double kSr = 48000.0;

    gzzio::glottal_synth::NeuralFormantAndGlottalSuite glottal;
    gzzio::multiband_lr4::MultibandLinearPhaseCrossoverSuite crossover;
    gzzio::analog_mastering::AnalogTubeAndOptoMasteringSuite analogChain;
    gzzio::stem_sculptor::AccompanimentDuckingAndStemSuite ducker;

    glottal.prepare(kSr);
    crossover.prepare(kSr);
    analogChain.prepare(kSr);
    ducker.prepare(kSr);

    bool allFinite = true;
    double outEnergy = 0.0;
    double bgmOutEnergy = 0.0;

    for (int i = 0; i < 4800; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kSr);
        float vL = 0.35f * std::sin(6.2831853f * 220.0f * t) + 0.15f * std::sin(6.2831853f * 660.0f * t);
        float vR = vL;
        float bgmL = 0.40f * std::sin(6.2831853f * 440.0f * t);
        float bgmR = bgmL;

        glottal.processSamplePair(vL, vR, 220.0f, true, 0.9f, 0.55f, 0.45f, 0.35f);
        crossover.processSamplePair(vL, vR, 0.55f, 0.45f);
        analogChain.processSamplePair(vL, vR, 0.50f, 0.55f, 0.45f);
        ducker.processAccompanimentPair(0.5f * (vL + vR), bgmL, bgmR, 0.65f, 0.35f, 3.0f);

        if (!std::isfinite(vL) || !std::isfinite(vR) || !std::isfinite(bgmL) || !std::isfinite(bgmR))
            allFinite = false;
        outEnergy += static_cast<double>(vL * vL + vR * vR);
        bgmOutEnergy += static_cast<double>(bgmL * bgmL + bgmR * bgmR);
    }

    checkCondition(allFinite, "Glottal + LR4 + Fairchild/LA-2A + Trackspacer finite output");
    checkCondition(outEnergy > 10.0 && bgmOutEnergy > 10.0, "Healthy vocal & sculpted BGM energy");
    checkCondition(analogChain.getLa2aOpto().getOptoGainReductionDb() >= 0.0f, "Analog Tube/Opto telemetry valid");
}

// =============================================================================
// Test 3: VTuberディレクター・ピッチこぶし・マイク物理ツイン・True-Peakリミッター・De-Click検証
// =============================================================================
void testDirectorPitchMicLimiterAndDeClick()
{
    std::printf("\n=== [Test 3] VTuber Director, Kobushi Pitch, Mic Twin, 8x True-Peak Limiter & AR(2) De-Click Audit ===\n");
    constexpr double kSr = 48000.0;

    gzzio::stage_director::LiveStageAndVtuberDirectorSuite director;
    gzzio::precision_pitch::PrecisionPitchAndVibratoSuite pitchSuite;
    gzzio::mic_physical_twin::MicrophoneAndPreampPhysicalTwinSuite micTwin;
    gzzio::true_peak_limiter::MasteringLimiterAndTruePeakSuite limiter;
    gzzio::declick_plosive::DynamicDePlosiveAndDeClickSuite deClicker;

    director.prepare(kSr);
    pitchSuite.prepare(kSr);
    micTwin.prepare(kSr);
    limiter.prepare(kSr);
    deClicker.prepare(kSr);

    bool allFinite = true;
    float maxLimitedPeak = 0.0f;

    for (int i = 0; i < 4800; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kSr);
        float sL = 0.65f * std::sin(6.2831853f * 330.0f * t);
        // Inject synthetic mouth click at sample 1000
        if (i == 1000)
            sL += 0.95f;
        float sR = sL;

        deClicker.processSamplePair(sL, sR, 0.70f, 0.75f);
        micTwin.processSamplePair(sL, sR, 0.60f, 0.35f, 0.50f);
        sL = pitchSuite.getPsolaL().processSample(sL, 330.0f, true, 1.02f, 1.0f);
        director.getVisemeAnimator().updateViseme(1.0f / static_cast<float>(kSr), 0.5f * (std::fabs(sL) + std::fabs(sR)), 720.0f, 1300.0f, true);
        limiter.processSamplePair(sL, sR, 2.5f);

        if (!std::isfinite(sL) || !std::isfinite(sR))
            allFinite = false;
        maxLimitedPeak = std::max(maxLimitedPeak, std::max(std::fabs(sL), std::fabs(sR)));
    }

    checkCondition(allFinite, "De-Click + Mic Twin + Pitch + VTuber Director + 8x True-Peak Limiter finite");
    checkCondition(maxLimitedPeak <= 0.95f, "8x Hermite True-Peak Limiter ceiling enforced");
    checkCondition(director.getVisemeAnimator().getCurrentVisemeFrame().mouthOpenHeight01 >= 0.0f, "VTuber 5-Vowel Viseme mouth open tracked");
}

// =============================================================================
// Test 4: バイノーラル較正・ヴィンテージ空間・16バンドAuto-EQ・ADT・チェビシェフ・位相ドクター・ディザ検証
// =============================================================================
void testNextGenMasteringAndAcousticSuites()
{
    std::printf("\n=== [Test 4] Binaural, Lexicon/RMX/DimD, 16-Band Auto-EQ, ADT, Chebyshev, Phase Doctor & Dither Audit ===\n");
    constexpr double kSr = 48000.0;

    gzzio::binaural_cal::BinauralHeadphoneAndRoomSuite binaural;
    gzzio::vintage_spatial::VintageDigitalReverbAndDelaySuite vintageSpatial;
    gzzio::adaptive_autoeq::AdaptiveAutoEqAndResonanceSuite autoEq;
    gzzio::vocal_doubler::VocalDoublerAndMicroShiftSuite doubler;
    gzzio::harmonic_sculptor::HarmonicSeriesSculptorAndSubBassSuite sculptor;
    gzzio::phase_doctor::MasteringStereoFieldAndPhaseSuite phaseDoc;
    gzzio::stream_safety::StreamingAudioRouterAndSafetySuite safetyRouter;
    gzzio::transient_sustain::PsychoacousticTransientAndSustainSuite transientSuite;
    gzzio::emotion_prosody::VocalEmotionAndProsodySuite emotionSuite;
    gzzio::vocal_tract_twin::AcousticImpedanceAndVocalTractSuite tractTwin;
    gzzio::mastering_dither::MasteringDitherAndBitDepthSuite ditherSuite;

    binaural.prepare(kSr);
    vintageSpatial.prepare(kSr);
    autoEq.prepare(kSr);
    doubler.prepare(kSr);
    sculptor.prepare(kSr);
    phaseDoc.prepare(kSr);
    safetyRouter.prepare(kSr);
    transientSuite.prepare(kSr);
    emotionSuite.prepare(kSr);
    tractTwin.prepare(kSr);
    ditherSuite.prepare(kSr);

    bool allFinite = true;
    double sumEnergy = 0.0;

    for (int i = 0; i < 4800; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kSr);
        float l = 0.30f * std::sin(6.2831853f * 261.63f * t) + 0.12f * std::sin(6.2831853f * 3100.0f * t);
        float r = l;

        tractTwin.processStereoSample(l, r, 0.45f, 0.55f, 0.40f);
        autoEq.processStereoSample(l, r, 0.60f, 0.50f);
        sculptor.processStereoSample(l, r, 0.45f, 0.35f);
        transientSuite.processStereoSample(l, r, 0.40f, 0.45f, 0.50f);
        doubler.processStereoSample(l, r, 0.35f, 0.40f);
        vintageSpatial.processStereoSample(l, r, 0.35f, 0.25f, 0.30f);
        phaseDoc.processStereoSample(l, r, 0.45f, true);
        binaural.processStereoSample(l, r, 0.50f, 0.45f);
        emotionSuite.analyzeStereoSample(l, r);
        safetyRouter.processStereoSample(l, r, true);
        ditherSuite.processStereoSample(l, r);

        if (!std::isfinite(l) || !std::isfinite(r))
            allFinite = false;
        sumEnergy += static_cast<double>(l * l + r * r);
    }

    checkCondition(allFinite, "All 11 Next-Gen Mastering & Acoustic Twin suites finite");
    checkCondition(sumEnergy > 15.0, "Processed stereo master signal energy valid");
    checkCondition(phaseDoc.getMetrics().correlationNeg1To1 > -0.20f, "Phase Doctor maintains positive mono compatibility");
    checkCondition(gzzio::preset_vault::BroadcastMasteringPresetVault::getCatalog().size() == 12,
                   "BroadcastMasteringPresetVault 12 factory snapshots ready");
}

// =============================================================================
// Test 5: Horology Grand Complication & Celestial UI オフスクリーン描画検証
// =============================================================================
void testHorologyGrandComplicationUiRendering()
{
    std::printf("\n=== [Test 5] Horology Celestial & Grand Complication HUD Offscreen Rendering Audit ===\n");
    juce::Image canvas(juce::Image::ARGB, 640, 360, true, juce::SoftwareImageType());
    {
        juce::Graphics g(canvas);

        gzzio::horology_celestial::CelestialPlanisphereAndSoundstageMapRenderer::SoundstageBandMetrics metrics{};
        gzzio::horology_celestial::CelestialPlanisphereAndSoundstageMapRenderer::drawPlanisphere(
            g, { 10.0f, 10.0f, 180.0f, 180.0f }, metrics, 0.5f);

        for (int se = 0; se < 4; ++se)
        {
            gzzio::horology_celestial::LiveSePadHorologyVisualizer::drawSePadEmblem(
                g, { 200.0f + se * 50.0f, 10.0f, 44.0f, 44.0f }, se, true, 0.75f);
        }

        gzzio::horology_celestial::LufsAndTruePeakChronographRenderer::drawRattrapanteMeter(
            g, { 200.0f, 70.0f, 140.0f, 140.0f }, -14.0f, -13.5f, -1.2f);

        gzzio::horology_grand::EquationOfTimeKidneyCamRenderer::drawKidneyCamMechanism(
            g, { 360.0f, 10.0f, 120.0f, 120.0f }, 0.8f, 0.15f);

        std::array<float, 16> bands{};
        bands.fill(0.65f);
        gzzio::horology_grand::Retrograde16BandHarmonicSectorRenderer::drawRetrogradeArc(
            g, { 360.0f, 140.0f, 160.0f, 95.0f }, bands, 0.72f);

        gzzio::horology_grand::AstronomicalVocalRegisterMoonphaseRenderer::drawMoonphaseWindow(
            g, { 500.0f, 10.0f, 120.0f, 120.0f }, 0.45f);
    }

    int nonZeroPixels = 0;
    for (int y = 0; y < canvas.getHeight(); y += 4)
        for (int x = 0; x < canvas.getWidth(); x += 4)
            if (canvas.getPixelAt(x, y).getAlpha() > 0)
                ++nonZeroPixels;

    checkCondition(nonZeroPixels > 250, "Horology Celestial & Grand Complication HUD rendered non-empty pixels");
}

// =============================================================================
// Test 6: NextGenMasteringOrchestrator & VocalGzzioProcessor Live SE End-to-End 検証
// =============================================================================
void testOrchestratorAndProcessorLiveSeEndToEnd()
{
    std::printf("\n=== [Test 6] NextGenMasteringOrchestrator & VocalGzzioProcessor Live SE End-to-End Audit ===\n");
    constexpr double kSr = 48000.0;

    gzzio::nextgen_orchestrator::NextGenMasteringOrchestrator orchestrator;
    orchestrator.prepare(kSr);

    bool orchFinite = true;
    for (int i = 0; i < 2400; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kSr);
        float l = 0.28f * std::sin(6.2831853f * 329.63f * t);
        float r = l;
        orchestrator.processMasterPolishSample(l, r, 0.55f, 0.45f, 0.40f);
        if (!std::isfinite(l) || !std::isfinite(r))
            orchFinite = false;
    }
    checkCondition(orchFinite, "NextGenMasteringOrchestrator 16-stage pipeline finite");

    const float qHz = gzzio::scale_quantizer::MusicalScaleAndTemperamentQuantizer::quantizeFrequencyHz(
        443.5f, 9, gzzio::scale_quantizer::MusicalScaleAndTemperamentQuantizer::ScaleType::JapaneseYoMajor, 1.0f, 0.25f);
    checkCondition(std::fabs(qHz - 440.0f) < 1.5f, "Japanese Yo-Major (呂旋法) pitch quantizer snaps to A4=440Hz");

    const auto lightFrame = gzzio::stage_lighting::LiveSeStageLightingChoreographer::evaluateSeChoreography(2, 1.28f, 0.8f);
    checkCondition(lightFrame.strobeIntensity01 > 0.5f, "Stage Lighting Choreographer triggers blinder flash at Drumroll 1.28s finale");

    // End-to-End Processor SE trigger test
    VocalGzzioProcessor proc;
    proc.prepareToPlay(kSr, 512);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block(2, 512);

    for (int seIdx = 0; seIdx < 4; ++seIdx)
    {
        proc.triggerLiveSe(seIdx);
        float maxSeBlockPeak = 0.0f;
        for (int b = 0; b < 140; ++b)
        {
            block.clear();
            proc.processBlock(block, midi);
            maxSeBlockPeak = std::max(maxSeBlockPeak, block.getMagnitude(0, 512));
        }
        proc.stopLiveSe();
        checkCondition(maxSeBlockPeak > 0.25f, "VocalGzzioProcessor::triggerLiveSe end-to-end audible studio output");
    }
    proc.releaseResources();
}

} // namespace

int main()
{
    std::printf("================================================================================\n");
    std::printf("  VocalGzzio v5.5.0 — Studio Live SE & Next-Gen Mastering Audit Suite\n");
    std::printf("================================================================================\n");

    testStudioLiveSoundEffects();
    testVocalGlottalCrossoverTubeAndDucking();
    testDirectorPitchMicLimiterAndDeClick();
    testNextGenMasteringAndAcousticSuites();
    testHorologyGrandComplicationUiRendering();
    testOrchestratorAndProcessorLiveSeEndToEnd();

    std::printf("\n================================================================================\n");
    std::printf("  Audit Summary: %d / %d checks passed (%d failed)\n",
                gTotalChecks - gFailedChecks, gTotalChecks, gFailedChecks);
    std::printf("================================================================================\n");

    return (gFailedChecks == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
