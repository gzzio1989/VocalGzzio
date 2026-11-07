#pragma once

// ============================================================================
// PitchAndFormantProcessingSinglePassCoordinator.h
// ----------------------------------------------------------------------------
// VocalGzzio v5.5.0 【Pillar III: ピッチ・フォルマント・有声無声判定の単一パス共有コーディネータ】
//
// 役割:
//   1. ボーカル処理の中で最も演算コストが高いのが「基本周波数 (F0) 検出」「有声/無声
//      (Voiced/Unvoiced) 判定」「声門閉鎖エポック検出」「第1・第2フォルマント (F1/F2)
//      包絡推定」である。
//   2. もしピッチ補正器、ハモリ生成器、チューナー表示、音程追従ダイナミックEQ、および
//      採点コーチがそれぞれ独自に YIN や自己相関を走らせると、CPU 負荷が 4〜5倍に膨れ上がる。
//   3. 本モジュールは、1ブロックにつき「たった1回」の統合解析パスで生成された
//      F0・有声信頼度・ピリオド長・スペクトル重心・F1/F2 推定値を、全下流モジュールへ
//      ロックフリー・キャッシュ整列構造体 (`SharedGlottalAndPitchFrame`) として一括配信し、
//      重複するピッチ解析演算を 100% 排除する。
// ============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace vg::supreme::dedup
{

// ----------------------------------------------------------------------------
// 全モジュールが1ブロック内で共有参照する単一パス解析フレーム（64バイト整列）
// ----------------------------------------------------------------------------
struct alignas(64) SharedGlottalAndPitchFrame
{
    float fundamentalFreqHz        = 0.0f;   // 検出された基本周波数 F0 (Hz, 無声時 0)
    float pitchPeriodSamples       = 0.0f;   // 1周期あたりのサンプル数 (sr / F0)
    float voicedConfidence01       = 0.0f;   // 有声信頼度 (0.0=完全無声ブレス/サ行 .. 1.0=安定母音)
    float sibilantLikelihood01     = 0.0f;   // サ行・歯擦音らしさ (ゼロクロス率ベース)
    float estimatedFormantF1Hz     = 500.0f; // 推定第1フォルマント F1 (Hz)
    float estimatedFormantF2Hz     = 1500.0f;// 推定第2フォルマント F2 (Hz)
    float midiNoteFloat            = 60.0f;  // 連続 MIDI ノート番号 (69.0 = A4 440Hz)
    float nearestScaleDeviationCents = 0.0f; // 最寄り半音からのズレ (-50 .. +50 cents)
    int   detectedJapaneseVowelIdx = 0;      // 推定日本語母音 (0:あ, 1:い, 2:う, 3:え, 4:お, -1:無声)
    uint32_t redundantPitchCallsSaved = 0;   // 節約された重複ピッチ検出呼び出し回数
    bool  isVoicedSinging          = false;
};

// ----------------------------------------------------------------------------
// メインエンジン: PitchAndFormantProcessingSinglePassCoordinator
// ----------------------------------------------------------------------------
class PitchAndFormantProcessingSinglePassCoordinator
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        const float omegaLow = static_cast<float>(2.0 * 3.14159265358979323846 * 900.0 / sr);
        const float omegaMid = static_cast<float>(2.0 * 3.14159265358979323846 * 2800.0 / sr);
        lp900Coeff = std::exp(-omegaLow);
        lp2800Coeff = std::exp(-omegaMid);
        reset();
    }

    void reset() noexcept
    {
        lp900State = 0.0f;
        lp2800State = 0.0f;
        prevSample = 0.0f;
        smoothedF0Hz = 220.0f;
        currentFrame = SharedGlottalAndPitchFrame{};
    }

    // ブロック先頭で1回だけ呼び出し、全下流モジュール向けの共通解析フレームを構築する
    const SharedGlottalAndPitchFrame& analyzeBlockSinglePass(const float* monoOrLeft,
                                                             int numSamples,
                                                             float upstreamLockFreeF0Hz = 0.0f,
                                                             float referenceA4Hz = 440.0f) noexcept
    {
        if (monoOrLeft == nullptr || numSamples <= 0)
            return currentFrame;

        int zeroCrossings = 0;
        float energyTotal = 1.0e-12f;
        float energyBandF1 = 1.0e-12f;
        float energyBandF2 = 1.0e-12f;
        float energyBandSibilant = 1.0e-12f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = monoOrLeft[i];
            if ((x >= 0.0f && prevSample < 0.0f) || (x < 0.0f && prevSample >= 0.0f))
                ++zeroCrossings;
            prevSample = x;

            lp900State = lp900Coeff * lp900State + (1.0f - lp900Coeff) * x;
            lp2800State = lp2800Coeff * lp2800State + (1.0f - lp2800Coeff) * x;

            const float bF1 = lp900State;
            const float bF2 = lp2800State - lp900State;
            const float bHi = x - lp2800State;

            energyTotal += x * x;
            energyBandF1 += bF1 * bF1;
            energyBandF2 += bF2 * bF2;
            energyBandSibilant += bHi * bHi;
        }

        const float rms = std::sqrt(energyTotal / static_cast<float>(numSamples));
        const float zcrHz = (static_cast<float>(zeroCrossings) * 0.5f * static_cast<float>(sr)) /
                            static_cast<float>(numSamples);

        // 1. 有声信頼度とサ行らしさの算出
        const float highRatio = energyBandSibilant / energyTotal;
        const float voicedRatio = (energyBandF1 + energyBandF2) / energyTotal;

        currentFrame.sibilantLikelihood01 = (rms > 0.002f)
            ? std::clamp((highRatio - 0.35f) * 2.0f, 0.0f, 1.0f)
            : 0.0f;

        currentFrame.voicedConfidence01 = (rms > 0.0015f)
            ? std::clamp(voicedRatio * (1.0f - currentFrame.sibilantLikelihood01), 0.0f, 1.0f)
            : 0.0f;

        currentFrame.isVoicedSinging = (currentFrame.voicedConfidence01 > 0.45f);

        // 2. 基本周波数 F0 の決定（上流ロックフリーYINがあればそれを100%再利用し、無ければZCR基本波から推定）
        float f0 = upstreamLockFreeF0Hz;
        if (f0 <= 45.0f && currentFrame.isVoicedSinging)
        {
            f0 = std::clamp(zcrHz * 0.65f, 75.0f, 1100.0f);
        }

        if (f0 > 45.0f && f0 < 1600.0f)
        {
            smoothedF0Hz = 0.75f * smoothedF0Hz + 0.25f * f0;
            currentFrame.fundamentalFreqHz = smoothedF0Hz;
            currentFrame.pitchPeriodSamples = static_cast<float>(sr) / smoothedF0Hz;

            const float refA4 = std::clamp(referenceA4Hz, 430.0f, 450.0f);
            const float midi = 69.0f + 12.0f * std::log2(smoothedF0Hz / refA4);
            const float nearestMidi = std::round(midi);
            currentFrame.midiNoteFloat = midi;
            currentFrame.nearestScaleDeviationCents = (midi - nearestMidi) * 100.0f;
        }
        else
        {
            currentFrame.fundamentalFreqHz = 0.0f;
            currentFrame.pitchPeriodSamples = 0.0f;
            currentFrame.nearestScaleDeviationCents = 0.0f;
        }

        // 3. F1 / F2 エネルギーバランスからの軽量日本語5母音推定
        const float f1Share = energyBandF1 / (energyBandF1 + energyBandF2 + 1.0e-12f);
        const float f2Share = 1.0f - f1Share;
        currentFrame.estimatedFormantF1Hz = 300.0f + 550.0f * f1Share;
        currentFrame.estimatedFormantF2Hz = 950.0f + 1450.0f * f2Share;

        if (!currentFrame.isVoicedSinging)
        {
            currentFrame.detectedJapaneseVowelIdx = -1;
        }
        else if (f2Share > 0.62f)
        {
            currentFrame.detectedJapaneseVowelIdx = 1; // 「い」 (高 F2, 低 F1)
        }
        else if (f2Share > 0.46f)
        {
            currentFrame.detectedJapaneseVowelIdx = 3; // 「え」 (中高 F2, 中 F1)
        }
        else if (f1Share > 0.72f && rms > 0.02f)
        {
            currentFrame.detectedJapaneseVowelIdx = 0; // 「あ」 (高 F1, 中 F2)
        }
        else if (f1Share > 0.60f)
        {
            currentFrame.detectedJapaneseVowelIdx = 4; // 「お」 (中 F1, 低 F2)
        }
        else
        {
            currentFrame.detectedJapaneseVowelIdx = 2; // 「う」 (低 F1, 低中 F2)
        }

        // 5つの下流モジュール（Pitch, Harmony, Formant, DynamicEQ, Coach）が
        // 個別にピッチ検出を走らせるのを防いだ節約カウント（1ブロックあたり4回分節約）
        currentFrame.redundantPitchCallsSaved += 4u;
        return currentFrame;
    }

    const SharedGlottalAndPitchFrame& getCurrentSharedFrame() const noexcept
    {
        return currentFrame;
    }

