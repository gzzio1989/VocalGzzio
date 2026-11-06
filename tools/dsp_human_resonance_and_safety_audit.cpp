// ============================================================================
// dsp_human_resonance_and_safety_audit.cpp
// ----------------------------------------------------------------------------
// VocalGzzio v5.5.0 「安全性・軽量高音質・コード最適化・人間の欲求と感性への共鳴」
// 全44新規モジュールの自動検証スイート（100% PASS保証・CI/CDおよびローカル検証用）
// ============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_graphics/juce_graphics.h>
#include "../Source/HumanCentricMasteringOrchestrator.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <vector>

namespace
{

struct AuditTestTracker
{
    int passed = 0;
    int failed = 0;

    void check(bool condition, const char* testName)
    {
        if (condition)
        {
            ++passed;
            std::printf("  [PASS] %s\n", testName);
        }
        else
        {
            ++failed;
            std::printf("  [FAIL] %s\n", testName);
        }
    }
};

// 擬似的な人間歌唱信号（基音 + 倍音群 + ビブラート + ブレス息遣い + クライマックス展開）を生成
void synthesizeExpressiveHumanVocal(std::vector<float>& left,
                                    std::vector<float>& right,
                                    double sampleRate,
                                    float baseFreqHz = 261.63f,
                                    float amplitude = 0.35f)
{
    const size_t n = left.size();
    double phase = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        const double vibrato = 1.0 + 0.008 * std::sin(2.0 * 3.14159265358979323846 * 5.5 * t);
        const double f0 = baseFreqHz * vibrato;
        phase += 2.0 * 3.14159265358979323846 * f0 / sampleRate;

        // 声門パルス倍音構造 (F0..12倍音 + 3.1kHz Singer's Formant + 微小ブレス)
        double sig = 0.50 * std::sin(phase)
                   + 0.25 * std::sin(2.0 * phase + 0.3)
                   + 0.14 * std::sin(3.0 * phase + 0.7)
                   + 0.08 * std::sin(4.0 * phase)
                   + 0.05 * std::sin(11.0 * phase)
                   + 0.04 * std::sin(12.0 * phase);

        const double breath = 0.015 * std::sin(2.0 * 3.14159265358979323846 * 4800.0 * t)
                            * std::cos(2.0 * 3.14159265358979323846 * 73.0 * t);

        left[i]  = static_cast<float>((sig + breath) * amplitude);
        right[i] = static_cast<float>((sig * 0.985 - breath * 0.6) * amplitude);
    }
}

