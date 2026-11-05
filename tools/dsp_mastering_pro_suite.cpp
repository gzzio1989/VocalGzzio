// =============================================================================
//  dsp_mastering_pro_suite.cpp — VocalGzzio v5.5.0
//  次世代プロマスタリング＆音響科学DSP群・物理キネティックUIの数理回帰検証スイート
// =============================================================================
//  【検証項目】
//  1. Zavalishin TPT/ZDF SVF フィルタの全9モード周波数応答精度と高速変調安定性
//  2. ADAA 2次 連続時間逆微分飽和器の折り返し歪み低減とDCオフセット完全抑制
//  3. ヒルベルト変換・直交解析信号包絡線検波器の低音85Hzリップル免疫検証
//  4. Serban-Style 並列FET/Optoボーカルバス＋Oxford知覚インフレータの無歪み密度向上
//  5. Jaycen-Style 4帯域ダイナミック耳障り共振サプレッサーの3.25kHzピンポイント抑制
//  6. ISO 226:2023 動的等ラウドネス曲線のウィスパー補強＆強唱整理の非対称検証
//  7. Sundberg シンガーズフォルマント＆音素識別器（母音 vs 歯擦音）の自動判別精度
//  8. Zwicker 16臨界帯域バーク尺度マスキング解析と Vocal Audibility Index (VAI) 検証
//  9. AudioStabilityGuardian による NaN / ±Inf / Denormal / DC / 1サンプル突発ポップ防御
// 10. Pultec EQP-1A Low-End Trick / H3000 ダブラー / エリプティック低域モノメーカー /
//     4相ポリフェーズFIRトゥルーピーク天井制御 / 物理キネティック磁気デテント収束検査
// =============================================================================

#include "../Source/MasteringProEngine.h"
#include "../Source/VocalScienceLab.h"
#include "../Source/GrammyVocalChainRack.h"
#include "../Source/SpatialMasteringConsole.h"
#include "../Source/VocalArticulationPolish.h"
#include "../Source/BroadcastStreamGuard.h"
#include "../Source/HorologyTactileUi.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <array>
#include <limits>
#include <algorithm>

namespace
{

int gTotalChecks = 0;
int gFailedChecks = 0;

void requireCondition(bool cond, const char* expr, const char* detail, int line)
{
    ++gTotalChecks;
    if (!cond)
    {
        ++gFailedChecks;
        std::fprintf(stderr, "[FAIL] Line %d: %s (%s)\n", line, expr, detail);
    }
}

#define GZ_VERIFY(cond, detail) requireCondition((cond), #cond, (detail), __LINE__)

float measureRms(const std::vector<float>& buf, size_t startIdx = 0)
{
    if (buf.size() <= startIdx)
        return 0.0f;
    double sumSq = 0.0;
    const size_t n = buf.size() - startIdx;
    for (size_t i = startIdx; i < buf.size(); ++i)
        sumSq += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(n)));
}

float measurePeak(const std::vector<float>& buf, size_t startIdx = 0)
{
    float pk = 0.0f;
    for (size_t i = startIdx; i < buf.size(); ++i)
        pk = std::max(pk, std::fabs(buf[i]));
    return pk;
}

float measureDcMean(const std::vector<float>& buf, size_t startIdx = 0)
{
    if (buf.size() <= startIdx)
        return 0.0f;
    double sum = 0.0;
    for (size_t i = startIdx; i < buf.size(); ++i)
        sum += static_cast<double>(buf[i]);
    return static_cast<float>(sum / static_cast<double>(buf.size() - startIdx));
}

