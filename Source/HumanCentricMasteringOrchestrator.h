#pragma once

// ============================================================================
// HumanCentricMasteringOrchestrator.h
// ----------------------------------------------------------------------------
// VocalGzzio v5.5.0 「安全性・軽量高音質・コード最適化・人間の欲求と感性への共鳴」
// 全43モジュールを統括するマスター・オーケストレーター・ヘッダ。
// ============================================================================

#include "RealTimeAudioSafetyAndWatchdogGuard.h"
#include "LockFreeSpscRingAndZeroAllocMemoryPool.h"
#include "AcousticFeedbackHowlSuppressorAndBroadcastGuard.h"
#include "RealtimeCpuThermalAndBufferUnderrunGuard.h"
#include "LightweightSimdFastMathAndEcoGovernor.h"
#include "ContinuousAudioQualityRegressionAuditor.h"
#include "ZeroPhaseLatencyCompensatedCrossoverAndLinearFilter.h"
#include "BitTransparentBypassAndNullTestPhaseVerifier.h"
#include "UnifiedSignalChainDeduplicatorAndArbitrator.h"
#include "CodebaseHealthAndRedundancyInspector.h"
#include "ParameterStateSanitizerAndPresetMigrationGuard.h"
#include "NeuroaestheticFrissonAndGoosebumpEngine.h"
#include "BoneConductionSelfVoiceMirrorEngine.h"
#include "EmotionalBreathAndTearNuancePreserver.h"
#include "ListenerEmpathyAndIntimacyProximityEngine.h"
#include "JapaneseLyricPhonemeAndDictionEnhancer.h"
#include "ChorusClimaxAndArrangementLiftEngine.h"
#include "SingerConfidenceAndFlowStateCoach.h"
#include "PsychoacousticGoldenRatioHarmonicWeaver.h"
#include "ZeroLatencyCueMixAndHeadphoneBlissEngine.h"
#include "AcousticFatigueFreeListeningGuard.h"
#include "MicroTimingGrooveAndPocketHumanizer.h"
#include "AnalogConsoleCrosstalkAndTransformerSoul.h"
#include "IntelligentGainStagingAndLoudnessNormalizer.h"
#include "AdaptiveBreathGateAndSilenceVelvetEngine.h"
#include "SweetSpotAutoFinderAndOneTouchMagic.h"
#include "HarmonicSubOctaveAndAirSilkSculptor.h"
#include "EmotionalStorytellingAndLufsArcAnalyzer.h"
#include "PerceptualLoudnessEqualizerAndFletcherMunson.h"
#include "RoomModeAntinodeAndDeskReflectionCanceller.h"
#include "HarmonicFormantVowelMorphAndSingabilityEngine.h"
#include "BinauralAutonomousSensoryMeridianWhisperEngine.h"
#include "MasteringStemPhaseAlignmentAndMixTranslationEngine.h"
#include "VocalHarmonicOvertoneSingersFormantProjector.h"
#include "LiveStreamCommentaryAndBgmAutoDuckingSidechain.h"
#include "NostalgicTapeMemoryAndVinylWarmthEmotionEngine.h"
#include "PitchIntonationMagneticSweetenerAndVibratoShaper.h"
#include "SpatialHeadphoneCrossfeedAndIemStageLocalizer.h"
#include "DynamicPlosiveDePopperAndMouthClickCleaner.h"
#include "DopamineAnticipationBuildAndDropClimaxEngine.h"
#include "HorologyBiophilicAndHeartCoherenceUi.h"
#include "HorologyKintsugiAndWabiSabiArtisanRenderer.h"
#include "HorologyChronometerEscapementAndAcousticPendulumUi.h"