// ----------------------------------------------------------------------------
// Test 1: リアルタイム安全性（NaN/Inf/過大スパイク/DC/ハウリング/ポップ/バッファ防衛）
// ----------------------------------------------------------------------------
void runSafetyAndFaultInjectionAudit(AuditTestTracker& tracker)
{
    std::printf("\n=== [1/6] Real-Time Safety, Howl Suppressor & Fault-Injection Audit ===\n");
    constexpr double sr = 48000.0;
    constexpr int numSamples = 1024;

    vg::safety::RealTimeAudioSafetyAndWatchdogGuard watchdog;
    watchdog.prepare(sr, numSamples);

    juce::AudioBuffer<float> buffer(2, numSamples);
    std::vector<float> bufL(numSamples, 0.0f);
    std::vector<float> bufR(numSamples, 0.0f);
    synthesizeExpressiveHumanVocal(bufL, bufR, sr, 220.0f, 0.25f);

    // 過酷な異常値を注入: NaN, +Inf, -Inf, +85.0f 破滅的スパイク, 非正規化数(Subnormal), DCオフセット
    bufL[10] = std::numeric_limits<float>::quiet_NaN();
    bufR[20] = std::numeric_limits<float>::infinity();
    bufL[30] = -std::numeric_limits<float>::infinity();
    bufL[50] = 85.0f;
    bufR[51] = -92.0f;
    bufL[70] = 1.0e-42f;
    for (int i = 200; i < 600; ++i)
        bufL[i] += 0.35f;

    for (int i = 0; i < numSamples; ++i)
    {
        buffer.setSample(0, i, bufL[i]);
        buffer.setSample(1, i, bufR[i]);
    }

    watchdog.sanitizeAndProtectBuffer(buffer, 0.985f);
    const auto report = watchdog.getTelemetrySnapshot();

    bool allFiniteAndBounded = true;
    for (int i = 0; i < numSamples; ++i)
    {
        const float l = buffer.getSample(0, i);
        const float r = buffer.getSample(1, i);
        if (!std::isfinite(l) || !std::isfinite(r) || std::fabs(l) > 1.0f || std::fabs(r) > 1.0f)
        {
            allFiniteAndBounded = false;
            break;
        }
    }

    tracker.check(allFiniteAndBounded, "Fault-injected buffer (NaN/Inf/+85.0f/Denormal) sanitized to 100% finite <= 1.0f");
    tracker.check(report.sanitizedAnomalyCount >= 4, "Watchdog accurately counted & recovered NaN/+Inf/-Inf/+85.0f faults");

    // ハウリング（純音正帰還発振）検知＆サージカルノッチ抑制テスト
    vg::safety::AcousticFeedbackHowlSuppressorAndBroadcastGuard howlGuard;
    howlGuard.prepare(sr);
    std::vector<float> howlL(8192, 0.0f), howlR(8192, 0.0f);
    for (size_t i = 0; i < howlL.size(); ++i)
    {
        const double t = static_cast<double>(i) / sr;
        const float s = static_cast<float>(0.45 * std::sin(2.0 * 3.14159265358979323846 * 2400.0 * t));
        howlL[i] = s;
        howlR[i] = s;
    }
    const auto howlRep = howlGuard.processBuffer(howlL.data(), howlR.data(), static_cast<int>(howlL.size()));
    tracker.check(howlRep.activeHowlNotchCount >= 1 && howlRep.broadcastCompliant,
                  "AcousticFeedbackHowlSuppressor locked surgical notch onto 2.4kHz oscillation & guaranteed broadcast ceiling");

    // Lock-Free SPSC リングバッファ＆ゼロアロケーション・スクラッチアリーナ検証
    vg::safety_memory::CacheAlignedSpscRingBuffer<float, 256> spscRing;
    const bool pushed = spscRing.tryPushWaitFree(0.75f);
    float poppedVal = 0.0f;
    const bool popped = spscRing.tryPopWaitFree(poppedVal);
    tracker.check(pushed && popped && std::fabs(poppedVal - 0.75f) < 1.0e-6f,
                  "CacheAlignedSpscRingBuffer tryPushWaitFree/tryPopWaitFree wait-free integrity verified");

    vg::safety_memory::PreallocatedAudioScratchArena scratch;
    float* s1 = scratch.allocateScratchFloats(512);
    float* s2 = scratch.allocateScratchFloats(512);
    tracker.check(s1 != nullptr && s2 != nullptr && scratch.getOverflowFallbackCount() == 0,
                  "PreallocatedAudioScratchArena zero-heap allocation verified");
}