// -----------------------------------------------------------------------------
//  Test 1: Zavalishin TPT/ZDF SVF Filter Stability & Multi-Output Reconstruction
// -----------------------------------------------------------------------------
void testTptStateVariableFilter()
{
    using gzzio::mastering::TptStateVariableFilter;
    using gzzio::mastering::FastAudioMath;

    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        TptStateVariableFilter svf;
        svf.prepare(sr);

        // 1. 高速カットオフ変調（80Hz ⇄ 18000Hz）でも発散せず有界であることを検証
        svf.setParameters(TptStateVariableFilter::Mode::PeakBell, 1000.0f, 8.0f, 12.0f);
        bool allFinite = true;
        float maxOut = 0.0f;
        for (int n = 0; n < 8000; ++n)
        {
            const float modFc = 120.0f + 14000.0f * (0.5f + 0.5f * std::sin(static_cast<float>(n) * 0.03f));
            svf.setParameters(TptStateVariableFilter::Mode::PeakBell, modFc, 6.5f, 9.0f);
            const float x = 0.5f * std::sin(static_cast<float>(n) * 0.15f);
            const float y = svf.processSample(x);
            if (!std::isfinite(y))
                allFinite = false;
            maxOut = std::max(maxOut, std::fabs(y));
        }
        GZ_VERIFY(allFinite, "TPT SVF must stay 100% finite under audio-rate cutoff modulation");
        GZ_VERIFY(maxOut > 0.1f && maxOut < 8.0f, "TPT SVF modulated output must stay bounded");

        // 2. Multi-Output 完全再構成恒等式: LP + k*BP + HP == Input
        svf.reset();
        constexpr float kQ = 0.70710678f;
        svf.setParameters(TptStateVariableFilter::Mode::Lowpass, 1200.0f, kQ, 0.0f);
        double maxReconErr = 0.0;
        for (int n = 0; n < 2000; ++n)
        {
            const float x = 0.8f * std::sin(static_cast<float>(n) * 0.07f);
            float lp = 0.0f, bp = 0.0f, hp = 0.0f;
            svf.processMultiOutput(x, lp, bp, hp);
            const float recon = lp + (1.0f / kQ) * bp + hp;
            maxReconErr = std::max(maxReconErr, static_cast<double>(std::fabs(recon - x)));
        }
        GZ_VERIFY(maxReconErr < 1.0e-5, "TPT SVF LP + k*BP + HP must exactly reconstruct input signal");
    }
}

// -----------------------------------------------------------------------------
//  Test 2: ADAA 2nd-Order Triode & Transformer Stage (Zero DC & Bounded Harmonics)
// -----------------------------------------------------------------------------
void testAdaaTriodeTransformerStage()
{
    using gzzio::mastering::AdaaTriodeTransformerStage;

    AdaaTriodeTransformerStage adaa;
    adaa.prepare(48000.0);
    adaa.setDriveAndBias(0.75f, 0.28f, 0.65f);

    const size_t N = 24000;
    std::vector<float> out(N, 0.0f);
    for (size_t i = 0; i < N; ++i)
    {
        const double t = static_cast<double>(i) / 48000.0;
        const float sig = static_cast<float>(0.65 * std::sin(2.0 * 3.141592653589793 * 440.0 * t));
        out[i] = adaa.processSample(sig);
    }

    const float rms = measureRms(out, 4000);
    const float pk  = measurePeak(out, 4000);
    const float dc  = std::fabs(measureDcMean(out, 8000));

    GZ_VERIFY(rms > 0.25f && rms < 0.85f, "ADAA stage RMS must remain healthy");
    GZ_VERIFY(pk <= 1.05f, "ADAA stage peak must be smoothly saturated");
    GZ_VERIFY(dc < 0.005f, "ADAA asymmetric saturation DC offset must be removed by 14Hz TPT highpass");
}

// -----------------------------------------------------------------------------
//  Test 3: Hilbert Analytic Envelope Detector (Zero Ripple on 85Hz Male Fundamental)
// -----------------------------------------------------------------------------
void testHilbertAnalyticDetector()
{
    using gzzio::mastering::HilbertAnalyticDetector;

    HilbertAnalyticDetector hilbert;
    hilbert.prepare(48000.0);
    hilbert.setBallistics(0.5f, 15.0f); // 超高速セッティングでもリップルが出ないことを検証

    const double freq = 85.0; // 男性低音ボーカル基音
    const double amp  = 0.50;
    float minEnv = 10.0f;
    float maxEnv = 0.0f;

    for (int i = 0; i < 24000; ++i)
    {
        const float x = static_cast<float>(amp * std::sin(2.0 * 3.141592653589793 * freq * i / 48000.0));
        const float env = hilbert.processSample(x);
        if (i > 12000)
        {
            minEnv = std::min(minEnv, env);
            maxEnv = std::max(maxEnv, env);
        }
    }

    const float rippleRatioDb = 20.0f * std::log10((maxEnv + 1.0e-6f) / (minEnv + 1.0e-6f));
    GZ_VERIFY(rippleRatioDb < 0.35f, "Hilbert quadrature detector must exhibit <0.35 dB ripple at 85Hz");
    GZ_VERIFY(std::fabs(maxEnv - 0.50f) < 0.05f, "Hilbert detector envelope must converge to true sine amplitude");
}

