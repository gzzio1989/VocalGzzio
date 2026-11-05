/**
 * @file ui_horology_and_science_audit.cpp
 * @brief VocalGzzio 純正律ピッチ科学・8x8アダマール音響空間・テープヒステリシス・ミックスグルー・配信ラウドネス＆スイス時計UI描画 総合検証スイート
 */

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../Source/PitchAndHarmonyScience.h"
#include "../Source/StudioAcousticRoomEngine.h"
#include "../Source/VocalistPerformanceCoach.h"
#include "../Source/HarmonicExciterAndTapeDeck.h"
#include "../Source/IntelligentMixGlueEngine.h"
#include "../Source/HorologyInteractiveDeck.h"
#include "../Source/DynamicFormantAndRegisterEngine.h"
#include "../Source/PsychoacousticClarityEngine.h"
#include "../Source/MasteringLoudnessAndDelivery.h"
#include "../Source/MasteringConsolePresets.h"

namespace
{
constexpr float kTwoPi = 6.28318530717958647692f;

bool checkCondition(bool cond, const char* msg)
{
    if (!cond)
    {
        std::fprintf(stderr, "[FAIL] %s\n", msg);
        return false;
    }
    std::printf("[PASS] %s\n", msg);
    return true;
}

bool testPitchAndHarmonyScience()
{
    constexpr double sr = 48000.0;
    vg::pitch_science::PitchAndHarmonyScienceSuite suite;
    suite.prepare(sr);

    // 1. 純正律チューナー検証: 長3度 (+4 semitones) は -13.686 cents、完全5度 (+7 semitones) は +1.955 cents
    const float major3rdCents = vg::pitch_science::JustIntonationChordTuner::getPureJustCentsOffset(4, false);
    const float perfect5thCents = vg::pitch_science::JustIntonationChordTuner::getPureJustCentsOffset(7, false);

    if (!checkCondition(std::fabs(major3rdCents - (-13.6863f)) < 0.05f &&
                        std::fabs(perfect5thCents - (+1.9550f)) < 0.05f,
                        "JustIntonationChordTuner exact 5:4 (-13.69c) and 3:2 (+1.96c) offsets"))
        return false;

    std::array<int, 5> chordShifts = { 4, 7, 12, -3, -5 };
    for (int i = 0; i < 12000; ++i)
        suite.getJustIntonationTuner().updateHarmonyTuning(220.0f, true, chordShifts, 1.0f);

    if (!checkCondition(suite.getJustIntonationTuner().getNoteHoldLock01() > 0.85f &&
                        suite.getJustIntonationTuner().getBeatingReductionIndexDb() > 1.0f,
                        "JustIntonationChordTuner sustained chord lock-in & acoustic beating reduction"))
        return false;

    // 2. 輪状甲状筋ビブラート＆最小躍度ポルタメント検証
    auto& vib = suite.getKinematicVibrato();
    const float m0 = vg::pitch_science::KinematicVibratoAndPortamentoModel::evaluateMinimumJerk(0.0f);
    const float mHalf = vg::pitch_science::KinematicVibratoAndPortamentoModel::evaluateMinimumJerk(0.5f);
    const float m1 = vg::pitch_science::KinematicVibratoAndPortamentoModel::evaluateMinimumJerk(1.0f);
    if (!checkCondition(std::fabs(m0) < 1.0e-5f && std::fabs(mHalf - 0.5f) < 1.0e-5f && std::fabs(m1 - 1.0f) < 1.0e-5f,
                        "KinematicVibratoAndPortamentoModel minimum-jerk trajectory boundary conditions"))
        return false;

    vg::pitch_science::KinematicVibratoAndPortamentoModel::VibratoState st{};
    for (int n = 0; n < 24000; ++n) // 0.5 秒間持続
        st = vib.tickVibrato(true, 5.65f, 25.0f, 0.18f);

    if (!checkCondition(st.bloomEnvelope01 > 0.75f && st.frequencyMultiplier > 0.9f && st.frequencyMultiplier < 1.1f,
                        "KinematicVibratoAndPortamentoModel onset delay & natural bloom envelope"))
        return false;

    // 3. ビタビ・オクターブジャンプ防止器検証
    auto& vit = suite.getViterbiRefiner();
    for (int i = 0; i < 30; ++i)
        vit.refineF0Estimate(220.0f, 0.92f);
    // 突発的な1オクターブ誤検出 (440Hz, 低〜中信頼度 0.55) を注入しても 220Hz 付近を維持するか
    const float guardedF0 = vit.refineF0Estimate(440.0f, 0.55f);
    if (!checkCondition( std::fabs(guardedF0 - 220.0f) < 15.0f,
                        "SubSampleViterbiPitchRefiner blocks spurious octave-jump glitch"))
        return false;

    return true;
}

bool testStudioAcousticRoomEngine()
{
    constexpr double sr = 48000.0;
    vg::studio_acoustics::StudioAcousticRoomSuite room;
    room.prepare(sr);

    // 1. 8x8 直交アダマール行列のエネルギー保存則検証 (∑x_i^2 == ∑y_i^2)
    std::array<float, 8> vec = { 1.0f, -0.5f, 0.25f, 0.8f, -0.3f, 0.6f, -0.9f, 0.4f };
    float energyBefore = 0.0f;
    for (float v : vec)
        energyBefore += v * v;

    vg::studio_acoustics::Hadamard8x8VelvetFdnReverb::applyNormalizedHadamard8(vec);
    float energyAfter = 0.0f;
    for (float v : vec)
        energyAfter += v * v;

    if (!checkCondition(std::fabs(energyBefore - energyAfter) < 1.0e-4f,
                        "Hadamard8x8VelvetFdnReverb exact orthogonal energy conservation"))
        return false;

    // 2. デリバーブ＋名機マイクカプセル＋FDNリバーブ統合処理の安定性検証
    bool finiteOk = true;
    for (int n = 0; n < 12000; ++n)
    {
        const float t = static_cast<float>(n / sr);
        float l = 0.35f * std::sin(kTwoPi * 240.0f * t);
        float r = l;
        room.processSamplePair(l, r, 0.65f,
                               vg::studio_acoustics::VintageMicrophoneCapsuleSimulator::CapsuleModel::GermanTubeU47,
                               0.80f, 0.35f);
        if (!std::isfinite(l) || !std::isfinite(r))
            finiteOk = false;
    }

    return checkCondition(finiteOk, "StudioAcousticRoomSuite (DRR De-Reverb + U47 Tube + 8x8 FDN) stability");
}

bool testTapeGluePsychoAndDeliverySuites()
{
    constexpr double sr = 48000.0;
    vg::tape_and_harmonics::HarmonicExciterAndTapeSuite tapeSuite;
    vg::mix_glue::IntelligentMixGlueSuite glueSuite;
    vg::register_science::DynamicFormantAndRegisterSuite regSuite;
    vg::psychoacoustics::PsychoacousticClaritySuite psychoSuite;
    vg::mastering_delivery::MasteringLoudnessAndDeliverySuite deliverySuite;
    vg::performance_coach::VocalistPerformanceCoachSuite coachSuite;

    tapeSuite.prepare(sr);
    glueSuite.prepare(sr);
    regSuite.prepare(sr);
    psychoSuite.prepare(sr);
    deliverySuite.prepare(sr);
    coachSuite.prepare(sr);

    bool allFinite = true;
    for (int n = 0; n < 16000; ++n)
    {
        const float t = static_cast<float>(n / sr);
        // 基音 330Hz (E4 パッサージョ) + 8.4kHz 歯擦音バースト
        float l = 0.42f * std::sin(kTwoPi * 330.0f * t) + 0.25f * std::sin(kTwoPi * 8450.0f * t);
        float r = l;

        tapeSuite.processSamplePair(l, r, 0.55f, 0.65f, 0.50f, 0.35f);
        glueSuite.processSamplePair(l, r, 0.75f, 0.60f, 0.55f);
        regSuite.processSamplePair(l, r, 330.0f, true, 0.70f, 0.65f, 0.55f);
        psychoSuite.processSamplePair(l, r, 0.55f, 0.65f, 0.40f);
        deliverySuite.processSamplePair(l, r, 0.45f, 0.65f, true);

        coachSuite.getVowelMapper().analyzeMonoSample(0.5f * (l + r));
        if ((n & 255) == 0)
        {
            coachSuite.getCentHistogram().pushPitchFrame(3.2f * std::sin(t * 35.0f), 330.0f, true);
            coachSuite.getVibratoBiometrics().pushPitchDeviationFrame(22.0f * std::sin(kTwoPi * 5.6f * t), true);
        }

        if (!std::isfinite(l) || !std::isfinite(r))
            allFinite = false;
    }

    coachSuite.getTakeScorer().updateEvaluation(
        coachSuite.getCentHistogram().getPitchLockScore01(),
        91.5f, 11.2f,
        coachSuite.getCentHistogram().getMeanCentsBias());

    if (!checkCondition(allFinite, "Tape + MixGlue + Passaggio + Psychoacoustic + Delivery 24-bit Dither finite check"))
        return false;

    if (!checkCondition(glueSuite.getWeissDeEsser().getActiveDeEssDb() > 0.2f &&
                        regSuite.getPassaggioBridge().getBridgeActivation01() > 0.2f &&
                        coachSuite.getTakeScorer().getEvaluation().totalScore100 > 75.0f,
                        "Weiss Relative De-Esser, Passaggio Bridge & StudioSessionTakeScorer active verification"))
        return false;

    const auto& c0 = vg::console_presets::WorldClassConsoleCatalog::getSpecByIndex(0);
    const auto& c4 = vg::console_presets::WorldClassConsoleCatalog::getSpecByIndex(4);
    const auto& c9 = vg::console_presets::WorldClassConsoleCatalog::getSpecByIndex(9);
    const auto& c23 = vg::console_presets::WorldClassConsoleCatalog::getSpecByIndex(23);
    const auto morphed = vg::console_presets::WorldClassConsoleCatalog::interpolateFourConsoles(c0, c4, c9, c23, 0.5f, 0.5f);
    const int recIdx = vg::console_presets::WorldClassConsoleCatalog::recommendOptimalConsoleIndex(360.0f, 8.2f, 0.25f, false);

    if (!checkCondition(morphed.serbanParallelBus01 > 0.30f && morphed.serbanParallelBus01 < 0.80f && recIdx == 5,
                        "WorldClassConsoleCatalog 24-console bilinear morphing & optimal genre recommendation"))
        return false;

    return true;
}

bool testHorologyOffscreenRendering()
{
    juce::Image canvas(juce::Image::ARGB, 640, 360, true, juce::SoftwareImageType());
    {
        juce::Graphics g(canvas);

        const auto fullBounds = canvas.getBounds().toFloat();
        g.fillAll(vg::horology_ui::MidnightLanternPalette::canopyAbyss());
        vg::horology_ui::HorologyGuillocheRenderer::drawAtmosphericLanternBokeh(g, fullBounds, 0.75f, 1);

        vg::horology_deck::YoimatsuriParticleFieldEngine particleField;
        particleField.stepPhysics(0.016f, 0.72f, 0.85f);
        particleField.drawField(g, fullBounds);

        vg::horology_ui::HorologyGuillocheRenderer::drawMasterChronoDial(
            g, { 76.0f, 76.0f, 168.0f, 168.0f }, 0.618f, "MASTER", "61.8%",
            vg::horology_ui::MidnightLanternPalette::lanternAmber(), true, false, 0.35f);

        vg::horology_deck::TourbillonBalanceWheelVisualizer::drawTourbillonCage(
            g, { 420.0f, 120.0f }, 48.0f, 1.1f, 0.92f, 0.85f);

        vg::horology_deck::AnalogVuBallisticMeterRenderer vuMeter;
        for (int i = 0; i < 30; ++i)
            vuMeter.updateBallistics(0.76f, 0.85f, 0.016f);
        vuMeter.drawVuMeter(g, { 320.0f, 200.0f, 280.0f, 80.0f }, "GRAMMY MASTER VU");

        vg::horology_deck::GrammyMasteringChainFlowStrip::ChainStageMetrics metrics{};
        vg::horology_deck::GrammyMasteringChainFlowStrip::drawChainStrip(
            g, { 20.0f, 300.0f, 600.0f, 44.0f }, metrics);
    }

    // 描画結果が空でないことをピクセル輝度で確認
    const auto centerPixel = canvas.getPixelAt(160, 160);
    return checkCondition(centerPixel.getAlpha() == 255 &&
                          (centerPixel.getRed() + centerPixel.getGreen() + centerPixel.getBlue()) > 10,
                          "HorologyGuillocheRenderer & YoimatsuriParticleFieldEngine offscreen ARGB rendering");
}

} // namespace

int main()
{
    std::printf("=== VocalGzzio Horology & Science Audit Suite ===\n");
    bool ok = true;
    ok = testPitchAndHarmonyScience() && ok;
    ok = testStudioAcousticRoomEngine() && ok;
    ok = testTapeGluePsychoAndDeliverySuites() && ok;
    ok = testHorologyOffscreenRendering() && ok;

    if (!ok)
    {
        std::fprintf(stderr, "=== SOME SCIENCE / HOROLOGY AUDIT TESTS FAILED ===\n");
        return EXIT_FAILURE;
    }

    std::printf("=== ALL SCIENCE & HOROLOGY AUDIT TESTS PASSED 100%% ===\n");
    return EXIT_SUCCESS;
}