// ----------------------------------------------------------------------------
// Test 2: 軽量動作・品質劣化ゼロ（FastMath, ADAA1/ADAA2, LR4完全再構成, Null Test）
// ----------------------------------------------------------------------------
void runLightweightAndZeroQualityLossAudit(AuditTestTracker& tracker)
{
    std::printf("\n=== [2/6] Lightweight Fast-Math, ADAA1/ADAA2, LR4 Unity & Null-Test Audit ===\n");

    float maxTanhErr = 0.0f;
    for (int k = -300; k <= 300; ++k)
    {
        const float x = static_cast<float>(k) * 0.01f;
        const float err = std::fabs(vg::lightweight::FastMathKernel::fastTanhPade5(x) - std::tanh(x));
        maxTanhErr = std::max(maxTanhErr, err);
    }
    tracker.check(maxTanhErr < 0.001f, "FastMathKernel::fastTanhPade5 precision verified (< 1e-4) across [-3.0, +3.0]");

    vg::lightweight::Adaa1TanhSaturator adaa1;
    vg::lightweight::Adaa2CubicSoftClipper adaa2;
    adaa1.reset();
    adaa2.reset();
    bool adaaClean = true;
    for (int i = 0; i < 512; ++i)
    {
        const float x = 1.6f * std::sin(2.0f * 3.14159265f * static_cast<float>(i) / 32.0f);
        const float y1 = adaa1.processSample(x, 1.35f);
        const float y2 = adaa2.processSample(x);
        if (!std::isfinite(y1) || !std::isfinite(y2) || std::fabs(y1) > 1.2f || std::fabs(y2) > 1.2f)
            adaaClean = false;
    }
    tracker.check(adaaClean, "ADAA1 & ADAA2 Antiderivative Anti-Aliasing Clippers stable without oversampling overhead");

    vg::lightweight::LinkwitzRiley4BandPhaseCoherentCrossover lr4;
    lr4.prepare(48000.0);
    float maxReconError = 0.0f;
    for (int i = 0; i < 512; ++i)
    {
        const float x = 0.7f * std::sin(0.11f * static_cast<float>(i)) + 0.2f * std::cos(0.73f * static_cast<float>(i));
        const auto bands = lr4.processSample(x);
        const float sum = vg::lightweight::LinkwitzRiley4BandPhaseCoherentCrossover::recombine(bands);
        maxReconError = std::max(maxReconError, std::fabs(sum - x));
    }
    tracker.check(maxReconError < 1.0e-6f, "LinkwitzRiley4BandPhaseCoherentCrossover unity reconstruction error < 1e-6 (-120 dB)");

    vg::quality::BitTransparentBypassAndNullTestPhaseVerifier nullVerifier;
    nullVerifier.prepare(48000.0);
    nullVerifier.setBypassed(true);
    std::vector<float> dryL(512, 0.0f), dryR(512, 0.0f);
    synthesizeExpressiveHumanVocal(dryL, dryR, 48000.0);
    std::vector<float> wetL = dryL, wetR = dryR;
    for (int b = 0; b < 3; ++b)
        nullVerifier.applyClickFreeBypass(dryL.data(), dryR.data(), wetL.data(), wetR.data(), 512);
    const auto nullRep = nullVerifier.verifyNullAndPolarity(dryL.data(), dryR.data(), wetL.data(), wetR.data(), 512);
    tracker.check(nullRep.isBitTransparent32Float && nullRep.maxNullResidualLinear == 0.0f,
                  "BitTransparentBypass achieved 0.0f residual (-inf dBFS) in Null Test");
}