namespace vg::human_mastering
{

struct HumanCentricMasteringTelemetry
{
    vg::safety::SafetyTelemetrySnapshot safetyReport{};
    vg::safety::HowlAndBroadcastReport howlBroadcastReport{};
    vg::safety::UnderrunGuardStatus underrunStatus{};
    vg::lightweight::EcoGovernorStatus ecoMetrics{};
    vg::quality::AudioQualityAuditReport qualityReport{};
    vg::arbitration::ArbitratedChainSchedule deduplicatedBudget{};
    vg::review::CodebaseAndChainHealthReport codebaseHealth{};
    vg::human::FrissonTelemetry frisson{};
    vg::human::SelfVoiceMirrorTelemetry boneMirror{};
    vg::human::BreathNuanceTelemetry breathNuance{};
    vg::human::IntimacyProximityTelemetry intimacy{};
    vg::human::JapaneseDictionTelemetry japaneseDiction{};
    vg::human::ChorusClimaxTelemetry chorusLift{};
    vg::human::FlowStateCoachTelemetry singerCoach{};
    vg::human::DeskAndRoomModeTelemetry roomCanceller{};
    vg::human::VowelSingabilityTelemetry belCantoSingability{};
    vg::human::SingersFormantTelemetry singersFormant{};
    vg::human::StreamRiderAndDuckingReport streamRider{};
    vg::human::NostalgicWarmthReport nostalgicWarmth{};
    vg::human::IntonationAndVibratoReport intonationSweetener{};
    vg::human::IemStageLocalizationReport iemStage{};
    vg::human::PlosiveAndClickReport plosiveDePopper{};
    vg::human::DopamineArcTelemetry dopamineArc{};
    float overallHumanAppealScore01 = 1.0f;
};

class HumanCentricMasteringOrchestrator
{
public:
    void prepare(double sampleRate, int maxBlockSize = 512) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        safetyGuard.prepare(sr, maxBlockSize);
        howlBroadcastGuard.prepare(sr);
        underrunGuard.prepare(sr, maxBlockSize);
        ecoGovernor.prepare(sr, maxBlockSize);
        qualityAuditor.prepare(sr);
        crossover4Band.prepare(sr);
        nullVerifier.prepare(sr);
        codebaseHealth = vg::review::CodebaseHealthAndRedundancyInspector::inspectAndOptimize({});
        frissonEngine.prepare(sr);
        boneMirror.prepare(sr);
        nuancePreserver.prepare(sr);
        intimacyEngine.prepare(sr);
        japaneseDiction.prepare(sr);
        chorusLift.prepare(sr);
        singerCoach.prepare(sr);
        goldenWeaver.prepare(sr);
        cueMixBliss.prepare(sr);
        fatigueGuard.prepare(sr);
        grooveHumanizer.prepare(sr);
        analogSoul.prepare(sr);
        gainNormalizer.prepare(sr);
        velvetGate.prepare(sr);
        sweetSpotFinder.prepare(sr);
        subAndAir.prepare(sr);
        storyArc.prepare(sr);
        isoLoudness.prepare(sr);
        roomCanceller.prepare(sr);
        belCantoEngine.prepare(sr);
        asmrWhisper.prepare(sr);
        mixTranslation.prepare(sr);
        singersFormant.prepare(sr);
        streamRider.prepare(sr);
        nostalgicWarmth.prepare(sr);
        intonationSweetener.prepare(sr);
        iemLocalizer.prepare(sr);
        plosiveCleaner.prepare(sr);
        dopamineClimax.prepare(sr);
        scratchPool.zeroAllMemory();
    }

    void reset() noexcept
    {
        safetyGuard.reset();
        howlBroadcastGuard.reset();
        underrunGuard.reset();
        ecoGovernor.reset();
        qualityAuditor.reset();
        crossover4Band.reset();
        nullVerifier.reset();
        frissonEngine.reset();
        boneMirror.reset();
        nuancePreserver.reset();
        intimacyEngine.reset();
        japaneseDiction.reset();
        chorusLift.reset();
        singerCoach.reset();
        goldenWeaver.reset();
        cueMixBliss.reset();
        fatigueGuard.reset();
        grooveHumanizer.reset();
        analogSoul.reset();
        gainNormalizer.reset();
        velvetGate.reset();
        sweetSpotFinder.reset();
        subAndAir.reset();
        storyArc.reset();
        isoLoudness.reset();
        roomCanceller.reset();
        belCantoEngine.reset();
        asmrWhisper.reset();
        mixTranslation.reset();
        singersFormant.reset();
        streamRider.reset();
        nostalgicWarmth.reset();
        intonationSweetener.reset();
        iemLocalizer.reset();
        plosiveCleaner.reset();
        dopamineClimax.reset();
        scratchPool.resetFrame();
    }