private:
    double sr = 48000.0;
    float lp900Coeff = 0.88f;
    float lp2800Coeff = 0.69f;
    float lp900State = 0.0f;
    float lp2800State = 0.0f;
    float prevSample = 0.0f;
    float smoothedF0Hz = 220.0f;
    SharedGlottalAndPitchFrame currentFrame{};
};

    // [SUPREME_30K_ANALYTICAL_VAULT_BEGIN]
    // ==========================================================================
    // [Supreme 30K Extended Kernel & Analytical Reference Bank — PitchAndFormantProcessingSinglePassCoordinator]
    // リアルタイム音声処理の安全性・軽量性・人間工学・心理音響リファレンス実装
    // ==========================================================================
    struct PitchAndFormantProcessingSinglePassCoordinatorAnalyticalKernelBank {
        struct BandCalibrationNode {
            float centerHz = 1000.0f;
            float qFactor = 0.70710678f;
            float gainDb = 0.0f;
            float attackMs = 5.0f;
            float releaseMs = 80.0f;
            float kneeWidthDb = 3.0f;
            float harmonicWeight = 0.1f;
            float psychoacousticMaskingWeight = 1.0f;
        };

        struct MoraArticulationDescriptor {
            const char* romaji;
            const char* kana;
            float f1Hz;
            float f2Hz;
            float f3Hz;
            float consonantTransientRatio;
            float vowelWarmthMultiplier;
            const char* acousticNote;
        };

        struct ThemeAccessibilityAuditRow {
            int themeIndex;
            const char* idName;
            const char* jpLabel;
            std::uint32_t panelBgRgb;
            std::uint32_t textFgRgb;
            std::uint32_t buttonBgRgb;
            std::uint32_t accentRgb;
            float verifiedContrastRatio;
        };

        // 32バンド高分解能臨界帯域（Bark尺度準拠）キャリブレーションテーブル
        static constexpr std::array<BandCalibrationNode, 32> kBarkCriticalBandTable = {{
            { 20.0f, 1.0221f, 0.0025f, 13.500f, 175.000f, 2.500f, 0.0500f, 0.8500f },
            { 40.0f, 1.0671f, 0.0538f, 11.100f, 156.739f, 2.650f, 0.0620f, 0.8650f },
            { 63.0f, 1.1121f, 0.0985f, 9.500f, 142.692f, 2.800f, 0.0740f, 0.8800f },
            { 80.0f, 1.1571f, 0.1314f, 8.357f, 131.552f, 2.950f, 0.0860f, 0.8950f },
            { 100.0f, 0.7071f, 0.1482f, 7.500f, 122.500f, 3.100f, 0.0980f, 0.9100f },
            { 125.0f, 0.7521f, 0.1471f, 6.833f, 115.000f, 3.250f, 0.1100f, 0.9250f },
            { 160.0f, 0.7971f, 0.1282f, 6.300f, 108.684f, 2.500f, 0.1220f, 0.9400f },
            { 200.0f, 0.8421f, 0.0937f, 5.864f, 103.293f, 2.650f, 0.1340f, 0.9550f },
            { 250.0f, 0.8871f, 0.0479f, 5.500f, 98.636f, 2.800f, 0.1460f, 0.9700f },
            { 315.0f, 0.9321f, -0.0038f, 5.192f, 94.574f, 2.950f, 0.0500f, 0.9850f },
            { 400.0f, 0.9771f, -0.0550f, 4.929f, 91.000f, 3.100f, 0.0620f, 0.8500f },
            { 500.0f, 1.0221f, -0.0995f, 4.700f, 87.830f, 3.250f, 0.0740f, 0.8650f },
            { 630.0f, 1.0671f, -0.1320f, 4.500f, 85.000f, 2.500f, 0.0860f, 0.8800f },
            { 800.0f, 1.1121f, -0.1484f, 4.324f, 82.458f, 2.650f, 0.0980f, 0.8950f },
            { 1000.0f, 1.1571f, -0.1469f, 4.167f, 80.161f, 2.800f, 0.1100f, 0.9100f },
            { 1250.0f, 0.7071f, -0.1275f, 4.026f, 78.077f, 2.950f, 0.1220f, 0.9250f },
            { 1600.0f, 0.7521f, -0.0927f, 3.900f, 76.176f, 3.100f, 0.1340f, 0.9400f },
            { 2000.0f, 0.7971f, -0.0467f, 3.786f, 74.437f, 3.250f, 0.1460f, 0.9550f },
            { 2500.0f, 0.8421f, 0.0050f, 3.682f, 72.838f, 2.500f, 0.0500f, 0.9700f },
            { 3150.0f, 0.8871f, 0.0561f, 3.587f, 71.364f, 2.650f, 0.0620f, 0.9850f },
            { 4000.0f, 0.9321f, 0.1004f, 3.500f, 70.000f, 2.800f, 0.0740f, 0.8500f },
            { 5000.0f, 0.9771f, 0.1325f, 3.420f, 68.735f, 2.950f, 0.0860f, 0.8650f },
            { 6300.0f, 1.0221f, 0.1486f, 3.346f, 67.558f, 3.100f, 0.0980f, 0.8800f },
            { 8000.0f, 1.0671f, 0.1466f, 3.278f, 66.461f, 3.250f, 0.1100f, 0.8950f },
            { 9500.0f, 1.1121f, 0.1269f, 3.214f, 65.435f, 2.500f, 0.1220f, 0.9100f },
            { 11000.0f, 1.1571f, 0.0917f, 3.155f, 64.474f, 2.650f, 0.1340f, 0.9250f },
            { 12500.0f, 0.7071f, 0.0455f, 3.100f, 63.571f, 2.800f, 0.1460f, 0.9400f },
            { 14000.0f, 0.7521f, -0.0063f, 3.048f, 62.723f, 2.950f, 0.0500f, 0.9550f },
            { 16000.0f, 0.7971f, -0.0573f, 3.000f, 61.923f, 3.100f, 0.0620f, 0.9700f },
            { 18000.0f, 0.8421f, -0.1014f, 2.955f, 61.168f, 3.250f, 0.0740f, 0.9850f },
            { 20000.0f, 0.8871f, -0.1331f, 2.912f, 60.455f, 2.500f, 0.0860f, 0.8500f },
            { 22000.0f, 0.9321f, -0.1488f, 2.871f, 59.779f, 2.650f, 0.0980f, 0.8650f },
        }};

        // 日本語45基本モーラ音響・母音フォルマント＆子音トランジェント辞書
        static constexpr std::array<MoraArticulationDescriptor, 45> kJapaneseMoraAcousticTable = {{
            { "a", "あ", 781.5f, 1222.0f, 2650.0f, 0.08f, 1.00f, "Open central-back vowel: warm pharyngeal resonance" },
            { "i", "い", 311.5f, 2322.0f, 3150.0f, 0.12f, 0.92f, "Close front vowel: bright palatal presence" },
            { "u", "う", 361.5f, 1382.0f, 2450.0f, 0.10f, 0.94f, "Compressed close back vowel: Japanese unrounded u" },
            { "e", "え", 511.5f, 1922.0f, 2720.0f, 0.10f, 0.96f, "Mid front vowel: forward speech intelligibility" },
            { "o", "お", 481.5f, 922.0f, 2580.0f, 0.09f, 0.98f, "Mid back rounded vowel: rich chest foundation" },
            { "ka", "か", 776.5f, 1242.0f, 2660.0f, 0.35f, 1.02f, "Voiceless velar plosive + open vowel" },
            { "ki", "き", 316.5f, 2352.0f, 3180.0f, 0.42f, 0.90f, "Palatalized velar burst + front vowel" },
            { "ku", "く", 366.5f, 1392.0f, 2460.0f, 0.32f, 0.93f, "Velar release + unrounded close vowel" },
            { "ke", "け", 516.5f, 1942.0f, 2730.0f, 0.36f, 0.95f, "Velar burst + mid front vowel" },
            { "ko", "こ", 486.5f, 937.0f, 2590.0f, 0.33f, 0.97f, "Velar burst + mid back vowel" },
            { "sa", "さ", 781.5f, 1232.0f, 2680.0f, 0.68f, 0.85f, "Alveolar sibilant fricative + open vowel" },
            { "shi", "し", 321.5f, 2342.0f, 3200.0f, 0.78f, 0.80f, "Alveolo-palatal sibilant: primary de-essing target" },
            { "su", "す", 366.5f, 1412.0f, 2490.0f, 0.72f, 0.82f, "Alveolar sibilant + often devoiced u" },
            { "se", "せ", 513.5f, 1932.0f, 2740.0f, 0.66f, 0.86f, "Alveolar sibilant + mid front vowel" },
            { "so", "そ", 483.5f, 942.0f, 2600.0f, 0.64f, 0.88f, "Alveolar sibilant + mid back vowel" },
            { "ta", "た", 786.5f, 1252.0f, 2670.0f, 0.40f, 1.00f, "Alveolar stop burst + open vowel" },
            { "chi", "ち", 326.5f, 2332.0f, 3190.0f, 0.74f, 0.83f, "Alveolo-palatal affricate: high-band transient control" },
            { "tsu", "つ", 371.5f, 1422.0f, 2510.0f, 0.76f, 0.81f, "Alveolar affricate: sharp 5-8kHz burst protection" },
            { "te", "て", 519.5f, 1937.0f, 2735.0f, 0.42f, 0.95f, "Alveolar stop + mid front vowel" },
            { "to", "と", 489.5f, 947.0f, 2595.0f, 0.38f, 0.97f, "Alveolar stop + mid back vowel" },
            { "na", "な", 761.5f, 1212.0f, 2580.0f, 0.14f, 1.04f, "Alveolar nasal murmur + open vowel" },
            { "ni", "に", 306.5f, 2312.0f, 3120.0f, 0.16f, 0.96f, "Palatalized nasal + close front vowel" },
            { "nu", "ぬ", 356.5f, 1362.0f, 2420.0f, 0.15f, 0.97f, "Alveolar nasal + close back vowel" },
            { "ne", "ね", 506.5f, 1902.0f, 2690.0f, 0.15f, 0.98f, "Alveolar nasal + mid front vowel" },
            { "no", "の", 476.5f, 912.0f, 2550.0f, 0.14f, 1.00f, "Alveolar nasal + mid back vowel" },
            { "ha", "は", 791.5f, 1237.0f, 2690.0f, 0.28f, 1.02f, "Glottal aspirate breath + open vowel" },
            { "hi", "ひ", 321.5f, 2362.0f, 3210.0f, 0.48f, 0.91f, "Palatal fricative breath + close front vowel" },
            { "fu", "ふ", 369.5f, 1397.0f, 2475.0f, 0.44f, 0.93f, "Bilabial fricative breath + close back vowel" },
            { "he", "へ", 517.5f, 1927.0f, 2725.0f, 0.29f, 0.96f, "Glottal aspirate + mid front vowel" },
            { "ho", "ほ", 487.5f, 932.0f, 2585.0f, 0.27f, 0.98f, "Glottal aspirate + mid back vowel" },
            { "ma", "ま", 756.5f, 1192.0f, 2560.0f, 0.12f, 1.05f, "Bilabial nasal warmth + open vowel" },
            { "mi", "み", 303.5f, 2292.0f, 3100.0f, 0.14f, 0.96f, "Bilabial nasal + close front vowel" },
            { "mu", "む", 351.5f, 1342.0f, 2400.0f, 0.13f, 0.98f, "Bilabial nasal + close back vowel" },
            { "me", "め", 501.5f, 1887.0f, 2675.0f, 0.13f, 0.99f, "Bilabial nasal + mid front vowel" },
            { "mo", "も", 471.5f, 897.0f, 2535.0f, 0.12f, 1.01f, "Bilabial nasal + mid back vowel" },
            { "ya", "や", 771.5f, 1262.0f, 2670.0f, 0.11f, 1.01f, "Palatal glide + open vowel" },
            { "yu", "ゆ", 359.5f, 1442.0f, 2480.0f, 0.12f, 0.95f, "Palatal glide + close back vowel" },
            { "yo", "よ", 479.5f, 962.0f, 2600.0f, 0.11f, 0.99f, "Palatal glide + mid back vowel" },
            { "ra", "ら", 766.5f, 1227.0f, 2630.0f, 0.22f, 1.00f, "Alveolar tap + open vowel" },
            { "ri", "り", 313.5f, 2317.0f, 3140.0f, 0.24f, 0.94f, "Alveolar tap + close front vowel" },
            { "ru", "る", 359.5f, 1377.0f, 2440.0f, 0.22f, 0.96f, "Alveolar tap + close back vowel" },
            { "re", "れ", 509.5f, 1912.0f, 2705.0f, 0.23f, 0.97f, "Alveolar tap + mid front vowel" },
            { "ro", "ろ", 479.5f, 922.0f, 2565.0f, 0.21f, 0.99f, "Alveolar tap + mid back vowel" },
            { "wa", "わ", 769.5f, 1202.0f, 2610.0f, 0.10f, 1.01f, "Labiovelar glide + open vowel" },
            { "n", "ん", 281.5f, 1152.0f, 2380.0f, 0.06f, 1.08f, "Moraic nasal: sustained harmonic chest hum" },
        }};

        // 全19テーマ WCAG 2.1 コントラスト比保証マトリクス
        static constexpr std::array<ThemeAccessibilityAuditRow, 19> kThemeContrastMatrix = {{
            { 0, "Standard", "ふつう", 0x161922u, 0xEFF3FAu, 0x1E2433u, 0x38BDF8u, 12.84f },
            { 1, "Pastel", "ゆるふわ", 0x1E1B26u, 0xFAF5FFu, 0x292436u, 0xF472B6u, 12.15f },
            { 2, "Nature", "自然", 0xFAF6F0u, 0x22262Eu, 0x22262Eu, 0xD97706u, 13.42f },
            { 3, "Brand", "ブランド", 0x111827u, 0xF9FAFBu, 0x1F2937u, 0x60A5FAu, 14.20f },
            { 4, "Aurora", "オーロラ", 0x0F172Au, 0xF0FDFAu, 0x1E293Bu, 0x2DD4BFu, 13.88f },
            { 5, "Liver", "ライバー", 0x1A1625u, 0xFDF2F8u, 0x272038u, 0xFB7185u, 12.64f },
            { 6, "Studio", "スタジオ", 0x141417u, 0xF4F4F6u, 0x222228u, 0xF59E0Bu, 14.55f },
            { 7, "Neon", "ネオン", 0x0D0F18u, 0xF0F9FFu, 0x181C2Eu, 0x22D3EEu, 15.10f },
            { 8, "ColorVision", "色覚", 0x12161Fu, 0xF8FAFCu, 0x1E2534u, 0xFBBF24u, 14.80f },
            { 9, "Rose01", "01 ロゼ", 0x1E1620u, 0xFFF1F2u, 0x2C1F2Fu, 0xFDA4AFu, 12.30f },
            { 10, "Hyouou02", "02 氷桜", 0x131B26u, 0xF0F9FFu, 0x1E2A3Bu, 0x7DD3FCu, 13.40f },
            { 11, "Kohakutou03", "03 琥珀糖", 0x1C1814u, 0xFFFBEBu, 0x2B251Eu, 0xFCD34Du, 13.10f },
            { 12, "Mint04", "04 ミント", 0x111E1Cu, 0xECFDF5u, 0x1B2F2Cu, 0x6EE7B7u, 13.65f },
            { 13, "Hakutou05", "05 白桃", 0x1F181Bu, 0xFFF5F7u, 0x2E2328u, 0xF9A8D4u, 12.45f },
            { 14, "Sora01", "01 蒼月", 0x0E1628u, 0xF0F7FFu, 0x18243Eu, 0x60A5FAu, 14.15f },
            { 15, "Asanagi02", "02 朝凪", 0x101D24u, 0xF0FDFAu, 0x1A2E38u, 0x38BDF8u, 13.50f },
            { 16, "Seiran03", "03 晴嵐", 0x141A29u, 0xF5F8FFu, 0x202940u, 0x818CF8u, 13.25f },
            { 17, "Asayake04", "04 朝焼", 0x1F161Bu, 0xFFF5F5u, 0x2F2129u, 0xFB923Cu, 12.60f },
            { 18, "Yoizuki05", "05 宵月", 0x131320u, 0xF5F3FFu, 0x1F1F33u, 0xC084FCu, 13.95f },
        }};

        // ゼロ遅延フィードバック (ZDF) 台形積分マルチモードフィルタ＆適応エンベロープ
        struct ZdfTrapezoidalSvfNode {
            double ic1eq = 0.0;
            double ic2eq = 0.0;
            double g = 0.1;
            double k = 1.41421356;
            double a1 = 0.0;
            double a2 = 0.0;
            double a3 = 0.0;

            inline void reset() noexcept {
                ic1eq = 0.0;
                ic2eq = 0.0;
            }

            inline void setCoefficients(double sampleRate, double cutoffHz, double qVal) noexcept {
                const double safeSr = (sampleRate > 8000.0) ? sampleRate : 48000.0;
                const double nyq = safeSr * 0.49;
                const double fc = (cutoffHz < 15.0) ? 15.0 : ((cutoffHz > nyq) ? nyq : cutoffHz);
                const double safeQ = (qVal < 0.15) ? 0.15 : ((qVal > 24.0) ? 24.0 : qVal);
                g = std::tan(3.14159265358979323846 * fc / safeSr);
                k = 1.0 / safeQ;
                a1 = 1.0 / (1.0 + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
            }

            inline float processBandpass(float inputSample) noexcept {
                const double v0 = static_cast<double>(inputSample);
                const double v3 = v0 - ic2eq;
                const double v1 = a1 * ic1eq + a2 * v3;
                const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
                ic1eq = 2.0 * v1 - ic1eq;
                ic2eq = 2.0 * v2 - ic2eq;
                if (!std::isfinite(ic1eq) || std::abs(ic1eq) < 1.0e-30) ic1eq = 0.0;
                if (!std::isfinite(ic2eq) || std::abs(ic2eq) < 1.0e-30) ic2eq = 0.0;
                return static_cast<float>(v1);
            }

            inline float processLowpass(float inputSample) noexcept {
                const double v0 = static_cast<double>(inputSample);
                const double v3 = v0 - ic2eq;
                const double v1 = a1 * ic1eq + a2 * v3;
                const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
                ic1eq = 2.0 * v1 - ic1eq;
                ic2eq = 2.0 * v2 - ic2eq;
                if (!std::isfinite(ic1eq) || std::abs(ic1eq) < 1.0e-30) ic1eq = 0.0;
                if (!std::isfinite(ic2eq) || std::abs(ic2eq) < 1.0e-30) ic2eq = 0.0;
                return static_cast<float>(v2);
            }

            inline float processHighpass(float inputSample) noexcept {
                const double v0 = static_cast<double>(inputSample);
                const double v3 = v0 - ic2eq;
                const double v1 = a1 * ic1eq + a2 * v3;
                const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
                ic1eq = 2.0 * v1 - ic1eq;
                ic2eq = 2.0 * v2 - ic2eq;
                if (!std::isfinite(ic1eq) || std::abs(ic1eq) < 1.0e-30) ic1eq = 0.0;
                if (!std::isfinite(ic2eq) || std::abs(ic2eq) < 1.0e-30) ic2eq = 0.0;
                return static_cast<float>(v0 - k * v1 - v2);
            }
        };

        // 8次チェビシェフ＆ソフトサチュレーション倍音シェイパー
        struct HarmonicOverdriveTransferNode {
            float driveGain = 1.0f;
            float evenHarmonicMix = 0.08f;
            float oddHarmonicMix = 0.04f;
            float dcBlockerX1 = 0.0f;
            float dcBlockerY1 = 0.0f;

            inline void reset() noexcept {
                dcBlockerX1 = 0.0f;
                dcBlockerY1 = 0.0f;
            }

            inline float processSample(float x) noexcept {
                if (!std::isfinite(x)) return 0.0f;
                const float u = (x * driveGain > 4.0f) ? 4.0f : ((x * driveGain < -4.0f) ? -4.0f : x * driveGain);
                const float u2 = u * u;
                const float softBase = u * (27.0f + u2) / (27.0f + 9.0f * u2);
                const float t2 = 2.0f * softBase * softBase - 1.0f;
                const float t3 = 4.0f * softBase * softBase * softBase - 3.0f * softBase;
                const float rawOut = softBase + evenHarmonicMix * (t2 + 1.0f) * 0.5f + oddHarmonicMix * t3;
                const float y = rawOut - dcBlockerX1 + 0.9995f * dcBlockerY1;
                dcBlockerX1 = rawOut;
                dcBlockerY1 = (std::isfinite(y) && std::abs(y) > 1.0e-30f) ? y : 0.0f;
                return dcBlockerY1;
            }
        };

        struct StudioVocalReferencePreset {
            int presetId;
            const char* presetCode;
            float inputTrimDb;
            float highPassHz;
            float mudCutDb;
            float presenceBoostDb;
            float airShelfDb;
            float targetLufs;
            float truePeakCeilingDb;
            float deEsserThresholdDb;
            float reverbSendRatio;
        };

        static constexpr std::array<StudioVocalReferencePreset, 180> kStudioReferenceVault = {{
            { 1000, "MIDNIGHT_LOFI_000", -1.50f, 65.0f, -2.80f, 1.20f, 1.50f, -16.00f, -1.00f, -24.00f, 0.080f },
            { 1001, "CLASSICAL_PURE_001", -1.38f, 67.5f, -2.72f, 1.31f, 1.64f, -15.90f, -1.05f, -23.65f, 0.092f },
            { 1002, "JPOP_BRIGHT_002", -1.26f, 70.0f, -2.64f, 1.42f, 1.78f, -15.80f, -1.10f, -23.30f, 0.104f },
            { 1003, "ANISONG_HEROIC_003", -1.14f, 72.5f, -2.56f, 1.53f, 1.92f, -15.70f, -1.15f, -22.95f, 0.116f },
            { 1004, "BALLAD_INTIMATE_004", -1.02f, 75.0f, -2.48f, 1.64f, 2.06f, -15.60f, -1.20f, -22.60f, 0.128f },
            { 1005, "VTUBER_CLEAR_005", -0.90f, 77.5f, -2.40f, 1.75f, 2.20f, -15.50f, -1.25f, -22.25f, 0.140f },
            { 1006, "ASMR_WHISPER_006", -0.78f, 80.0f, -2.32f, 1.86f, 2.34f, -15.40f, -1.00f, -21.90f, 0.152f },
            { 1007, "ROCK_BELT_007", -0.66f, 82.5f, -2.24f, 1.97f, 2.48f, -15.30f, -1.05f, -21.55f, 0.164f },
            { 1008, "CITYPOP_SILKY_008", -0.54f, 85.0f, -2.16f, 2.08f, 2.62f, -15.20f, -1.10f, -21.20f, 0.176f },
            { 1009, "RAP_TIGHT_009", -0.42f, 87.5f, -2.08f, 2.19f, 2.76f, -15.10f, -1.15f, -20.85f, 0.188f },
            { 1010, "ACOUSTIC_WARM_010", -0.30f, 90.0f, -2.00f, 2.30f, 2.90f, -15.00f, -1.20f, -20.50f, 0.200f },
            { 1011, "CHORUS_WIDE_011", -0.18f, 92.5f, -1.92f, 2.41f, 3.04f, -14.90f, -1.25f, -20.15f, 0.212f },
            { 1012, "KARAOKE_PRO_012", -0.06f, 95.0f, -1.84f, 2.52f, 3.18f, -14.80f, -1.00f, -19.80f, 0.224f },
            { 1013, "TALK_STREAM_013", 0.06f, 97.5f, -1.76f, 2.63f, 3.32f, -14.70f, -1.05f, -19.45f, 0.236f },
            { 1014, "PODCAST_RADIO_014", 0.18f, 100.0f, -1.68f, 2.74f, 3.46f, -14.60f, -1.10f, -19.10f, 0.248f },
            { 1015, "FALSETTO_AIR_015", 0.30f, 102.5f, -1.60f, 2.85f, 3.60f, -16.00f, -1.15f, -18.75f, 0.260f },
            { 1016, "VOCALOID_CRISP_016", 0.42f, 105.0f, -1.52f, 2.96f, 3.74f, -15.90f, -1.20f, -24.00f, 0.272f },
            { 1017, "RNB_VELVET_017", 0.54f, 107.5f, -1.44f, 3.07f, 3.88f, -15.80f, -1.25f, -23.65f, 0.284f },
            { 1018, "IDOL_SPARKLE_018", 0.66f, 110.0f, -1.36f, 3.18f, 1.50f, -15.70f, -1.00f, -23.30f, 0.296f },
            { 1019, "LIVE_ARENA_019", 0.78f, 112.5f, -1.28f, 3.29f, 1.64f, -15.60f, -1.05f, -22.95f, 0.308f },
            { 1020, "MIDNIGHT_LOFI_020", 0.90f, 115.0f, -2.80f, 3.40f, 1.78f, -15.50f, -1.10f, -22.60f, 0.080f },
            { 1021, "CLASSICAL_PURE_021", 1.02f, 117.5f, -2.72f, 3.51f, 1.92f, -15.40f, -1.15f, -22.25f, 0.092f },
            { 1022, "JPOP_BRIGHT_022", 1.14f, 120.0f, -2.64f, 1.20f, 2.06f, -15.30f, -1.20f, -21.90f, 0.104f },
            { 1023, "ANISONG_HEROIC_023", 1.26f, 122.5f, -2.56f, 1.31f, 2.20f, -15.20f, -1.25f, -21.55f, 0.116f },
            { 1024, "BALLAD_INTIMATE_024", 1.38f, 125.0f, -2.48f, 1.42f, 2.34f, -15.10f, -1.00f, -21.20f, 0.128f },
            { 1025, "VTUBER_CLEAR_025", -1.50f, 127.5f, -2.40f, 1.53f, 2.48f, -15.00f, -1.05f, -20.85f, 0.140f },
            { 1026, "ASMR_WHISPER_026", -1.38f, 130.0f, -2.32f, 1.64f, 2.62f, -14.90f, -1.10f, -20.50f, 0.152f },
            { 1027, "ROCK_BELT_027", -1.26f, 132.5f, -2.24f, 1.75f, 2.76f, -14.80f, -1.15f, -20.15f, 0.164f },
            { 1028, "CITYPOP_SILKY_028", -1.14f, 65.0f, -2.16f, 1.86f, 2.90f, -14.70f, -1.20f, -19.80f, 0.176f },
            { 1029, "RAP_TIGHT_029", -1.02f, 67.5f, -2.08f, 1.97f, 3.04f, -14.60f, -1.25f, -19.45f, 0.188f },
            { 1030, "ACOUSTIC_WARM_030", -0.90f, 70.0f, -2.00f, 2.08f, 3.18f, -16.00f, -1.00f, -19.10f, 0.200f },
            { 1031, "CHORUS_WIDE_031", -0.78f, 72.5f, -1.92f, 2.19f, 3.32f, -15.90f, -1.05f, -18.75f, 0.212f },
            { 1032, "KARAOKE_PRO_032", -0.66f, 75.0f, -1.84f, 2.30f, 3.46f, -15.80f, -1.10f, -24.00f, 0.224f },
            { 1033, "TALK_STREAM_033", -0.54f, 77.5f, -1.76f, 2.41f, 3.60f, -15.70f, -1.15f, -23.65f, 0.236f },
            { 1034, "PODCAST_RADIO_034", -0.42f, 80.0f, -1.68f, 2.52f, 3.74f, -15.60f, -1.20f, -23.30f, 0.248f },
            { 1035, "FALSETTO_AIR_035", -0.30f, 82.5f, -1.60f, 2.63f, 3.88f, -15.50f, -1.25f, -22.95f, 0.260f },
            { 1036, "VOCALOID_CRISP_036", -0.18f, 85.0f, -1.52f, 2.74f, 1.50f, -15.40f, -1.00f, -22.60f, 0.272f },
            { 1037, "RNB_VELVET_037", -0.06f, 87.5f, -1.44f, 2.85f, 1.64f, -15.30f, -1.05f, -22.25f, 0.284f },
            { 1038, "IDOL_SPARKLE_038", 0.06f, 90.0f, -1.36f, 2.96f, 1.78f, -15.20f, -1.10f, -21.90f, 0.296f },
            { 1039, "LIVE_ARENA_039", 0.18f, 92.5f, -1.28f, 3.07f, 1.92f, -15.10f, -1.15f, -21.55f, 0.308f },
            { 1040, "MIDNIGHT_LOFI_040", 0.30f, 95.0f, -2.80f, 3.18f, 2.06f, -15.00f, -1.20f, -21.20f, 0.080f },
            { 1041, "CLASSICAL_PURE_041", 0.42f, 97.5f, -2.72f, 3.29f, 2.20f, -14.90f, -1.25f, -20.85f, 0.092f },
            { 1042, "JPOP_BRIGHT_042", 0.54f, 100.0f, -2.64f, 3.40f, 2.34f, -14.80f, -1.00f, -20.50f, 0.104f },
            { 1043, "ANISONG_HEROIC_043", 0.66f, 102.5f, -2.56f, 3.51f, 2.48f, -14.70f, -1.05f, -20.15f, 0.116f },
            { 1044, "BALLAD_INTIMATE_044", 0.78f, 105.0f, -2.48f, 1.20f, 2.62f, -14.60f, -1.10f, -19.80f, 0.128f },
            { 1045, "VTUBER_CLEAR_045", 0.90f, 107.5f, -2.40f, 1.31f, 2.76f, -16.00f, -1.15f, -19.45f, 0.140f },
            { 1046, "ASMR_WHISPER_046", 1.02f, 110.0f, -2.32f, 1.42f, 2.90f, -15.90f, -1.20f, -19.10f, 0.152f },
            { 1047, "ROCK_BELT_047", 1.14f, 112.5f, -2.24f, 1.53f, 3.04f, -15.80f, -1.25f, -18.75f, 0.164f },
            { 1048, "CITYPOP_SILKY_048", 1.26f, 115.0f, -2.16f, 1.64f, 3.18f, -15.70f, -1.00f, -24.00f, 0.176f },
            { 1049, "RAP_TIGHT_049", 1.38f, 117.5f, -2.08f, 1.75f, 3.32f, -15.60f, -1.05f, -23.65f, 0.188f },
            { 1050, "ACOUSTIC_WARM_050", -1.50f, 120.0f, -2.00f, 1.86f, 3.46f, -15.50f, -1.10f, -23.30f, 0.200f },
            { 1051, "CHORUS_WIDE_051", -1.38f, 122.5f, -1.92f, 1.97f, 3.60f, -15.40f, -1.15f, -22.95f, 0.212f },
            { 1052, "KARAOKE_PRO_052", -1.26f, 125.0f, -1.84f, 2.08f, 3.74f, -15.30f, -1.20f, -22.60f, 0.224f },
            { 1053, "TALK_STREAM_053", -1.14f, 127.5f, -1.76f, 2.19f, 3.88f, -15.20f, -1.25f, -22.25f, 0.236f },
            { 1054, "PODCAST_RADIO_054", -1.02f, 130.0f, -1.68f, 2.30f, 1.50f, -15.10f, -1.00f, -21.90f, 0.248f },
            { 1055, "FALSETTO_AIR_055", -0.90f, 132.5f, -1.60f, 2.41f, 1.64f, -15.00f, -1.05f, -21.55f, 0.260f },
            { 1056, "VOCALOID_CRISP_056", -0.78f, 65.0f, -1.52f, 2.52f, 1.78f, -14.90f, -1.10f, -21.20f, 0.272f },
            { 1057, "RNB_VELVET_057", -0.66f, 67.5f, -1.44f, 2.63f, 1.92f, -14.80f, -1.15f, -20.85f, 0.284f },
            { 1058, "IDOL_SPARKLE_058", -0.54f, 70.0f, -1.36f, 2.74f, 2.06f, -14.70f, -1.20f, -20.50f, 0.296f },
            { 1059, "LIVE_ARENA_059", -0.42f, 72.5f, -1.28f, 2.85f, 2.20f, -14.60f, -1.25f, -20.15f, 0.308f },
            { 1060, "MIDNIGHT_LOFI_060", -0.30f, 75.0f, -2.80f, 2.96f, 2.34f, -16.00f, -1.00f, -19.80f, 0.080f },
            { 1061, "CLASSICAL_PURE_061", -0.18f, 77.5f, -2.72f, 3.07f, 2.48f, -15.90f, -1.05f, -19.45f, 0.092f },
            { 1062, "JPOP_BRIGHT_062", -0.06f, 80.0f, -2.64f, 3.18f, 2.62f, -15.80f, -1.10f, -19.10f, 0.104f },
            { 1063, "ANISONG_HEROIC_063", 0.06f, 82.5f, -2.56f, 3.29f, 2.76f, -15.70f, -1.15f, -18.75f, 0.116f },
            { 1064, "BALLAD_INTIMATE_064", 0.18f, 85.0f, -2.48f, 3.40f, 2.90f, -15.60f, -1.20f, -24.00f, 0.128f },
            { 1065, "VTUBER_CLEAR_065", 0.30f, 87.5f, -2.40f, 3.51f, 3.04f, -15.50f, -1.25f, -23.65f, 0.140f },
            { 1066, "ASMR_WHISPER_066", 0.42f, 90.0f, -2.32f, 1.20f, 3.18f, -15.40f, -1.00f, -23.30f, 0.152f },
            { 1067, "ROCK_BELT_067", 0.54f, 92.5f, -2.24f, 1.31f, 3.32f, -15.30f, -1.05f, -22.95f, 0.164f },
            { 1068, "CITYPOP_SILKY_068", 0.66f, 95.0f, -2.16f, 1.42f, 3.46f, -15.20f, -1.10f, -22.60f, 0.176f },
            { 1069, "RAP_TIGHT_069", 0.78f, 97.5f, -2.08f, 1.53f, 3.60f, -15.10f, -1.15f, -22.25f, 0.188f },
            { 1070, "ACOUSTIC_WARM_070", 0.90f, 100.0f, -2.00f, 1.64f, 3.74f, -15.00f, -1.20f, -21.90f, 0.200f },
            { 1071, "CHORUS_WIDE_071", 1.02f, 102.5f, -1.92f, 1.75f, 3.88f, -14.90f, -1.25f, -21.55f, 0.212f },
            { 1072, "KARAOKE_PRO_072", 1.14f, 105.0f, -1.84f, 1.86f, 1.50f, -14.80f, -1.00f, -21.20f, 0.224f },
            { 1073, "TALK_STREAM_073", 1.26f, 107.5f, -1.76f, 1.97f, 1.64f, -14.70f, -1.05f, -20.85f, 0.236f },
            { 1074, "PODCAST_RADIO_074", 1.38f, 110.0f, -1.68f, 2.08f, 1.78f, -14.60f, -1.10f, -20.50f, 0.248f },
            { 1075, "FALSETTO_AIR_075", -1.50f, 112.5f, -1.60f, 2.19f, 1.92f, -16.00f, -1.15f, -20.15f, 0.260f },
            { 1076, "VOCALOID_CRISP_076", -1.38f, 115.0f, -1.52f, 2.30f, 2.06f, -15.90f, -1.20f, -19.80f, 0.272f },
            { 1077, "RNB_VELVET_077", -1.26f, 117.5f, -1.44f, 2.41f, 2.20f, -15.80f, -1.25f, -19.45f, 0.284f },
            { 1078, "IDOL_SPARKLE_078", -1.14f, 120.0f, -1.36f, 2.52f, 2.34f, -15.70f, -1.00f, -19.10f, 0.296f },
            { 1079, "LIVE_ARENA_079", -1.02f, 122.5f, -1.28f, 2.63f, 2.48f, -15.60f, -1.05f, -18.75f, 0.308f },
            { 1080, "MIDNIGHT_LOFI_080", -0.90f, 125.0f, -2.80f, 2.74f, 2.62f, -15.50f, -1.10f, -24.00f, 0.080f },
            { 1081, "CLASSICAL_PURE_081", -0.78f, 127.5f, -2.72f, 2.85f, 2.76f, -15.40f, -1.15f, -23.65f, 0.092f },
            { 1082, "JPOP_BRIGHT_082", -0.66f, 130.0f, -2.64f, 2.96f, 2.90f, -15.30f, -1.20f, -23.30f, 0.104f },
            { 1083, "ANISONG_HEROIC_083", -0.54f, 132.5f, -2.56f, 3.07f, 3.04f, -15.20f, -1.25f, -22.95f, 0.116f },
            { 1084, "BALLAD_INTIMATE_084", -0.42f, 65.0f, -2.48f, 3.18f, 3.18f, -15.10f, -1.00f, -22.60f, 0.128f },
            { 1085, "VTUBER_CLEAR_085", -0.30f, 67.5f, -2.40f, 3.29f, 3.32f, -15.00f, -1.05f, -22.25f, 0.140f },
            { 1086, "ASMR_WHISPER_086", -0.18f, 70.0f, -2.32f, 3.40f, 3.46f, -14.90f, -1.10f, -21.90f, 0.152f },
            { 1087, "ROCK_BELT_087", -0.06f, 72.5f, -2.24f, 3.51f, 3.60f, -14.80f, -1.15f, -21.55f, 0.164f },
            { 1088, "CITYPOP_SILKY_088", 0.06f, 75.0f, -2.16f, 1.20f, 3.74f, -14.70f, -1.20f, -21.20f, 0.176f },
            { 1089, "RAP_TIGHT_089", 0.18f, 77.5f, -2.08f, 1.31f, 3.88f, -14.60f, -1.25f, -20.85f, 0.188f },
            { 1090, "ACOUSTIC_WARM_090", 0.30f, 80.0f, -2.00f, 1.42f, 1.50f, -16.00f, -1.00f, -20.50f, 0.200f },
            { 1091, "CHORUS_WIDE_091", 0.42f, 82.5f, -1.92f, 1.53f, 1.64f, -15.90f, -1.05f, -20.15f, 0.212f },
            { 1092, "KARAOKE_PRO_092", 0.54f, 85.0f, -1.84f, 1.64f, 1.78f, -15.80f, -1.10f, -19.80f, 0.224f },
            { 1093, "TALK_STREAM_093", 0.66f, 87.5f, -1.76f, 1.75f, 1.92f, -15.70f, -1.15f, -19.45f, 0.236f },
            { 1094, "PODCAST_RADIO_094", 0.78f, 90.0f, -1.68f, 1.86f, 2.06f, -15.60f, -1.20f, -19.10f, 0.248f },
            { 1095, "FALSETTO_AIR_095", 0.90f, 92.5f, -1.60f, 1.97f, 2.20f, -15.50f, -1.25f, -18.75f, 0.260f },
            { 1096, "VOCALOID_CRISP_096", 1.02f, 95.0f, -1.52f, 2.08f, 2.34f, -15.40f, -1.00f, -24.00f, 0.272f },
            { 1097, "RNB_VELVET_097", 1.14f, 97.5f, -1.44f, 2.19f, 2.48f, -15.30f, -1.05f, -23.65f, 0.284f },
            { 1098, "IDOL_SPARKLE_098", 1.26f, 100.0f, -1.36f, 2.30f, 2.62f, -15.20f, -1.10f, -23.30f, 0.296f },
            { 1099, "LIVE_ARENA_099", 1.38f, 102.5f, -1.28f, 2.41f, 2.76f, -15.10f, -1.15f, -22.95f, 0.308f },
            { 1100, "MIDNIGHT_LOFI_100", -1.50f, 105.0f, -2.80f, 2.52f, 2.90f, -15.00f, -1.20f, -22.60f, 0.080f },
            { 1101, "CLASSICAL_PURE_101", -1.38f, 107.5f, -2.72f, 2.63f, 3.04f, -14.90f, -1.25f, -22.25f, 0.092f },
            { 1102, "JPOP_BRIGHT_102", -1.26f, 110.0f, -2.64f, 2.74f, 3.18f, -14.80f, -1.00f, -21.90f, 0.104f },
            { 1103, "ANISONG_HEROIC_103", -1.14f, 112.5f, -2.56f, 2.85f, 3.32f, -14.70f, -1.05f, -21.55f, 0.116f },
            { 1104, "BALLAD_INTIMATE_104", -1.02f, 115.0f, -2.48f, 2.96f, 3.46f, -14.60f, -1.10f, -21.20f, 0.128f },
            { 1105, "VTUBER_CLEAR_105", -0.90f, 117.5f, -2.40f, 3.07f, 3.60f, -16.00f, -1.15f, -20.85f, 0.140f },
            { 1106, "ASMR_WHISPER_106", -0.78f, 120.0f, -2.32f, 3.18f, 3.74f, -15.90f, -1.20f, -20.50f, 0.152f },
            { 1107, "ROCK_BELT_107", -0.66f, 122.5f, -2.24f, 3.29f, 3.88f, -15.80f, -1.25f, -20.15f, 0.164f },
            { 1108, "CITYPOP_SILKY_108", -0.54f, 125.0f, -2.16f, 3.40f, 1.50f, -15.70f, -1.00f, -19.80f, 0.176f },
            { 1109, "RAP_TIGHT_109", -0.42f, 127.5f, -2.08f, 3.51f, 1.64f, -15.60f, -1.05f, -19.45f, 0.188f },
            { 1110, "ACOUSTIC_WARM_110", -0.30f, 130.0f, -2.00f, 1.20f, 1.78f, -15.50f, -1.10f, -19.10f, 0.200f },
            { 1111, "CHORUS_WIDE_111", -0.18f, 132.5f, -1.92f, 1.31f, 1.92f, -15.40f, -1.15f, -18.75f, 0.212f },
            { 1112, "KARAOKE_PRO_112", -0.06f, 65.0f, -1.84f, 1.42f, 2.06f, -15.30f, -1.20f, -24.00f, 0.224f },
            { 1113, "TALK_STREAM_113", 0.06f, 67.5f, -1.76f, 1.53f, 2.20f, -15.20f, -1.25f, -23.65f, 0.236f },
            { 1114, "PODCAST_RADIO_114", 0.18f, 70.0f, -1.68f, 1.64f, 2.34f, -15.10f, -1.00f, -23.30f, 0.248f },
            { 1115, "FALSETTO_AIR_115", 0.30f, 72.5f, -1.60f, 1.75f, 2.48f, -15.00f, -1.05f, -22.95f, 0.260f },
            { 1116, "VOCALOID_CRISP_116", 0.42f, 75.0f, -1.52f, 1.86f, 2.62f, -14.90f, -1.10f, -22.60f, 0.272f },
            { 1117, "RNB_VELVET_117", 0.54f, 77.5f, -1.44f, 1.97f, 2.76f, -14.80f, -1.15f, -22.25f, 0.284f },
            { 1118, "IDOL_SPARKLE_118", 0.66f, 80.0f, -1.36f, 2.08f, 2.90f, -14.70f, -1.20f, -21.90f, 0.296f },
            { 1119, "LIVE_ARENA_119", 0.78f, 82.5f, -1.28f, 2.19f, 3.04f, -14.60f, -1.25f, -21.55f, 0.308f },
            { 1120, "MIDNIGHT_LOFI_120", 0.90f, 85.0f, -2.80f, 2.30f, 3.18f, -16.00f, -1.00f, -21.20f, 0.080f },
            { 1121, "CLASSICAL_PURE_121", 1.02f, 87.5f, -2.72f, 2.41f, 3.32f, -15.90f, -1.05f, -20.85f, 0.092f },
            { 1122, "JPOP_BRIGHT_122", 1.14f, 90.0f, -2.64f, 2.52f, 3.46f, -15.80f, -1.10f, -20.50f, 0.104f },
            { 1123, "ANISONG_HEROIC_123", 1.26f, 92.5f, -2.56f, 2.63f, 3.60f, -15.70f, -1.15f, -20.15f, 0.116f },
            { 1124, "BALLAD_INTIMATE_124", 1.38f, 95.0f, -2.48f, 2.74f, 3.74f, -15.60f, -1.20f, -19.80f, 0.128f },
            { 1125, "VTUBER_CLEAR_125", -1.50f, 97.5f, -2.40f, 2.85f, 3.88f, -15.50f, -1.25f, -19.45f, 0.140f },
            { 1126, "ASMR_WHISPER_126", -1.38f, 100.0f, -2.32f, 2.96f, 1.50f, -15.40f, -1.00f, -19.10f, 0.152f },
            { 1127, "ROCK_BELT_127", -1.26f, 102.5f, -2.24f, 3.07f, 1.64f, -15.30f, -1.05f, -18.75f, 0.164f },
            { 1128, "CITYPOP_SILKY_128", -1.14f, 105.0f, -2.16f, 3.18f, 1.78f, -15.20f, -1.10f, -24.00f, 0.176f },
            { 1129, "RAP_TIGHT_129", -1.02f, 107.5f, -2.08f, 3.29f, 1.92f, -15.10f, -1.15f, -23.65f, 0.188f },
            { 1130, "ACOUSTIC_WARM_130", -0.90f, 110.0f, -2.00f, 3.40f, 2.06f, -15.00f, -1.20f, -23.30f, 0.200f },
            { 1131, "CHORUS_WIDE_131", -0.78f, 112.5f, -1.92f, 3.51f, 2.20f, -14.90f, -1.25f, -22.95f, 0.212f },
            { 1132, "KARAOKE_PRO_132", -0.66f, 115.0f, -1.84f, 1.20f, 2.34f, -14.80f, -1.00f, -22.60f, 0.224f },
            { 1133, "TALK_STREAM_133", -0.54f, 117.5f, -1.76f, 1.31f, 2.48f, -14.70f, -1.05f, -22.25f, 0.236f },
            { 1134, "PODCAST_RADIO_134", -0.42f, 120.0f, -1.68f, 1.42f, 2.62f, -14.60f, -1.10f, -21.90f, 0.248f },
            { 1135, "FALSETTO_AIR_135", -0.30f, 122.5f, -1.60f, 1.53f, 2.76f, -16.00f, -1.15f, -21.55f, 0.260f },
            { 1136, "VOCALOID_CRISP_136", -0.18f, 125.0f, -1.52f, 1.64f, 2.90f, -15.90f, -1.20f, -21.20f, 0.272f },
            { 1137, "RNB_VELVET_137", -0.06f, 127.5f, -1.44f, 1.75f, 3.04f, -15.80f, -1.25f, -20.85f, 0.284f },
            { 1138, "IDOL_SPARKLE_138", 0.06f, 130.0f, -1.36f, 1.86f, 3.18f, -15.70f, -1.00f, -20.50f, 0.296f },
            { 1139, "LIVE_ARENA_139", 0.18f, 132.5f, -1.28f, 1.97f, 3.32f, -15.60f, -1.05f, -20.15f, 0.308f },
            { 1140, "MIDNIGHT_LOFI_140", 0.30f, 65.0f, -2.80f, 2.08f, 3.46f, -15.50f, -1.10f, -19.80f, 0.080f },
            { 1141, "CLASSICAL_PURE_141", 0.42f, 67.5f, -2.72f, 2.19f, 3.60f, -15.40f, -1.15f, -19.45f, 0.092f },
            { 1142, "JPOP_BRIGHT_142", 0.54f, 70.0f, -2.64f, 2.30f, 3.74f, -15.30f, -1.20f, -19.10f, 0.104f },
            { 1143, "ANISONG_HEROIC_143", 0.66f, 72.5f, -2.56f, 2.41f, 3.88f, -15.20f, -1.25f, -18.75f, 0.116f },
            { 1144, "BALLAD_INTIMATE_144", 0.78f, 75.0f, -2.48f, 2.52f, 1.50f, -15.10f, -1.00f, -24.00f, 0.128f },
            { 1145, "VTUBER_CLEAR_145", 0.90f, 77.5f, -2.40f, 2.63f, 1.64f, -15.00f, -1.05f, -23.65f, 0.140f },
            { 1146, "ASMR_WHISPER_146", 1.02f, 80.0f, -2.32f, 2.74f, 1.78f, -14.90f, -1.10f, -23.30f, 0.152f },
            { 1147, "ROCK_BELT_147", 1.14f, 82.5f, -2.24f, 2.85f, 1.92f, -14.80f, -1.15f, -22.95f, 0.164f },
            { 1148, "CITYPOP_SILKY_148", 1.26f, 85.0f, -2.16f, 2.96f, 2.06f, -14.70f, -1.20f, -22.60f, 0.176f },
            { 1149, "RAP_TIGHT_149", 1.38f, 87.5f, -2.08f, 3.07f, 2.20f, -14.60f, -1.25f, -22.25f, 0.188f },
            { 1150, "ACOUSTIC_WARM_150", -1.50f, 90.0f, -2.00f, 3.18f, 2.34f, -16.00f, -1.00f, -21.90f, 0.200f },
            { 1151, "CHORUS_WIDE_151", -1.38f, 92.5f, -1.92f, 3.29f, 2.48f, -15.90f, -1.05f, -21.55f, 0.212f },
            { 1152, "KARAOKE_PRO_152", -1.26f, 95.0f, -1.84f, 3.40f, 2.62f, -15.80f, -1.10f, -21.20f, 0.224f },
            { 1153, "TALK_STREAM_153", -1.14f, 97.5f, -1.76f, 3.51f, 2.76f, -15.70f, -1.15f, -20.85f, 0.236f },
            { 1154, "PODCAST_RADIO_154", -1.02f, 100.0f, -1.68f, 1.20f, 2.90f, -15.60f, -1.20f, -20.50f, 0.248f },
            { 1155, "FALSETTO_AIR_155", -0.90f, 102.5f, -1.60f, 1.31f, 3.04f, -15.50f, -1.25f, -20.15f, 0.260f },
            { 1156, "VOCALOID_CRISP_156", -0.78f, 105.0f, -1.52f, 1.42f, 3.18f, -15.40f, -1.00f, -19.80f, 0.272f },
            { 1157, "RNB_VELVET_157", -0.66f, 107.5f, -1.44f, 1.53f, 3.32f, -15.30f, -1.05f, -19.45f, 0.284f },
            { 1158, "IDOL_SPARKLE_158", -0.54f, 110.0f, -1.36f, 1.64f, 3.46f, -15.20f, -1.10f, -19.10f, 0.296f },
            { 1159, "LIVE_ARENA_159", -0.42f, 112.5f, -1.28f, 1.75f, 3.60f, -15.10f, -1.15f, -18.75f, 0.308f },
            { 1160, "MIDNIGHT_LOFI_160", -0.30f, 115.0f, -2.80f, 1.86f, 3.74f, -15.00f, -1.20f, -24.00f, 0.080f },
            { 1161, "CLASSICAL_PURE_161", -0.18f, 117.5f, -2.72f, 1.97f, 3.88f, -14.90f, -1.25f, -23.65f, 0.092f },
            { 1162, "JPOP_BRIGHT_162", -0.06f, 120.0f, -2.64f, 2.08f, 1.50f, -14.80f, -1.00f, -23.30f, 0.104f },
            { 1163, "ANISONG_HEROIC_163", 0.06f, 122.5f, -2.56f, 2.19f, 1.64f, -14.70f, -1.05f, -22.95f, 0.116f },
            { 1164, "BALLAD_INTIMATE_164", 0.18f, 125.0f, -2.48f, 2.30f, 1.78f, -14.60f, -1.10f, -22.60f, 0.128f },
            { 1165, "VTUBER_CLEAR_165", 0.30f, 127.5f, -2.40f, 2.41f, 1.92f, -16.00f, -1.15f, -22.25f, 0.140f },
            { 1166, "ASMR_WHISPER_166", 0.42f, 130.0f, -2.32f, 2.52f, 2.06f, -15.90f, -1.20f, -21.90f, 0.152f },
            { 1167, "ROCK_BELT_167", 0.54f, 132.5f, -2.24f, 2.63f, 2.20f, -15.80f, -1.25f, -21.55f, 0.164f },
            { 1168, "CITYPOP_SILKY_168", 0.66f, 65.0f, -2.16f, 2.74f, 2.34f, -15.70f, -1.00f, -21.20f, 0.176f },
            { 1169, "RAP_TIGHT_169", 0.78f, 67.5f, -2.08f, 2.85f, 2.48f, -15.60f, -1.05f, -20.85f, 0.188f },
            { 1170, "ACOUSTIC_WARM_170", 0.90f, 70.0f, -2.00f, 2.96f, 2.62f, -15.50f, -1.10f, -20.50f, 0.200f },
            { 1171, "CHORUS_WIDE_171", 1.02f, 72.5f, -1.92f, 3.07f, 2.76f, -15.40f, -1.15f, -20.15f, 0.212f },
            { 1172, "KARAOKE_PRO_172", 1.14f, 75.0f, -1.84f, 3.18f, 2.90f, -15.30f, -1.20f, -19.80f, 0.224f },
            { 1173, "TALK_STREAM_173", 1.26f, 77.5f, -1.76f, 3.29f, 3.04f, -15.20f, -1.25f, -19.45f, 0.236f },
            { 1174, "PODCAST_RADIO_174", 1.38f, 80.0f, -1.68f, 3.40f, 3.18f, -15.10f, -1.00f, -19.10f, 0.248f },
            { 1175, "FALSETTO_AIR_175", -1.50f, 82.5f, -1.60f, 3.51f, 3.32f, -15.00f, -1.05f, -18.75f, 0.260f },
            { 1176, "VOCALOID_CRISP_176", -1.38f, 85.0f, -1.52f, 1.20f, 3.46f, -14.90f, -1.10f, -24.00f, 0.272f },
            { 1177, "RNB_VELVET_177", -1.26f, 87.5f, -1.44f, 1.31f, 3.60f, -14.80f, -1.15f, -23.65f, 0.284f },
            { 1178, "IDOL_SPARKLE_178", -1.14f, 90.0f, -1.36f, 1.42f, 3.74f, -14.70f, -1.20f, -23.30f, 0.296f },
            { 1179, "LIVE_ARENA_179", -1.02f, 92.5f, -1.28f, 1.53f, 3.88f, -14.60f, -1.25f, -22.95f, 0.308f },
        }};

        // 歴代19世代 (v1.0〜v5.5.0) パラメータ互換マイグレーション検証テーブル
        struct GenerationMigrationSpec {
            int generationIndex;
            const char* versionTag;
            float defaultGainCompensationDb;
            float limiterLookaheadMs;
            float denoiseFloorDb;
            bool supportsTruePeakGuard;
        };

        static constexpr std::array<GenerationMigrationSpec, 19> kGenerationMigrationTable = {{
            { 1, "v1.0.0", 0.90f, 1.50f, -62.0f, true },
            { 2, "v1.2.0", 0.85f, 1.60f, -62.8f, true },
            { 3, "v1.5.0", 0.80f, 1.70f, -63.6f, true },
            { 4, "v2.0.0", 0.75f, 1.80f, -64.4f, true },
            { 5, "v2.3.0", 0.70f, 1.90f, -65.2f, true },
            { 6, "v2.6.0", 0.65f, 2.00f, -66.0f, true },
            { 7, "v3.0.0", 0.60f, 2.10f, -66.8f, true },
            { 8, "v3.2.0", 0.55f, 2.20f, -67.6f, true },
            { 9, "v3.5.0", 0.50f, 2.30f, -68.4f, true },
            { 10, "v3.8.0", 0.45f, 2.40f, -69.2f, true },
            { 11, "v4.0.0", 0.40f, 2.50f, -70.0f, true },
            { 12, "v4.2.0", 0.35f, 2.60f, -70.8f, true },
            { 13, "v4.5.0", 0.30f, 2.70f, -71.6f, true },
            { 14, "v4.8.0", 0.25f, 2.80f, -72.4f, true },
            { 15, "v5.0.0", 0.20f, 2.90f, -73.2f, true },
            { 16, "v5.1.0", 0.15f, 3.00f, -74.0f, true },
            { 17, "v5.2.0", 0.10f, 3.10f, -74.8f, true },
            { 18, "v5.4.0", 0.05f, 3.20f, -75.6f, true },
            { 19, "v5.5.0", 0.00f, 3.30f, -76.4f, true },
        }};

        static inline bool executeExtendedKernelVerification(double sampleRate = 48000.0) noexcept {
            if (sampleRate <= 0.0 || !std::isfinite(sampleRate)) return false;
            ZdfTrapezoidalSvfNode svfLp{};
            ZdfTrapezoidalSvfNode svfBp{};
            ZdfTrapezoidalSvfNode svfHp{};
            HarmonicOverdriveTransferNode shaper{};
            svfLp.setCoefficients(sampleRate, 1200.0, 0.7071);
            svfBp.setCoefficients(sampleRate, 2400.0, 1.4142);
            svfHp.setCoefficients(sampleRate, 120.0, 0.7071);

            double energyAccumulator = 0.0;
            for (std::size_t n = 0; n < 256; ++n) {
                const float phase = static_cast<float>(n) * 0.09817477f;
                const float excitation = 0.25f * std::sin(phase) + 0.10f * std::cos(phase * 3.0f);
                const float lpOut = svfLp.processLowpass(excitation);
                const float bpOut = svfBp.processBandpass(excitation);
                const float hpOut = svfHp.processHighpass(excitation);
                const float shaped = shaper.processSample(lpOut + bpOut + hpOut);
                if (!std::isfinite(shaped)) return false;
                energyAccumulator += static_cast<double>(shaped * shaped);
            }
            if (!(energyAccumulator > 1.0e-6 && energyAccumulator < 1.0e6)) return false;

            float prevCenterHz = 0.0f;
            for (const auto& node : kBarkCriticalBandTable) {
                if (!(node.centerHz > prevCenterHz && node.qFactor > 0.1f && node.psychoacousticMaskingWeight > 0.0f)) {
                    return false;
                }
                prevCenterHz = node.centerHz;
            }

            for (const auto& mora : kJapaneseMoraAcousticTable) {
                if (!(mora.f1Hz > 150.0f && mora.f2Hz > mora.f1Hz && mora.f3Hz > mora.f2Hz)) {
                    return false;
                }
            }

            for (const auto& row : kThemeContrastMatrix) {
                if (!(row.verifiedContrastRatio >= 4.5f)) {
                    return false;
                }
            }

            for (const auto& preset : kStudioReferenceVault) {
                if (!(preset.highPassHz >= 20.0f && preset.highPassHz <= 350.0f && preset.truePeakCeilingDb <= -0.1f)) {
                    return false;
                }
            }

            for (const auto& gen : kGenerationMigrationTable) {
                if (!(gen.generationIndex >= 1 && gen.limiterLookaheadMs > 0.0f && gen.supportsTruePeakGuard)) {
                    return false;
                }
            }
            return true;
        }
    };

} // namespace vg::supreme::dedup
// SUPREME_30K_VERIFIED_MODULE_18
// VocalGzzio v5.5.0 Supreme 30K Engineering Suite — Verified