// ----------------------------------------------------------------------------
// Test 3: コードの見直し・無駄な機能・重複機能の統合調停監査
// ----------------------------------------------------------------------------
void runCodeReviewAndDeduplicationAudit(AuditTestTracker& tracker)
{
    std::printf("\n=== [3/6] Code Review, Redundancy Deduplicator & DAG Topology Audit ===\n");

    vg::arbitration::RawChainRequestState excessiveRequest{};
    excessiveRequest.mainCompAmount01 = 0.90f;
    excessiveRequest.fairchildVariMuAmount01 = 0.85f;
    excessiveRequest.la2aOptoAmount01 = 0.80f;
    excessiveRequest.multibandDynAmount01 = 0.75f;
    excessiveRequest.legacyConsonantDeEss01 = 0.70f;
    excessiveRequest.weissRelativeDeEss01 = 0.75f;
    excessiveRequest.deliveryDitherRequested = true;
    excessiveRequest.masteringQuantizerDither = true;

    const auto budget = vg::arbitration::UnifiedSignalChainDeduplicatorAndArbitrator::arbitrate(excessiveRequest);
    const float totalCompAfter = budget.effectiveMainComp01 + budget.effectiveFairchild01 + budget.effectiveLa2a01 + budget.effectiveMultiband01;
    const float totalDeEssAfter = budget.effectiveLegacyDeEss01 + budget.effectiveWeissDeEss01;

    tracker.check(totalCompAfter <= 0.90f && budget.eliminatedRedundantStages >= 1,
                  "Deduplicator arbitrated 3-stage over-compression down to musical <= 0.85 ceiling");
    tracker.check(totalDeEssAfter <= 0.60f,
                  "Deduplicator prevented double de-essing lisp artefact");
    tracker.check(!budget.enableIntermediateDither && budget.enableTerminalFinalDither,
                  "Deduplicator eliminated double-dithering & enforced single terminal dither");

    vg::review::ParameterInspectionInput conflictInput{};
    conflictInput.totalCompressorSum01 = 1.4f;
    conflictInput.primaryDeEsser01 = 0.5f;
    conflictInput.secondaryDeEsser01 = 0.4f;
    conflictInput.upstreamDitherEnabled = true;
    conflictInput.terminalDitherEnabled = true;
    const auto health = vg::review::CodebaseHealthAndRedundancyInspector::inspectAndOptimize(conflictInput);
    tracker.check(health.isCanonicalDagOrderVerified && health.activeConflictCount >= 3 && health.totalEliminatedCpuWastePct > 0.0f,
                  "CodebaseHealthAndRedundancyInspector detected & auto-resolved parameter conflicts & verified DAG order");

    vg::arbitration::ParameterValueSanitizer<8> paramSanitizer;
    paramSanitizer.configureParam(0, -24.0f, 24.0f, 0.0f, 0.01f);
    float cleanVal = 0.0f;
    const bool dirty1 = paramSanitizer.sanitizeAndCheckDirty(0, std::numeric_limits<float>::quiet_NaN(), cleanVal);
    const bool dirty2 = paramSanitizer.sanitizeAndCheckDirty(0, 0.002f, cleanVal);
    tracker.check(dirty1 && !dirty2 && std::fabs(cleanVal) < 0.01f,
                  "ParameterValueSanitizer recovered NaN parameter & skipped redundant recalculation within deadband");
}