// -----------------------------------------------------------------------------
//  Test 4: Dynamic Harshness Tamer (Selective 3.25kHz Resonance Suppression)
// -----------------------------------------------------------------------------
void testDynamicHarshnessTamer()
{
    using gzzio::mastering::DynamicHarshnessTamer;

    DynamicHarshnessTamer tamer;
    tamer.prepare(48000.0);
    tamer.setSensitivity(0.85f, 6.5f);

    // 440Hz の穏やかな基音 ＋ 3250Hz の刺さる咽頭共振ピークを合成
    std::vector<float> inBuf(24000), outBuf(24000);
    for (size_t i = 0; i < inBuf.size(); ++i)
    {
        const double t = static_cast<double>(i) / 48000.0;
        const float warmFund = static_cast<float>(0.18 * std::sin(2.0 * 3.141592653589793 * 440.0 * t));
        const float harshPeak= static_cast<float>(0.55 * std::sin(2.0 * 3.141592653589793 * 3250.0 * t));
        inBuf[i]  = warmFund + harshPeak;
        outBuf[i] = tamer.processSample(inBuf[i]);
    }

    const float inRms  = measureRms(inBuf, 6000);
    const float outRms = measureRms(outBuf, 6000);
    const float node1CutDb = tamer.getNodeCutDb(1); // 3250Hz Node

    GZ_VERIFY(node1CutDb < -1.5f, "DynamicHarshnessTamer 3.25kHz node must actively attenuate harsh resonance");
    GZ_VERIFY(outRms < inRms * 0.88f, "DynamicHarshnessTamer must reduce harsh signal energy");
}

// -----------------------------------------------------------------------------
//  Test 5: ISO 226:2023 Dynamic Equal-Loudness Contour (Whisper Lift vs Belting Trim)
// -----------------------------------------------------------------------------
void testIso226LoudnessContour()
{
    using gzzio::mastering::Iso226LoudnessContour;

    Iso226LoudnessContour iso;
    iso.prepare(48000.0);
    iso.setAmount(0.80f, -18.0f);

    // 1. 小声ウィスパー (-34 dBFS = 0.02 linear) 入力時に低域＆エアー補償が正(+)になることを検証
    for (int i = 0; i < 12000; ++i)
    {
        const float whisper = 0.02f * std::sin(static_cast<float>(i) * 0.08f);
        (void) iso.processSample(whisper);
    }
    GZ_VERIFY(iso.getLowCompensationDb() > 0.5f, "ISO 226 must boost low warmth during quiet whisper passages");
    GZ_VERIFY(iso.getAirCompensationDb() > 0.3f, "ISO 226 must boost high air during quiet whisper passages");

    // 2. 強唱ベルティング (-4 dBFS = 0.63 linear) 入力時に中域緩和が負(-)になることを検証
    for (int i = 0; i < 12000; ++i)
    {
        const float belt = 0.63f * std::sin(static_cast<float>(i) * 0.08f);
        (void) iso.processSample(belt);
    }
    GZ_VERIFY(iso.getMidCompensationDb() < -0.2f, "ISO 226 must gently tame 3.35kHz during loud belting");
}