    // 1ブロック全体の安全性・軽量性・重複調停・人間感性エンハンス統合処理
    HumanCentricMasteringTelemetry processMasterBlock(
        float* left,
        float* right,
        int numSamples,
        const vg::arbitration::RawChainRequestState& intent = {}) noexcept
    {
        HumanCentricMasteringTelemetry telemetry{};
        if (left == nullptr || right == nullptr || numSamples <= 0 || numSamples > 2048)
            return telemetry;

        const auto tBlockStart = safetyGuard.beginBlockTiming();
        underrunGuard.beginCallbackMeasurement(numSamples);
        scratchPool.resetFrame();

        // 品質劣化ゼロ監査のために入力スナップショットをゼロヒープ・スクラッチ領域に退避
        float* dryL = scratchPool.allocateScratchFloats(static_cast<size_t>(numSamples), false);
        float* dryR = scratchPool.allocateScratchFloats(static_cast<size_t>(numSamples), false);
        if (dryL != nullptr && dryR != nullptr)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                dryL[i] = left[i];
                dryR[i] = right[i];
            }
        }

        // 1. 重複機能（3段コンプ、2重ディエッサー、多重リバーブ等）の統合調停
        telemetry.deduplicatedBudget = vg::arbitration::UnifiedSignalChainDeduplicatorAndArbitrator::arbitrate(intent);
        telemetry.codebaseHealth = codebaseHealth;

        // 2. パ行ポップ吹かれ/マウスクリック自動修復＆配信オートライダー
        telemetry.plosiveDePopper = plosiveCleaner.processBuffer(left, right, numSamples);
        telemetry.streamRider = streamRider.processVocalRider(left, right, numSamples);

        // 3. サンプル単位の感性・生理学・音響空間コヒーレンス処理
        for (int i = 0; i < numSamples; ++i)
        {
            float l = left[i];
            float r = right[i];

            roomCanceller.processStereo(l, r, 0.35f);
            boneMirror.processStereo(l, r, 0.38f);
            belCantoEngine.processStereo(l, r, 0.35f);
            nuancePreserver.processStereo(l, r, 0.45f);
            japaneseDiction.processStereo(l, r, 0.40f);
            frissonEngine.processStereo(l, r, 0.42f, 0.38f);
            chorusLift.processStereo(l, r, 0.40f);
            intimacyEngine.processStereo(l, r, 0.65f);
            singerCoach.processStereo(l, r, 0.42f);
            fatigueGuard.processStereo(l, r, 0.35f);
            isoLoudness.processStereo(l, r, 74.0f, 0.35f);
            mixTranslation.processStereo(l, r, 0.35f);

            left[i] = l;
            right[i] = r;
        }

        // 4. 歌手のフォルマント、ピッチ磁気スイートナー、ドパミン展開、ノスタルジー温もり、IEM頭外定位
        telemetry.singersFormant = singersFormant.processBuffer(left, right, numSamples);
        telemetry.intonationSweetener = intonationSweetener.processBuffer(left, right, numSamples);
        telemetry.dopamineArc = dopamineClimax.processBuffer(left, right, numSamples);
        telemetry.nostalgicWarmth = nostalgicWarmth.processBuffer(left, right, numSamples);
        telemetry.iemStage = iemLocalizer.processBuffer(left, right, numSamples);

        // 5. ハウリング抑制＆最終リアルタイム安全番犬（NaN/Inf/DC/過大スパイク・聴覚保護）
        telemetry.howlBroadcastReport = howlBroadcastGuard.processBuffer(left, right, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            safetyGuard.sanitizeAndProtectSamplePair(left[i], right[i], 0.985f);
            if (dryL != nullptr && dryR != nullptr)
                qualityAuditor.observeInputAndOutput(dryL[i], dryR[i], left[i], right[i]);
        }

        safetyGuard.endBlockTiming(tBlockStart, numSamples);
        telemetry.safetyReport = safetyGuard.getTelemetrySnapshot();
        telemetry.qualityReport = qualityAuditor.buildAuditReport();
        telemetry.ecoMetrics = ecoGovernor.updateGovernor(telemetry.safetyReport.lastBlockCpuUsageRatio01, false);
        telemetry.underrunStatus = underrunGuard.endCallbackMeasurement();

        telemetry.frisson = frissonEngine.getTelemetry();
        telemetry.boneMirror = boneMirror.getTelemetry(0.38f);
        telemetry.breathNuance = nuancePreserver.getTelemetry(0.45f);
        telemetry.intimacy = intimacyEngine.getTelemetry(0.65f);
        telemetry.japaneseDiction = japaneseDiction.getTelemetry(0.40f);
        telemetry.chorusLift = chorusLift.getTelemetry(0.40f);
        telemetry.singerCoach = singerCoach.getTelemetry(0.42f);
        telemetry.roomCanceller = roomCanceller.getTelemetry(0.35f);
        telemetry.belCantoSingability = belCantoEngine.getTelemetry(0.35f);

        telemetry.overallHumanAppealScore01 = std::clamp(
            0.25f * telemetry.frisson.frissonGoosebumpScore01 +
            0.25f * (telemetry.boneMirror.selfAcceptanceScore * 0.01f) +
            0.25f * (telemetry.singerCoach.singerFlowConfidencePct * 0.01f) +
            0.25f * (telemetry.qualityReport.overallMasterQualityScore * 0.01f),
            0.0f, 1.0f);

        lastTelemetryPtr.publishNewState(telemetry);
        return telemetry;
    }

    HumanCentricMasteringTelemetry readLatestTelemetryLockFree() noexcept
    {
        return lastTelemetryPtr.acquireLatestState();
    }