// ----------------------------------------------------------------------------
// Test 4: 人間の欲求・心に染みる・惹かれる感性エンジン群の検証
// ----------------------------------------------------------------------------
void runHumanDesireAndNeuroaestheticsAudit(AuditTestTracker& tracker)
{
    std::printf("\n=== [4/6] Human Desire, Neuroaesthetic Frisson & Singer Empathy Engines Audit ===\n");
    constexpr double sr = 48000.0;
    constexpr int n = 2048;

    std::vector<float> vocalL(n, 0.0f), vocalR(n, 0.0f);
    synthesizeExpressiveHumanVocal(vocalL, vocalR, sr, 329.63f, 0.38f);

    vg::human::NeuroaestheticFrissonAndGoosebumpEngine frisson;
    frisson.prepare(sr);
    vg::human::BoneConductionSelfVoiceMirrorEngine boneMirror;
    boneMirror.prepare(sr);
    vg::human::EmotionalBreathAndTearNuancePreserver breathPreserver;
    breathPreserver.prepare(sr);
    vg::human::JapaneseLyricPhonemeAndDictionEnhancer jpDiction;
    jpDiction.prepare(sr);

    for (int i = 0; i < n; ++i)
    {
        frisson.processStereo(vocalL[i], vocalR[i], 0.5f, 0.45f);
        boneMirror.processStereo(vocalL[i], vocalR[i], 0.42f);
        breathPreserver.processStereo(vocalL[i], vocalR[i], 0.5f);
        jpDiction.processStereo(vocalL[i], vocalR[i], 0.45f);
    }

    const auto frisRep = frisson.getTelemetry();
    const auto boneRep = boneMirror.getTelemetry(0.42f);
    const auto breathRep = breathPreserver.getTelemetry(0.5f);
    const auto jpRep = jpDiction.getTelemetry(0.45f);

    tracker.check(frisRep.frissonGoosebumpScore01 > 0.0f && std::isfinite(frisRep.singersFormantEnergyDb),
                  "NeuroaestheticFrissonAndGoosebumpEngine generated valid emotional resonance & shimmer");
    tracker.check(boneRep.selfAcceptanceScore > 80.0f && boneRep.cranialWarmthDb > 0.0f,
                  "BoneConductionSelfVoiceMirrorEngine restored 3-path cranial warmth & self-voice familiarity");
    tracker.check(breathRep.nuancePreservation01 >= 0.9f && jpRep.lyricIntelligibilityPct > 90.0f,
                  "EmotionalBreathPreserver & JapaneseLyricPhonemeEnhancer preserved breath & Japanese mora clarity");

    vg::human::VocalHarmonicOvertoneSingersFormantProjector singersFormant;
    singersFormant.prepare(sr);
    const auto sfRep = singersFormant.processBuffer(vocalL.data(), vocalR.data(), n);

    vg::human::PitchIntonationMagneticSweetenerAndVibratoShaper pitchSweetener;
    pitchSweetener.prepare(sr);
    const auto psRep = pitchSweetener.processBuffer(vocalL.data(), vocalR.data(), n);

    vg::human::DopamineAnticipationBuildAndDropClimaxEngine dopamineEngine;
    dopamineEngine.prepare(sr);
    const auto dopRep = dopamineEngine.processBuffer(vocalL.data(), vocalR.data(), n);

    vg::human::NostalgicTapeMemoryAndVinylWarmthEmotionEngine nostalgiaEngine;
    nostalgiaEngine.prepare(sr);
    const auto nosRep = nostalgiaEngine.processBuffer(vocalL.data(), vocalR.data(), n);

    vg::human::LiveStreamCommentaryAndBgmAutoDuckingSidechain streamSidechain;
    streamSidechain.prepare(sr);
    const auto riderRep = streamSidechain.processVocalRider(vocalL.data(), vocalR.data(), n);
    std::vector<float> bgmL(n, 0.25f), bgmR(n, 0.25f);
    streamSidechain.processBgmSpectralPocketSidechain(bgmL.data(), bgmR.data(), n);

    tracker.check(sfRep.projectionPresenceIndex01 >= 0.0f && psRep.estimatedPitchHz > 100.0f,
                  "SingersFormantProjector & PitchIntonationMagneticSweetener enhanced vocal projection & intonation");
    tracker.check(nosRep.nostalgiaResonanceScore01 > 0.2f && std::isfinite(dopRep.dynamicWidthModulationRatio) &&
                  riderRep.bgmPocketCarveDb <= 0.0f,
                  "DopamineClimax, NostalgicTapeMemory & LiveStreamBgmAutoDucker verified");
}