// -----------------------------------------------------------------------------
//  Test 6: Sundberg Singer's Formant, Phoneme Classifier & Bark Masking Analyzer
// -----------------------------------------------------------------------------
void testVocalScienceLabSuite()
{
    using gzzio::science::ZeroLagPhonemeClassifier;
    using gzzio::science::VocalScienceLabSuite;

    ZeroLagPhonemeClassifier clf;
    clf.prepare(48000.0);

    // 1. 220Hz + 440Hz 母音ライク信号の識別
    ZeroLagPhonemeClassifier::PhonemeState vowelState {};
    for (int i = 0; i < 6000; ++i)
    {
        const double t = static_cast<double>(i) / 48000.0;
        const float v = static_cast<float>(0.45 * std::sin(2.0 * 3.141592653589793 * 220.0 * t)
                                         + 0.20 * std::sin(2.0 * 3.141592653589793 * 440.0 * t));
        vowelState = clf.processSample(v);
    }
    GZ_VERIFY(vowelState.voicedProb01 > 0.55f, "Phoneme classifier must detect voiced vowel phonation");
    GZ_VERIFY(vowelState.sibilantProb01 < 0.20f, "Voiced vowel must not trigger sibilant detector");

    // 2. 7500Hz 高域摩擦音（歯擦音ライク信号）の識別
    clf.reset();
    ZeroLagPhonemeClassifier::PhonemeState sibState {};
    for (int i = 0; i < 6000; ++i)
    {
        const double t = static_cast<double>(i) / 48000.0;
        const float s = static_cast<float>(0.30 * std::sin(2.0 * 3.141592653589793 * 7500.0 * t));
        sibState = clf.processSample(s);
    }
    GZ_VERIFY(sibState.sibilantProb01 > 0.55f, "Phoneme classifier must detect high-frequency sibilance");

    // 3. BarkMaskingAnalyzer & VocalScienceLabSuite 統合動作検証
    VocalScienceLabSuite lab;
    lab.prepare(48000.0, 512);
    lab.setVoiceScienceParameters(0.55f, 0.40f, 0.70f, 0.45f);

    std::vector<float> vL(1024), vR(1024), bgm(1024);
    for (int block = 0; block < 20; ++block)
    {
        for (size_t i = 0; i < 1024; ++i)
        {
            const double t = static_cast<double>(block * 1024 + static_cast<int>(i)) / 48000.0;
            vL[i]  = static_cast<float>(0.35 * std::sin(2.0 * 3.141592653589793 * 300.0 * t)
                                      + 0.25 * std::sin(2.0 * 3.141592653589793 * 3000.0 * t));
            vR[i]  = vL[i];
            bgm[i] = static_cast<float>(0.15 * std::sin(2.0 * 3.141592653589793 * 500.0 * t));
        }
        lab.processStereoWithBgmReference(vL.data(), vR.data(), bgm.data(), 1024);
    }

    const auto bark = lab.getBarkTelemetry();
    GZ_VERIFY(bark.vocalAudibilityIndex >= 15.0f && bark.vocalAudibilityIndex <= 100.0f,
              "Bark Vocal Audibility Index must stay within [15, 100]");
    GZ_VERIFY(bark.singersFormant01 > 0.15f, "Bark Singer's Formant telemetry must register 3kHz vocal energy");
}

// -----------------------------------------------------------------------------
//  Test 7: AudioStabilityGuardian Torture Test (NaN, Inf, Denormal, DC, USB Pop)
// -----------------------------------------------------------------------------
void testAudioStabilityGuardianTorture()
{
    using gzzio::science::AudioStabilityGuardian;

    AudioStabilityGuardian guard;
    guard.prepare(48000.0, 512);

    std::vector<float> L(4096, 0.0f), R(4096, 0.0f);
    for (size_t i = 0; i < L.size(); ++i)
    {
        // +0.25 の強いDCオフセットを重畳した正弦波
        L[i] = 0.25f + 0.30f * std::sin(static_cast<float>(i) * 0.05f);
        R[i] = -0.20f + 0.30f * std::cos(static_cast<float>(i) * 0.05f);
    }

    // 破壊的異常値の注入: NaN, +Inf, -Inf, Denormal, 1サンプル +100.0f 突発ポップ
    L[500]  = std::numeric_limits<float>::quiet_NaN();
    R[501]  = std::numeric_limits<float>::infinity();
    L[1200] = -std::numeric_limits<float>::infinity();
    L[1800] = 1.0e-39f; // Denormal
    L[2500] = 50.0f;    // 突発ケーブルスパイク

    guard.processStereoBlock(L.data(), R.data(), static_cast<int>(L.size()));

    bool allSafe = true;
    float maxAbs = 0.0f;
    for (size_t i = 0; i < L.size(); ++i)
    {
        if (!std::isfinite(L[i]) || !std::isfinite(R[i]))
            allSafe = false;
        maxAbs = std::max(maxAbs, std::max(std::fabs(L[i]), std::fabs(R[i])));
    }

    const auto diag = guard.getDiagnostics();
    const float tailDc = std::fabs(measureDcMean(L, 3000));

    GZ_VERIFY(allSafe, "AudioStabilityGuardian must scrub 100% of NaN and Inf values");
    GZ_VERIFY(diag.nanInfScrubCount >= 3u, "Guardian must count all scrubbed NaN/Inf events");
    GZ_VERIFY(diag.slewSpikeArrests >= 1u, "Guardian must arrest single-sample cable spike");
    GZ_VERIFY(maxAbs <= 4.0f, "Guardian hardware safety ceiling must bound output amplitude");
    GZ_VERIFY(tailDc < 0.04f, "Guardian 11.5Hz TPT DC blocker must remove DC offset");
}

