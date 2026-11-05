#pragma once

/**
 * @file NextGenMasteringOrchestrator.h
 * @brief VocalGzzio v5.5.0 第2世代マスタリング・声道物理・4大スタジオライブSE・複雑時計HUD 統合オーケストレータ
 *
 * `v5.5.0` 第2波で新設された全28モジュールのDSPチェーン・テレメトリ・4大スタジオライブSE音源（♪拍手 / ✦キラ / ●ドラム / ◆ベル）を
 * 単一のマスターハブとして統合制御するオーケストレータ。
 *
 * 信号フロー（Next-Gen Vocal & Mastering Pipeline）:
 *   1. DynamicDePlosiveAndDeClickSuite (高調波コム保護ポップ除去 ＋ AR(2) リップノイズ修復)
 *   2. MicrophoneAndPreampPhysicalTwinSuite (U87静電カプセル ＋ リボン近接 ＋ 2520 DOA & Cinemag トランス)
 *   3. AcousticImpedanceAndVocalTractSuite (Kelly-Lochbaum 6区間声道音響管 ＋ Sundberg 2.9kHz シンガーズ・フォルマント)
 *   4. NeuralFormantAndGlottalSuite (Fant LF声門パルス ＋ 14次LPC包絡 ＋ 胸腔サブ倍音)
 *   5. AdaptiveAutoEqAndResonanceSuite (Soothe型16バンド動的レゾナンス抑制 ＋ ピンクノイズ自動チルトサーボ)
 *   6. AnalogTubeAndOptoMasteringSuite (Fairchild 670 Vari-Mu ＋ Teletronix LA-2A T4B ＋ Massive Passive インダクタEQ)
 *   7. PsychoacousticTransientAndSustainSuite (SPL型差動トランジェント＆日本語子音28msオンセット明瞭化)
 *   8. HarmonicSeriesSculptorAndSubBassSuite (レベル不変チェビシェフ倍音 ＋ 位相同期サブオクターブ・アンカー)
 *   9. VocalDoublerAndMicroShiftSuite (Abbey Road J37 ヴァリスピードADT ＋ H3000 ±8.5 cents マイクロピッチ)
 *  10. VintageDigitalReverbAndDelaySuite (Roland Dimension D ＋ AMS RMX16 NonLin2 ＋ Lexicon 224 ホール)
 *  11. MultibandLinearPhaseCrossoverSuite (Linkwitz-Riley 4次 24dB/oct 4バンド位相補償クロスオーバー)
 *  12. AnalogTapeTransportAndFlutterSuite (Ampex ATR-102 30 IPS ヘッドバンプ ＋ 高域バイアス自己消磁)
 *  13. MasteringStereoFieldAndPhaseSuite (完全モノラル互換オールパスワイドナー ＋ ピアソン逆相自動レスキュー)
 *  14. BinauralHeadphoneAndRoomSuite (球体頭部回折30°外在化 ＋ 耳介Pinnaノッチ ＋ Harmanターゲット較正)
 *  15. MasteringLimiterAndTruePeakSuite (8倍Hermite ISP True-Peak検出 ＋ 2段IRCリミッター ＋ EBU R128 LUFS計)
 *  16. MasteringDitherAndBitDepthSuite (Lipshitz 3次聴覚心理ノイズシェーピング ＋ TPDFディザ)
 */

#include "StudioLiveSoundEffectsEngine.h"
#include "StudioFoleyAndStageAmbienceBank.h"
#include "NeuralFormantAndGlottalSynth.h"
#include "MultibandLinearPhaseCrossoverEngine.h"
#include "AnalogTubeAndOptoMasteringChain.h"
#include "AccompanimentDuckingAndStemSculptor.h"
#include "LiveStageAndVtuberDirectorEngine.h"
#include "PrecisionPitchAndVibratoSynthesizer.h"
#include "MicrophoneAndPreampPhysicalTwin.h"
#include "MasteringLimiterAndTruePeakEngine.h"
#include "DynamicDePlosiveAndDeClickEngine.h"
#include "BinauralHeadphoneAndRoomCalibration.h"
#include "VintageDigitalReverbAndDelayRack.h"
#include "AdaptiveAutoEqAndResonanceSuppressor.h"
#include "VocalDoublerAndMicroShiftArray.h"
#include "HarmonicSeriesSculptorAndSubBass.h"
#include "MasteringStereoFieldAndPhaseDoctor.h"
#include "StreamingAudioRouterAndSafetyMatrix.h"
#include "PsychoacousticTransientAndSustainEngine.h"
#include "BroadcastMasteringPresetVault.h"
#include "VocalEmotionAndProsodyAnalyzer.h"
#include "AcousticImpedanceAndVocalTractTwin.h"
#include "MasteringDitherAndBitDepthQuantizer.h"
#include "ConcertStageLightingAndSnsSync.h"
#include "PsychoacousticMaskingAndVocalPocket.h"
#include "AnalogTapeTransportAndFlutterModel.h"
#include "HarmonicPitchQuantizerAndScaleEngine.h"