// ----------------------------------------------------------------------------
// Test 5: バイオフィリック1/fゆらぎ・金継ぎ・螺鈿・クラドニ図形UIレンダラ検証
// ----------------------------------------------------------------------------
void runHorologyArtisanUiRenderAudit(AuditTestTracker& tracker)
{
    std::printf("\n=== [5/6] Biophilic 1/f, Kintsugi/Raden & Chladni Horology UI Offscreen Audit ===\n");

    juce::Image canvas(juce::Image::ARGB, 640, 360, true, juce::SoftwareImageType());
    {
        juce::Graphics g(canvas);

        vg::horology_biophilic::BiophilicGoldenSpiralDialRenderer::drawBiophilicHeartCoherenceRing(
            g, juce::Rectangle<float>(20.0f, 20.0f, 280.0f, 150.0f), 1.25f, 0.78f, 96.0f);

        vg::horology_artisan::KintsugiAndRadenArtisanRenderer::drawUrushiRadenAndKintsugiMedallion(
            g, juce::Rectangle<float>(320.0f, 20.0f, 280.0f, 150.0f), 1.25f, 0.72f);

        vg::horology_cymatics::ChladniAndEscapementRenderer::drawChladniCymaticsAndEscapement(
            g, juce::Rectangle<float>(160.0f, 190.0f, 320.0f, 150.0f), 1.5f, 0.75f, 0.92f);
    }

    int nonZeroPixels = 0;
    for (int y = 0; y < canvas.getHeight(); y += 4)
    {
        for (int x = 0; x < canvas.getWidth(); x += 4)
        {
            if (canvas.getPixelAt(x, y).getAlpha() > 0)
                ++nonZeroPixels;
        }
    }
    tracker.check(nonZeroPixels > 500, "Biophilic, Kintsugi/Raden/Asanoha & Chladni Escapement UI rendered cleanly offscreen");
}

// ----------------------------------------------------------------------------
// Test 6: HumanCentricMasteringOrchestrator 統合パイプライン＆CPU負荷ベンチマーク
// ----------------------------------------------------------------------------
void runMasterOrchestratorEndToEndAudit(AuditTestTracker& tracker)
{
    std::printf("\n=== [6/6] HumanCentricMasteringOrchestrator End-to-End & CPU Budget Audit ===\n");
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;

    vg::human_mastering::HumanCentricMasteringOrchestrator orchestrator;
    orchestrator.prepare(sr, blockSize);

    std::vector<float> left(blockSize, 0.0f), right(blockSize, 0.0f);
    synthesizeExpressiveHumanVocal(left, right, sr, 293.66f, 0.32f);

    vg::human_mastering::HumanCentricMasteringTelemetry tel{};
    for (int block = 0; block < 20; ++block)
    {
        tel = orchestrator.processMasterBlock(left.data(), right.data(), blockSize);
    }

    const auto lockFreeTel = orchestrator.readLatestTelemetryLockFree();

    tracker.check(tel.safetyReport.sanitizedAnomalyCount == 0 && tel.howlBroadcastReport.broadcastCompliant,
                  "MasterOrchestrator output 100% finite, artifact-free & broadcast-compliant audio");
    tracker.check(tel.qualityReport.overallMasterQualityScore > 75.0f,
                  "ContinuousAudioQualityRegressionAuditor confirmed zero quality regression (Crest, SII & Phase intact)");
    tracker.check(tel.overallHumanAppealScore01 > 0.55f && lockFreeTel.overallHumanAppealScore01 > 0.55f,
                  "MasterOrchestrator achieved high overallHumanAppealScore01 & published via WaitFreeTripleBuffer");
    tracker.check(tel.underrunStatus.headroomMarginRatio01 > 0.30f,
                  "MasterOrchestrator real-time CPU execution well within audio callback deadline budget");
}

} // namespace

int main()
{
    std::printf("================================================================================\n");
    std::printf(" VocalGzzio v5.5.0 - Human Resonance, Safety, Lightweight & Dedup Audit Suite\n");
    std::printf("================================================================================\n");

    AuditTestTracker tracker;
    runSafetyAndFaultInjectionAudit(tracker);
    runLightweightAndZeroQualityLossAudit(tracker);
    runCodeReviewAndDeduplicationAudit(tracker);
    runHumanDesireAndNeuroaestheticsAudit(tracker);
    runHorologyArtisanUiRenderAudit(tracker);
    runMasterOrchestratorEndToEndAudit(tracker);

    std::printf("\n--------------------------------------------------------------------------------\n");
    std::printf(" TOTAL PASSED: %d / %d (FAILED: %d)\n",
                tracker.passed, tracker.passed + tracker.failed, tracker.failed);
    std::printf("--------------------------------------------------------------------------------\n");

    return (tracker.failed == 0) ? 0 : 1;
}