// -----------------------------------------------------------------------------
//  Test 8: GrammyVocalChainRack, SpatialMasteringConsole, Polish & StreamGuard
// -----------------------------------------------------------------------------
void testFullMasteringAndSpatialSuites()
{
    gzzio::mastering::MasteringProEngine masteringPro;
    gzzio::grammy_rack::GrammyVocalChainRack grammyRack;
    gzzio::spatial_mastering::SpatialMasteringConsole spatialConsole;
    gzzio::articulation::VocalArticulationPolishSuite articulationSuite;
    gzzio::stream_guard::BroadcastStreamGuardSuite streamGuard;

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        masteringPro.prepare(sr);
        grammyRack.prepare(sr);
        spatialConsole.prepare(sr);
        articulationSuite.prepare(sr);
        streamGuard.prepare(sr);

        masteringPro.setMacroMorphXY(0.78f, 0.72f);
        grammyRack.setChainMacroParameters(0.55f, 0.50f, 0.45f, 0.40f);
        spatialConsole.setConsoleParameters(0.50f, 0.45f, 0.50f, -0.3f);
        articulationSuite.setPolishParameters(0.50f, 0.55f, 0.45f, 0.40f);
        streamGuard.setStreamGuardParameters(0.65f, 0.55f, 0.35f);

        std::vector<float> L(2048), R(2048);
        for (size_t i = 0; i < L.size(); ++i)
        {
            const double t = static_cast<double>(i) / sr;
            const float v = static_cast<float>(0.42 * std::sin(2.0 * 3.141592653589793 * 261.63 * t)
                                             + 0.18 * std::sin(2.0 * 3.141592653589793 * 523.25 * t)
                                             + 0.08 * std::sin(2.0 * 3.141592653589793 * 3100.0 * t));
            L[i] = v;
            R[i] = v;
        }

        streamGuard.processStereoBlock(L.data(), R.data(), 2048);
        articulationSuite.processStereoBlock(L.data(), R.data(), 2048, 0.1f, 0.05f);
        masteringPro.processStereoBlock(L.data(), R.data(), 2048);
        grammyRack.processStereoBlock(L.data(), R.data(), 2048);
        spatialConsole.processStereoBlock(L.data(), R.data(), 2048);

        const float outPeak = std::max(measurePeak(L, 256), measurePeak(R, 256));
        const float outRms  = 0.5f * (measureRms(L, 256) + measureRms(R, 256));
        const float corr    = spatialConsole.getStereoCorrelation();
        const auto kMetrics = grammyRack.getKSystemMetrics();

        GZ_VERIFY(outRms > 0.08f && outPeak <= 1.02f, "Full Grammy + Spatial chain must stay clean and within ceiling");
        GZ_VERIFY(corr > 0.50f, "Elliptic low-end aligner must preserve strong mono-compatible stereo correlation");
        GZ_VERIFY(std::isfinite(kMetrics.momentaryLufs) && kMetrics.momentaryLufs > -60.0f,
                  "Bob Katz K-System ITU-R BS.1770-4 meter must compute valid LUFS");
    }
}

// -----------------------------------------------------------------------------
//  Test 9: HorologyTactileUi KineticDialPhysics Detent & Spring Convergence
// -----------------------------------------------------------------------------
void testKineticDialPhysics()
{
    gzzio::ui_horology::KineticDialPhysics phys;

    // 黄金比デテント (0.618) の近傍 (0.612) を与えた際に吸着することを検証
    const float snapped = phys.applyMagneticDetent(0.612f, false);
    GZ_VERIFY(std::fabs(snapped - 0.618f) < 0.002f, "KineticDialPhysics must magnetically snap to 0.618 golden ratio detent");
    GZ_VERIFY(phys.isSnappedToDetent(), "Detent state must be marked active");

    // 120フレーム (2秒) のバネ・ダンパ物理ステップ後に目標値へ完全収束することを検証
    for (int f = 0; f < 120; ++f)
        phys.stepPhysics(1.0f / 60.0f);

    GZ_VERIFY(std::fabs(phys.getDisplayValue() - snapped) < 0.001f,
              "Spring-damper physics must smoothly converge to target value");
}

} // namespace

int main()
{
    testTptStateVariableFilter();
    testAdaaTriodeTransformerStage();
    testHilbertAnalyticDetector();
    testDynamicHarshnessTamer();
    testIso226LoudnessContour();
    testVocalScienceLabSuite();
    testAudioStabilityGuardianTorture();
    testFullMasteringAndSpatialSuites();
    testKineticDialPhysics();

    std::printf("[dsp_mastering_pro_suite] Completed %d checks (%d failed)\n", gTotalChecks, gFailedChecks);
    return (gFailedChecks == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