private:
    double sr = 48000.0;
    vg::safety::RealTimeAudioSafetyAndWatchdogGuard safetyGuard;
    vg::safety::AcousticFeedbackHowlSuppressorAndBroadcastGuard howlBroadcastGuard;
    vg::safety::RealtimeCpuThermalAndBufferUnderrunGuard underrunGuard;
    vg::safety_memory::PreallocatedAudioScratchArena scratchPool;
    vg::safety_memory::WaitFreeTripleBuffer<HumanCentricMasteringTelemetry> lastTelemetryPtr;
    vg::lightweight::QualityPreservingEcoGovernor ecoGovernor;
    vg::lightweight::LinkwitzRiley4BandPhaseCoherentCrossover crossover4Band;
    vg::quality::ContinuousAudioQualityRegressionAuditor qualityAuditor;
    vg::quality::BitTransparentBypassAndNullTestPhaseVerifier nullVerifier;
    vg::review::CodebaseAndChainHealthReport codebaseHealth{};
    vg::human::NeuroaestheticFrissonAndGoosebumpEngine frissonEngine;
    vg::human::BoneConductionSelfVoiceMirrorEngine boneMirror;
    vg::human::EmotionalBreathAndTearNuancePreserver nuancePreserver;
    vg::human::ListenerEmpathyAndIntimacyProximityEngine intimacyEngine;
    vg::human::JapaneseLyricPhonemeAndDictionEnhancer japaneseDiction;
    vg::human::ChorusClimaxAndArrangementLiftEngine chorusLift;
    vg::human::SingerConfidenceAndFlowStateCoach singerCoach;
    vg::human::PsychoacousticGoldenRatioHarmonicWeaver goldenWeaver;
    vg::human::ZeroLatencyCueMixAndHeadphoneBlissEngine cueMixBliss;
    vg::human::AcousticFatigueFreeListeningGuard fatigueGuard;
    vg::human::MicroTimingGrooveAndPocketHumanizer grooveHumanizer;
    vg::human::AnalogConsoleCrosstalkAndTransformerSoul analogSoul;
    vg::human::IntelligentGainStagingAndLoudnessNormalizer gainNormalizer;
    vg::human::AdaptiveBreathGateAndSilenceVelvetEngine velvetGate;
    vg::human::SweetSpotAutoFinderAndOneTouchMagic sweetSpotFinder;
    vg::human::HarmonicSubOctaveAndAirSilkSculptor subAndAir;
    vg::human::EmotionalStorytellingAndLufsArcAnalyzer storyArc;
    vg::human::PerceptualLoudnessEqualizerAndFletcherMunson isoLoudness;
    vg::human::RoomModeAntinodeAndDeskReflectionCanceller roomCanceller;
    vg::human::HarmonicFormantVowelMorphAndSingabilityEngine belCantoEngine;
    vg::human::BinauralAutonomousSensoryMeridianWhisperEngine asmrWhisper;
    vg::human::MasteringStemPhaseAlignmentAndMixTranslationEngine mixTranslation;
    vg::human::VocalHarmonicOvertoneSingersFormantProjector singersFormant;
    vg::human::LiveStreamCommentaryAndBgmAutoDuckingSidechain streamRider;
    vg::human::NostalgicTapeMemoryAndVinylWarmthEmotionEngine nostalgicWarmth;
    vg::human::PitchIntonationMagneticSweetenerAndVibratoShaper intonationSweetener;
    vg::human::SpatialHeadphoneCrossfeedAndIemStageLocalizer iemLocalizer;
    vg::human::DynamicPlosiveDePopperAndMouthClickCleaner plosiveCleaner;
    vg::human::DopamineAnticipationBuildAndDropClimaxEngine dopamineClimax;
};

} // namespace vg::human_mastering