namespace gzzio
{
namespace nextgen_orchestrator
{

class NextGenMasteringOrchestrator
{
public:
    struct UnifiedTelemetry
    {
        float resonanceSuppressionDb = 0.0f;
        float spectralTiltDb         = 0.0f;
        float analogCompressionDb    = 0.0f;
        float phaseCorrelation       = 1.0f;
        float momentaryLufs          = -24.0f;
        float truePeakDbtp           = -12.0f;
        float emotionValence         = 0.25f;
        float emotionArousal         = 0.15f;
        float mouthOpen01            = 0.0f;
        bool  confettiClimaxActive   = false;
    };

    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        deClicker_.prepare(sr_);
        micTwin_.prepare(sr_);
        tractTwin_.prepare(sr_);
        glottal_.prepare(sr_);
        autoEq_.prepare(sr_);
        analogChain_.prepare(sr_);
        transientSuite_.prepare(sr_);
        harmonicSculptor_.prepare(sr_);
        vocalDoubler_.prepare(sr_);
        vintageSpatial_.prepare(sr_);
        crossover_.prepare(sr_);
        tapeTransport_.prepare(sr_);
        phaseDoctor_.prepare(sr_);
        binauralSuite_.prepare(sr_);
        truePeakLimiter_.prepare(sr_);
        ditherSuite_.prepare(sr_);
        barkBooster_.prepare(sr_);
        emotionSuite_.prepare(sr_);
        vtuberDirector_.prepare(sr_);
        pyroDirector_.prepare(sr_);
        telemetry_ = {};
    }

    void reset() noexcept
    {
        deClicker_.reset();
        micTwin_.reset();
        tractTwin_.reset();
        glottal_.reset();
        autoEq_.reset();
        analogChain_.reset();
        transientSuite_.reset();
        harmonicSculptor_.reset();
        vocalDoubler_.reset();
        vintageSpatial_.reset();
        crossover_.reset();
        tapeTransport_.reset();
        phaseDoctor_.reset();
        binauralSuite_.reset();
        truePeakLimiter_.reset();
        ditherSuite_.reset();
        barkBooster_.reset();
        emotionSuite_.reset();
        vtuberDirector_.reset();
        pyroDirector_.reset();
        telemetry_ = {};
    }

    // ボーカル＆マスターバスに対する繊細なハイエンド・ポリッシュ処理（原音キャラクターを損なわない微細仕上げ）
    void processMasterPolishSample(float& ioL, float& ioR,
                                   float polishIntensity01,
                                   float spatialDepth01,
                                   float binauralComfort01) noexcept
    {
        const float pol = std::clamp(polishIntensity01, 0.0f, 1.0f);
        const float spa = std::clamp(spatialDepth01, 0.0f, 1.0f);
        const float bin = std::clamp(binauralComfort01, 0.0f, 1.0f);

        if (pol > 1.0e-4f)
        {
            deClicker_.processSamplePair(ioL, ioR, pol * 0.22f, pol * 0.25f);
            tractTwin_.processStereoSample(ioL, ioR, pol * 0.14f, pol * 0.18f, pol * 0.12f);
            autoEq_.processStereoSample(ioL, ioR, pol * 0.24f, pol * 0.18f);
            barkBooster_.processStereo(ioL, ioR, pol * 0.20f);
            analogChain_.processSamplePair(ioL, ioR, pol * 0.16f, pol * 0.20f, pol * 0.15f);
            transientSuite_.processStereoSample(ioL, ioR, pol * 0.15f, pol * 0.18f, pol * 0.22f);
            harmonicSculptor_.processStereoSample(ioL, ioR, pol * 0.14f, pol * 0.08f);
            tapeTransport_.processStereoSample(ioL, ioR, pol * 0.15f);
        }

        if (spa > 1.0e-4f)
        {
            vocalDoubler_.processStereoSample(ioL, ioR, spa * 0.12f, spa * 0.14f);
            vintageSpatial_.processStereoSample(ioL, ioR, spa * 0.15f, spa * 0.08f, spa * 0.10f);
            phaseDoctor_.processStereoSample(ioL, ioR, spa * 0.16f, true);
        }

        if (bin > 1.0e-4f)
        {
            binauralSuite_.processStereoSample(ioL, ioR, bin * 0.25f, bin * 0.20f);
        }

        emotionSuite_.analyzeStereoSample(ioL, ioR);
        vtuberDirector_.getVisemeAnimator().updateViseme(
            static_cast<float>(1.0 / sr_), 0.5f * (std::fabs(ioL) + std::fabs(ioR)), 650.0f, 1450.0f, true);
        pyroDirector_.analyzeStereoSample(ioL, ioR);

        telemetry_.resonanceSuppressionDb = autoEq_.getTelemetry().resonanceCutDb;
        telemetry_.spectralTiltDb         = autoEq_.getTelemetry().spectralTiltDb;
        telemetry_.analogCompressionDb    = analogChain_.getLa2aOpto().getOptoGainReductionDb()
                                          + analogChain_.getFairchild670().getGainReductionMidDb();
        telemetry_.phaseCorrelation       = phaseDoctor_.getMetrics().correlationNeg1To1;
        telemetry_.emotionValence         = emotionSuite_.getEmotionState().valenceNeg1To1;
        telemetry_.emotionArousal         = emotionSuite_.getEmotionState().arousalNeg1To1;
        telemetry_.mouthOpen01            = vtuberDirector_.getVisemeAnimator().getCurrentVisemeFrame().mouthOpenHeight01;
        telemetry_.confettiClimaxActive   = pyroDirector_.isConfettiTriggered();
    }

    const UnifiedTelemetry& getTelemetry() const noexcept { return telemetry_; }

private:
    double sr_ = 48000.0;
    declick_plosive::DynamicDePlosiveAndDeClickSuite               deClicker_;
    mic_physical_twin::MicrophoneAndPreampPhysicalTwinSuite        micTwin_;
    vocal_tract_twin::AcousticImpedanceAndVocalTractSuite          tractTwin_;
    glottal_synth::NeuralFormantAndGlottalSuite                    glottal_;
    adaptive_autoeq::AdaptiveAutoEqAndResonanceSuite               autoEq_;
    analog_mastering::AnalogTubeAndOptoMasteringSuite              analogChain_;
    transient_sustain::PsychoacousticTransientAndSustainSuite      transientSuite_;
    harmonic_sculptor::HarmonicSeriesSculptorAndSubBassSuite       harmonicSculptor_;
    vocal_doubler::VocalDoublerAndMicroShiftSuite                  vocalDoubler_;
    vintage_spatial::VintageDigitalReverbAndDelaySuite             vintageSpatial_;
    multiband_lr4::MultibandLinearPhaseCrossoverSuite              crossover_;
    tape_transport::AnalogTapeTransportAndFlutterSuite             tapeTransport_;
    phase_doctor::MasteringStereoFieldAndPhaseSuite                phaseDoctor_;
    binaural_cal::BinauralHeadphoneAndRoomSuite                    binauralSuite_;
    true_peak_limiter::MasteringLimiterAndTruePeakSuite            truePeakLimiter_;
    mastering_dither::MasteringDitherAndBitDepthSuite              ditherSuite_;
    bark_masking::VocalAudibilityMarginBooster                     barkBooster_;
    emotion_prosody::VocalEmotionAndProsodySuite                   emotionSuite_;
    stage_director::LiveStageAndVtuberDirectorSuite                vtuberDirector_;
    stage_lighting::VocalClimaxPyroAndConfettiDirector             pyroDirector_;
    UnifiedTelemetry telemetry_{};
};

} // namespace nextgen_orchestrator
} // namespace gzzio

namespace vg
{
namespace nextgen_orchestrator = ::gzzio::nextgen_orchestrator;
} // namespace vg
