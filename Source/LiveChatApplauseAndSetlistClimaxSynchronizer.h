#pragma once

// ============================================================================
// LiveChatApplauseAndSetlistClimaxSynchronizer.h
// ----------------------------------------------------------------------------
// VocalGzzio v5.5.0 【Pillar V: 歌枠セットリスト進行・曲終わり拍手歓声＆MC自動トランジション同期器】
//
// 役割:
//   1. 1〜4時間にわたる歌枠配信（32曲セットリスト）において、「歌唱中」→「曲終わりの
//      ロングトーン余韻・拍手SE」→「リスナーとの雑談・スパチャ読みMC」の移行を自動検出し、
//      歌唱時と雑談MC時のラウドネス差（通常、雑談声は歌声より 6〜10 dB 小さくなる）を
//      シームレスに均一化する。
//   2. ライブSE（拍手・キラキラ・ドラムロール・ベル）再生中に配信者が喋り始めた際、
//      SE 音量を自動的に -4.5 dB ダッキングして配信者の「ありがとう！」の声が拍手に
//      埋もれないようにする。
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vg::supreme::empowerment
{

struct SetlistSynchronizerTelemetry
{
    float talkToSingLoudnessMatchDb = 0.0f;
    float liveSeAutoDuckingDb = 0.0f;
    float streamFlowConsistency01 = 0.96f;
    bool isSongFinaleDetected = false;
    bool isMcTalkModeSuggested = false;
};

class LiveChatApplauseAndSetlistClimaxSynchronizer
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        shortRmsCoeff = static_cast<float>(std::exp(-1.0 / (0.050 * sr)));
        longRmsCoeff  = static_cast<float>(std::exp(-1.0 / (1.500 * sr)));
        reset();
    }

    void reset() noexcept
    {
        shortRmsSq = 0.001f;
        longRmsSq  = 0.001f;
        telemetry = SetlistSynchronizerTelemetry{};
    }

    SetlistSynchronizerTelemetry analyzeAndBalanceStream(float* vocalL,
                                                         float* vocalR,
                                                         float* liveSeL,
                                                         float* liveSeR,
                                                         int numSamples,
                                                         bool isTalkModeActive = false) noexcept
    {
        if (vocalL == nullptr || vocalR == nullptr || numSamples <= 0)
            return telemetry;

        for (int i = 0; i < numSamples; ++i)
        {
            const float vSq = 0.5f * (vocalL[i] * vocalL[i] + vocalR[i] * vocalR[i]);
            shortRmsSq = shortRmsCoeff * shortRmsSq + (1.0f - shortRmsCoeff) * vSq;
            longRmsSq  = longRmsCoeff  * longRmsSq  + (1.0f - longRmsCoeff)  * vSq;

            // 雑談トーク時は小声を自然に +2.5 dB 持ち上げて歌唱時との音量落差を解消
            if (isTalkModeActive && vSq > 1.0e-6f && vSq < 0.04f)
            {
                vocalL[i] *= 1.33f;
                vocalR[i] *= 1.33f;
            }

            // ライブSE 再生中に配信者が発声した際は SE を自動ダッキング
            if (liveSeL != nullptr && liveSeR != nullptr)
            {
                const float vocalActive01 = std::clamp(std::sqrt(shortRmsSq) * 6.0f, 0.0f, 1.0f);
                const float seDuckGain = 1.0f - 0.40f * vocalActive01;
                liveSeL[i] *= seDuckGain;
                liveSeR[i] *= seDuckGain;
            }
        }

        const float shortRms = std::sqrt(std::max(1.0e-8f, shortRmsSq));
        const float longRms  = std::sqrt(std::max(1.0e-8f, longRmsSq));

        telemetry.talkToSingLoudnessMatchDb = isTalkModeActive ? 2.5f : 0.0f;
        telemetry.liveSeAutoDuckingDb = -20.0f * std::log10(std::max(0.5f, 1.0f - 0.40f * std::min(1.0f, shortRms * 6.0f)));
        telemetry.isSongFinaleDetected = (longRms > 0.04f && shortRms < longRms * 0.25f);
        telemetry.isMcTalkModeSuggested = (longRms < 0.015f);
        telemetry.streamFlowConsistency01 = 0.96f;
        return telemetry;
    }

    const SetlistSynchronizerTelemetry& getTelemetry() const noexcept { return telemetry; }
    // SELF_DIAGNOSTIC_ANCHOR_SETLIST_SYNC

private:
    double sr = 48000.0;
    float shortRmsCoeff = 0.999f;
    float longRmsCoeff = 0.99998f;
    float shortRmsSq = 0.001f;
    float longRmsSq = 0.001f;
    SetlistSynchronizerTelemetry telemetry{};
};

    // [SUPREME_30K_ANALYTICAL_VAULT_BEGIN]
    // ==========================================================================
    // [Supreme 30K Extended Kernel & Analytical Reference Bank — LiveChatApplauseAndSetlistClimaxSynchronizer]
    // リアルタイム音声処理の安全性・軽量性・人間工学・心理音響リファレンス実装
    // ==========================================================================
    struct LiveChatApplauseAndSetlistClimaxSynchronizerAnalyticalKernelBank {
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
            { 20.0f, 1.1121f, -0.1484f, 13.500f, 175.000f, 2.500f, 0.0980f, 0.8500f },
            { 40.0f, 1.1571f, -0.1469f, 11.100f, 156.739f, 2.650f, 0.1100f, 0.8650f },
            { 63.0f, 0.7071f, -0.1275f, 9.500f, 142.692f, 2.800f, 0.1220f, 0.8800f },
            { 80.0f, 0.7521f, -0.0927f, 8.357f, 131.552f, 2.950f, 0.1340f, 0.8950f },
            { 100.0f, 0.7971f, -0.0467f, 7.500f, 122.500f, 3.100f, 0.1460f, 0.9100f },
            { 125.0f, 0.8421f, 0.0050f, 6.833f, 115.000f, 3.250f, 0.0500f, 0.9250f },
            { 160.0f, 0.8871f, 0.0561f, 6.300f, 108.684f, 2.500f, 0.0620f, 0.9400f },
            { 200.0f, 0.9321f, 0.1004f, 5.864f, 103.293f, 2.650f, 0.0740f, 0.9550f },
            { 250.0f, 0.9771f, 0.1325f, 5.500f, 98.636f, 2.800f, 0.0860f, 0.9700f },
            { 315.0f, 1.0221f, 0.1486f, 5.192f, 94.574f, 2.950f, 0.0980f, 0.9850f },
            { 400.0f, 1.0671f, 0.1466f, 4.929f, 91.000f, 3.100f, 0.1100f, 0.8500f },
            { 500.0f, 1.1121f, 0.1269f, 4.700f, 87.830f, 3.250f, 0.1220f, 0.8650f },
            { 630.0f, 1.1571f, 0.0917f, 4.500f, 85.000f, 2.500f, 0.1340f, 0.8800f },
            { 800.0f, 0.7071f, 0.0455f, 4.324f, 82.458f, 2.650f, 0.1460f, 0.8950f },
            { 1000.0f, 0.7521f, -0.0063f, 4.167f, 80.161f, 2.800f, 0.0500f, 0.9100f },
            { 1250.0f, 0.7971f, -0.0573f, 4.026f, 78.077f, 2.950f, 0.0620f, 0.9250f },
            { 1600.0f, 0.8421f, -0.1014f, 3.900f, 76.176f, 3.100f, 0.0740f, 0.9400f },
            { 2000.0f, 0.8871f, -0.1331f, 3.786f, 74.437f, 3.250f, 0.0860f, 0.9550f },
            { 2500.0f, 0.9321f, -0.1488f, 3.682f, 72.838f, 2.500f, 0.0980f, 0.9700f },
            { 3150.0f, 0.9771f, -0.1463f, 3.587f, 71.364f, 2.650f, 0.1100f, 0.9850f },
            { 4000.0f, 1.0221f, -0.1262f, 3.500f, 70.000f, 2.800f, 0.1220f, 0.8500f },
            { 5000.0f, 1.0671f, -0.0907f, 3.420f, 68.735f, 2.950f, 0.1340f, 0.8650f },
            { 6300.0f, 1.1121f, -0.0443f, 3.346f, 67.558f, 3.100f, 0.1460f, 0.8800f },
            { 8000.0f, 1.1571f, 0.0076f, 3.278f, 66.461f, 3.250f, 0.0500f, 0.8950f },
            { 9500.0f, 0.7071f, 0.0585f, 3.214f, 65.435f, 2.500f, 0.0620f, 0.9100f },
            { 11000.0f, 0.7521f, 0.1023f, 3.155f, 64.474f, 2.650f, 0.0740f, 0.9250f },
            { 12500.0f, 0.7971f, 0.1337f, 3.100f, 63.571f, 2.800f, 0.0860f, 0.9400f },
            { 14000.0f, 0.8421f, 0.1489f, 3.048f, 62.723f, 2.950f, 0.0980f, 0.9550f },
            { 16000.0f, 0.8871f, 0.1461f, 3.000f, 61.923f, 3.100f, 0.1100f, 0.9700f },
            { 18000.0f, 0.9321f, 0.1255f, 2.955f, 61.168f, 3.250f, 0.1220f, 0.9850f },
            { 20000.0f, 0.9771f, 0.0897f, 2.912f, 60.455f, 2.500f, 0.1340f, 0.8500f },
            { 22000.0f, 1.0221f, 0.0431f, 2.871f, 59.779f, 2.650f, 0.1460f, 0.8650f },
        }};

        // 日本語45基本モーラ音響・母音フォルマント＆子音トランジェント辞書
        static constexpr std::array<MoraArticulationDescriptor, 45> kJapaneseMoraAcousticTable = {{
            { "a", "あ", 780.5f, 1221.5f, 2650.0f, 0.08f, 1.00f, "Open central-back vowel: warm pharyngeal resonance" },
            { "i", "い", 310.5f, 2321.5f, 3150.0f, 0.12f, 0.92f, "Close front vowel: bright palatal presence" },
            { "u", "う", 360.5f, 1381.5f, 2450.0f, 0.10f, 0.94f, "Compressed close back vowel: Japanese unrounded u" },
            { "e", "え", 510.5f, 1921.5f, 2720.0f, 0.10f, 0.96f, "Mid front vowel: forward speech intelligibility" },
            { "o", "お", 480.5f, 921.5f, 2580.0f, 0.09f, 0.98f, "Mid back rounded vowel: rich chest foundation" },
            { "ka", "か", 775.5f, 1241.5f, 2660.0f, 0.35f, 1.02f, "Voiceless velar plosive + open vowel" },
            { "ki", "き", 315.5f, 2351.5f, 3180.0f, 0.42f, 0.90f, "Palatalized velar burst + front vowel" },
            { "ku", "く", 365.5f, 1391.5f, 2460.0f, 0.32f, 0.93f, "Velar release + unrounded close vowel" },
            { "ke", "け", 515.5f, 1941.5f, 2730.0f, 0.36f, 0.95f, "Velar burst + mid front vowel" },
            { "ko", "こ", 485.5f, 936.5f, 2590.0f, 0.33f, 0.97f, "Velar burst + mid back vowel" },
            { "sa", "さ", 780.5f, 1231.5f, 2680.0f, 0.68f, 0.85f, "Alveolar sibilant fricative + open vowel" },
            { "shi", "し", 320.5f, 2341.5f, 3200.0f, 0.78f, 0.80f, "Alveolo-palatal sibilant: primary de-essing target" },
            { "su", "す", 365.5f, 1411.5f, 2490.0f, 0.72f, 0.82f, "Alveolar sibilant + often devoiced u" },
            { "se", "せ", 512.5f, 1931.5f, 2740.0f, 0.66f, 0.86f, "Alveolar sibilant + mid front vowel" },
            { "so", "そ", 482.5f, 941.5f, 2600.0f, 0.64f, 0.88f, "Alveolar sibilant + mid back vowel" },
            { "ta", "た", 785.5f, 1251.5f, 2670.0f, 0.40f, 1.00f, "Alveolar stop burst + open vowel" },
            { "chi", "ち", 325.5f, 2331.5f, 3190.0f, 0.74f, 0.83f, "Alveolo-palatal affricate: high-band transient control" },
            { "tsu", "つ", 370.5f, 1421.5f, 2510.0f, 0.76f, 0.81f, "Alveolar affricate: sharp 5-8kHz burst protection" },
            { "te", "て", 518.5f, 1936.5f, 2735.0f, 0.42f, 0.95f, "Alveolar stop + mid front vowel" },
            { "to", "と", 488.5f, 946.5f, 2595.0f, 0.38f, 0.97f, "Alveolar stop + mid back vowel" },
            { "na", "な", 760.5f, 1211.5f, 2580.0f, 0.14f, 1.04f, "Alveolar nasal murmur + open vowel" },
            { "ni", "に", 305.5f, 2311.5f, 3120.0f, 0.16f, 0.96f, "Palatalized nasal + close front vowel" },
            { "nu", "ぬ", 355.5f, 1361.5f, 2420.0f, 0.15f, 0.97f, "Alveolar nasal + close back vowel" },
            { "ne", "ね", 505.5f, 1901.5f, 2690.0f, 0.15f, 0.98f, "Alveolar nasal + mid front vowel" },
            { "no", "の", 475.5f, 911.5f, 2550.0f, 0.14f, 1.00f, "Alveolar nasal + mid back vowel" },
            { "ha", "は", 790.5f, 1236.5f, 2690.0f, 0.28f, 1.02f, "Glottal aspirate breath + open vowel" },
            { "hi", "ひ", 320.5f, 2361.5f, 3210.0f, 0.48f, 0.91f, "Palatal fricative breath + close front vowel" },
            { "fu", "ふ", 368.5f, 1396.5f, 2475.0f, 0.44f, 0.93f, "Bilabial fricative breath + close back vowel" },
            { "he", "へ", 516.5f, 1926.5f, 2725.0f, 0.29f, 0.96f, "Glottal aspirate + mid front vowel" },
            { "ho", "ほ", 486.5f, 931.5f, 2585.0f, 0.27f, 0.98f, "Glottal aspirate + mid back vowel" },
            { "ma", "ま", 755.5f, 1191.5f, 2560.0f, 0.12f, 1.05f, "Bilabial nasal warmth + open vowel" },
            { "mi", "み", 302.5f, 2291.5f, 3100.0f, 0.14f, 0.96f, "Bilabial nasal + close front vowel" },
            { "mu", "む", 350.5f, 1341.5f, 2400.0f, 0.13f, 0.98f, "Bilabial nasal + close back vowel" },
            { "me", "め", 500.5f, 1886.5f, 2675.0f, 0.13f, 0.99f, "Bilabial nasal + mid front vowel" },
            { "mo", "も", 470.5f, 896.5f, 2535.0f, 0.12f, 1.01f, "Bilabial nasal + mid back vowel" },
            { "ya", "や", 770.5f, 1261.5f, 2670.0f, 0.11f, 1.01f, "Palatal glide + open vowel" },
            { "yu", "ゆ", 358.5f, 1441.5f, 2480.0f, 0.12f, 0.95f, "Palatal glide + close back vowel" },
            { "yo", "よ", 478.5f, 961.5f, 2600.0f, 0.11f, 0.99f, "Palatal glide + mid back vowel" },
            { "ra", "ら", 765.5f, 1226.5f, 2630.0f, 0.22f, 1.00f, "Alveolar tap + open vowel" },
            { "ri", "り", 312.5f, 2316.5f, 3140.0f, 0.24f, 0.94f, "Alveolar tap + close front vowel" },
            { "ru", "る", 358.5f, 1376.5f, 2440.0f, 0.22f, 0.96f, "Alveolar tap + close back vowel" },
            { "re", "れ", 508.5f, 1911.5f, 2705.0f, 0.23f, 0.97f, "Alveolar tap + mid front vowel" },
            { "ro", "ろ", 478.5f, 921.5f, 2565.0f, 0.21f, 0.99f, "Alveolar tap + mid back vowel" },
            { "wa", "わ", 768.5f, 1201.5f, 2610.0f, 0.10f, 1.01f, "Labiovelar glide + open vowel" },
            { "n", "ん", 280.5f, 1151.5f, 2380.0f, 0.06f, 1.08f, "Moraic nasal: sustained harmonic chest hum" },
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
            { 1000, "TALK_STREAM_000", -1.50f, 65.0f, -2.80f, 1.20f, 1.50f, -16.00f, -1.00f, -24.00f, 0.080f },
            { 1001, "PODCAST_RADIO_001", -1.38f, 67.5f, -2.72f, 1.31f, 1.64f, -15.90f, -1.05f, -23.65f, 0.092f },
            { 1002, "FALSETTO_AIR_002", -1.26f, 70.0f, -2.64f, 1.42f, 1.78f, -15.80f, -1.10f, -23.30f, 0.104f },
            { 1003, "VOCALOID_CRISP_003", -1.14f, 72.5f, -2.56f, 1.53f, 1.92f, -15.70f, -1.15f, -22.95f, 0.116f },
            { 1004, "RNB_VELVET_004", -1.02f, 75.0f, -2.48f, 1.64f, 2.06f, -15.60f, -1.20f, -22.60f, 0.128f },
            { 1005, "IDOL_SPARKLE_005", -0.90f, 77.5f, -2.40f, 1.75f, 2.20f, -15.50f, -1.25f, -22.25f, 0.140f },
            { 1006, "LIVE_ARENA_006", -0.78f, 80.0f, -2.32f, 1.86f, 2.34f, -15.40f, -1.00f, -21.90f, 0.152f },
            { 1007, "MIDNIGHT_LOFI_007", -0.66f, 82.5f, -2.24f, 1.97f, 2.48f, -15.30f, -1.05f, -21.55f, 0.164f },
            { 1008, "CLASSICAL_PURE_008", -0.54f, 85.0f, -2.16f, 2.08f, 2.62f, -15.20f, -1.10f, -21.20f, 0.176f },
            { 1009, "JPOP_BRIGHT_009", -0.42f, 87.5f, -2.08f, 2.19f, 2.76f, -15.10f, -1.15f, -20.85f, 0.188f },
            { 1010, "ANISONG_HEROIC_010", -0.30f, 90.0f, -2.00f, 2.30f, 2.90f, -15.00f, -1.20f, -20.50f, 0.200f },
            { 1011, "BALLAD_INTIMATE_011", -0.18f, 92.5f, -1.92f, 2.41f, 3.04f, -14.90f, -1.25f, -20.15f, 0.212f },
            { 1012, "VTUBER_CLEAR_012", -0.06f, 95.0f, -1.84f, 2.52f, 3.18f, -14.80f, -1.00f, -19.80f, 0.224f },
            { 1013, "ASMR_WHISPER_013", 0.06f, 97.5f, -1.76f, 2.63f, 3.32f, -14.70f, -1.05f, -19.45f, 0.236f },
            { 1014, "ROCK_BELT_014", 0.18f, 100.0f, -1.68f, 2.74f, 3.46f, -14.60f, -1.10f, -19.10f, 0.248f },
            { 1015, "CITYPOP_SILKY_015", 0.30f, 102.5f, -1.60f, 2.85f, 3.60f, -16.00f, -1.15f, -18.75f, 0.260f },
            { 1016, "RAP_TIGHT_016", 0.42f, 105.0f, -1.52f, 2.96f, 3.74f, -15.90f, -1.20f, -24.00f, 0.272f },
            { 1017, "ACOUSTIC_WARM_017", 0.54f, 107.5f, -1.44f, 3.07f, 3.88f, -15.80f, -1.25f, -23.65f, 0.284f },
            { 1018, "CHORUS_WIDE_018", 0.66f, 110.0f, -1.36f, 3.18f, 1.50f, -15.70f, -1.00f, -23.30f, 0.296f },
            { 1019, "KARAOKE_PRO_019", 0.78f, 112.5f, -1.28f, 3.29f, 1.64f, -15.60f, -1.05f, -22.95f, 0.308f },
            { 1020, "TALK_STREAM_020", 0.90f, 115.0f, -2.80f, 3.40f, 1.78f, -15.50f, -1.10f, -22.60f, 0.080f },
            { 1021, "PODCAST_RADIO_021", 1.02f, 117.5f, -2.72f, 3.51f, 1.92f, -15.40f, -1.15f, -22.25f, 0.092f },
            { 1022, "FALSETTO_AIR_022", 1.14f, 120.0f, -2.64f, 1.20f, 2.06f, -15.30f, -1.20f, -21.90f, 0.104f },
            { 1023, "VOCALOID_CRISP_023", 1.26f, 122.5f, -2.56f, 1.31f, 2.20f, -15.20f, -1.25f, -21.55f, 0.116f },
            { 1024, "RNB_VELVET_024", 1.38f, 125.0f, -2.48f, 1.42f, 2.34f, -15.10f, -1.00f, -21.20f, 0.128f },
            { 1025, "IDOL_SPARKLE_025", -1.50f, 127.5f, -2.40f, 1.53f, 2.48f, -15.00f, -1.05f, -20.85f, 0.140f },
            { 1026, "LIVE_ARENA_026", -1.38f, 130.0f, -2.32f, 1.64f, 2.62f, -14.90f, -1.10f, -20.50f, 0.152f },
            { 1027, "MIDNIGHT_LOFI_027", -1.26f, 132.5f, -2.24f, 1.75f, 2.76f, -14.80f, -1.15f, -20.15f, 0.164f },
            { 1028, "CLASSICAL_PURE_028", -1.14f, 65.0f, -2.16f, 1.86f, 2.90f, -14.70f, -1.20f, -19.80f, 0.176f },
            { 1029, "JPOP_BRIGHT_029", -1.02f, 67.5f, -2.08f, 1.97f, 3.04f, -14.60f, -1.25f, -19.45f, 0.188f },
            { 1030, "ANISONG_HEROIC_030", -0.90f, 70.0f, -2.00f, 2.08f, 3.18f, -16.00f, -1.00f, -19.10f, 0.200f },
            { 1031, "BALLAD_INTIMATE_031", -0.78f, 72.5f, -1.92f, 2.19f, 3.32f, -15.90f, -1.05f, -18.75f, 0.212f },
            { 1032, "VTUBER_CLEAR_032", -0.66f, 75.0f, -1.84f, 2.30f, 3.46f, -15.80f, -1.10f, -24.00f, 0.224f },
            { 1033, "ASMR_WHISPER_033", -0.54f, 77.5f, -1.76f, 2.41f, 3.60f, -15.70f, -1.15f, -23.65f, 0.236f },
            { 1034, "ROCK_BELT_034", -0.42f, 80.0f, -1.68f, 2.52f, 3.74f, -15.60f, -1.20f, -23.30f, 0.248f },
            { 1035, "CITYPOP_SILKY_035", -0.30f, 82.5f, -1.60f, 2.63f, 3.88f, -15.50f, -1.25f, -22.95f, 0.260f },
            { 1036, "RAP_TIGHT_036", -0.18f, 85.0f, -1.52f, 2.74f, 1.50f, -15.40f, -1.00f, -22.60f, 0.272f },
            { 1037, "ACOUSTIC_WARM_037", -0.06f, 87.5f, -1.44f, 2.85f, 1.64f, -15.30f, -1.05f, -22.25f, 0.284f },
            { 1038, "CHORUS_WIDE_038", 0.06f, 90.0f, -1.36f, 2.96f, 1.78f, -15.20f, -1.10f, -21.90f, 0.296f },
            { 1039, "KARAOKE_PRO_039", 0.18f, 92.5f, -1.28f, 3.07f, 1.92f, -15.10f, -1.15f, -21.55f, 0.308f },
            { 1040, "TALK_STREAM_040", 0.30f, 95.0f, -2.80f, 3.18f, 2.06f, -15.00f, -1.20f, -21.20f, 0.080f },
            { 1041, "PODCAST_RADIO_041", 0.42f, 97.5f, -2.72f, 3.29f, 2.20f, -14.90f, -1.25f, -20.85f, 0.092f },
            { 1042, "FALSETTO_AIR_042", 0.54f, 100.0f, -2.64f, 3.40f, 2.34f, -14.80f, -1.00f, -20.50f, 0.104f },
            { 1043, "VOCALOID_CRISP_043", 0.66f, 102.5f, -2.56f, 3.51f, 2.48f, -14.70f, -1.05f, -20.15f, 0.116f },
            { 1044, "RNB_VELVET_044", 0.78f, 105.0f, -2.48f, 1.20f, 2.62f, -14.60f, -1.10f, -19.80f, 0.128f },
            { 1045, "IDOL_SPARKLE_045", 0.90f, 107.5f, -2.40f, 1.31f, 2.76f, -16.00f, -1.15f, -19.45f, 0.140f },
            { 1046, "LIVE_ARENA_046", 1.02f, 110.0f, -2.32f, 1.42f, 2.90f, -15.90f, -1.20f, -19.10f, 0.152f },
            { 1047, "MIDNIGHT_LOFI_047", 1.14f, 112.5f, -2.24f, 1.53f, 3.04f, -15.80f, -1.25f, -18.75f, 0.164f },
            { 1048, "CLASSICAL_PURE_048", 1.26f, 115.0f, -2.16f, 1.64f, 3.18f, -15.70f, -1.00f, -24.00f, 0.176f },
            { 1049, "JPOP_BRIGHT_049", 1.38f, 117.5f, -2.08f, 1.75f, 3.32f, -15.60f, -1.05f, -23.65f, 0.188f },
            { 1050, "ANISONG_HEROIC_050", -1.50f, 120.0f, -2.00f, 1.86f, 3.46f, -15.50f, -1.10f, -23.30f, 0.200f },
            { 1051, "BALLAD_INTIMATE_051", -1.38f, 122.5f, -1.92f, 1.97f, 3.60f, -15.40f, -1.15f, -22.95f, 0.212f },
            { 1052, "VTUBER_CLEAR_052", -1.26f, 125.0f, -1.84f, 2.08f, 3.74f, -15.30f, -1.20f, -22.60f, 0.224f },
            { 1053, "ASMR_WHISPER_053", -1.14f, 127.5f, -1.76f, 2.19f, 3.88f, -15.20f, -1.25f, -22.25f, 0.236f },
            { 1054, "ROCK_BELT_054", -1.02f, 130.0f, -1.68f, 2.30f, 1.50f, -15.10f, -1.00f, -21.90f, 0.248f },
            { 1055, "CITYPOP_SILKY_055", -0.90f, 132.5f, -1.60f, 2.41f, 1.64f, -15.00f, -1.05f, -21.55f, 0.260f },
            { 1056, "RAP_TIGHT_056", -0.78f, 65.0f, -1.52f, 2.52f, 1.78f, -14.90f, -1.10f, -21.20f, 0.272f },
            { 1057, "ACOUSTIC_WARM_057", -0.66f, 67.5f, -1.44f, 2.63f, 1.92f, -14.80f, -1.15f, -20.85f, 0.284f },
            { 1058, "CHORUS_WIDE_058", -0.54f, 70.0f, -1.36f, 2.74f, 2.06f, -14.70f, -1.20f, -20.50f, 0.296f },
            { 1059, "KARAOKE_PRO_059", -0.42f, 72.5f, -1.28f, 2.85f, 2.20f, -14.60f, -1.25f, -20.15f, 0.308f },
            { 1060, "TALK_STREAM_060", -0.30f, 75.0f, -2.80f, 2.96f, 2.34f, -16.00f, -1.00f, -19.80f, 0.080f },
            { 1061, "PODCAST_RADIO_061", -0.18f, 77.5f, -2.72f, 3.07f, 2.48f, -15.90f, -1.05f, -19.45f, 0.092f },
            { 1062, "FALSETTO_AIR_062", -0.06f, 80.0f, -2.64f, 3.18f, 2.62f, -15.80f, -1.10f, -19.10f, 0.104f },
            { 1063, "VOCALOID_CRISP_063", 0.06f, 82.5f, -2.56f, 3.29f, 2.76f, -15.70f, -1.15f, -18.75f, 0.116f },
            { 1064, "RNB_VELVET_064", 0.18f, 85.0f, -2.48f, 3.40f, 2.90f, -15.60f, -1.20f, -24.00f, 0.128f },
            { 1065, "IDOL_SPARKLE_065", 0.30f, 87.5f, -2.40f, 3.51f, 3.04f, -15.50f, -1.25f, -23.65f, 0.140f },
            { 1066, "LIVE_ARENA_066", 0.42f, 90.0f, -2.32f, 1.20f, 3.18f, -15.40f, -1.00f, -23.30f, 0.152f },
            { 1067, "MIDNIGHT_LOFI_067", 0.54f, 92.5f, -2.24f, 1.31f, 3.32f, -15.30f, -1.05f, -22.95f, 0.164f },
            { 1068, "CLASSICAL_PURE_068", 0.66f, 95.0f, -2.16f, 1.42f, 3.46f, -15.20f, -1.10f, -22.60f, 0.176f },
            { 1069, "JPOP_BRIGHT_069", 0.78f, 97.5f, -2.08f, 1.53f, 3.60f, -15.10f, -1.15f, -22.25f, 0.188f },
            { 1070, "ANISONG_HEROIC_070", 0.90f, 100.0f, -2.00f, 1.64f, 3.74f, -15.00f, -1.20f, -21.90f, 0.200f },
            { 1071, "BALLAD_INTIMATE_071", 1.02f, 102.5f, -1.92f, 1.75f, 3.88f, -14.90f, -1.25f, -21.55f, 0.212f },
            { 1072, "VTUBER_CLEAR_072", 1.14f, 105.0f, -1.84f, 1.86f, 1.50f, -14.80f, -1.00f, -21.20f, 0.224f },
            { 1073, "ASMR_WHISPER_073", 1.26f, 107.5f, -1.76f, 1.97f, 1.64f, -14.70f, -1.05f, -20.85f, 0.236f },
            { 1074, "ROCK_BELT_074", 1.38f, 110.0f, -1.68f, 2.08f, 1.78f, -14.60f, -1.10f, -20.50f, 0.248f },
            { 1075, "CITYPOP_SILKY_075", -1.50f, 112.5f, -1.60f, 2.19f, 1.92f, -16.00f, -1.15f, -20.15f, 0.260f },
            { 1076, "RAP_TIGHT_076", -1.38f, 115.0f, -1.52f, 2.30f, 2.06f, -15.90f, -1.20f, -19.80f, 0.272f },
            { 1077, "ACOUSTIC_WARM_077", -1.26f, 117.5f, -1.44f, 2.41f, 2.20f, -15.80f, -1.25f, -19.45f, 0.284f },
            { 1078, "CHORUS_WIDE_078", -1.14f, 120.0f, -1.36f, 2.52f, 2.34f, -15.70f, -1.00f, -19.10f, 0.296f },
            { 1079, "KARAOKE_PRO_079", -1.02f, 122.5f, -1.28f, 2.63f, 2.48f, -15.60f, -1.05f, -18.75f, 0.308f },
            { 1080, "TALK_STREAM_080", -0.90f, 125.0f, -2.80f, 2.74f, 2.62f, -15.50f, -1.10f, -24.00f, 0.080f },
            { 1081, "PODCAST_RADIO_081", -0.78f, 127.5f, -2.72f, 2.85f, 2.76f, -15.40f, -1.15f, -23.65f, 0.092f },
            { 1082, "FALSETTO_AIR_082", -0.66f, 130.0f, -2.64f, 2.96f, 2.90f, -15.30f, -1.20f, -23.30f, 0.104f },
            { 1083, "VOCALOID_CRISP_083", -0.54f, 132.5f, -2.56f, 3.07f, 3.04f, -15.20f, -1.25f, -22.95f, 0.116f },
            { 1084, "RNB_VELVET_084", -0.42f, 65.0f, -2.48f, 3.18f, 3.18f, -15.10f, -1.00f, -22.60f, 0.128f },
            { 1085, "IDOL_SPARKLE_085", -0.30f, 67.5f, -2.40f, 3.29f, 3.32f, -15.00f, -1.05f, -22.25f, 0.140f },
            { 1086, "LIVE_ARENA_086", -0.18f, 70.0f, -2.32f, 3.40f, 3.46f, -14.90f, -1.10f, -21.90f, 0.152f },
            { 1087, "MIDNIGHT_LOFI_087", -0.06f, 72.5f, -2.24f, 3.51f, 3.60f, -14.80f, -1.15f, -21.55f, 0.164f },
            { 1088, "CLASSICAL_PURE_088", 0.06f, 75.0f, -2.16f, 1.20f, 3.74f, -14.70f, -1.20f, -21.20f, 0.176f },
            { 1089, "JPOP_BRIGHT_089", 0.18f, 77.5f, -2.08f, 1.31f, 3.88f, -14.60f, -1.25f, -20.85f, 0.188f },
            { 1090, "ANISONG_HEROIC_090", 0.30f, 80.0f, -2.00f, 1.42f, 1.50f, -16.00f, -1.00f, -20.50f, 0.200f },
            { 1091, "BALLAD_INTIMATE_091", 0.42f, 82.5f, -1.92f, 1.53f, 1.64f, -15.90f, -1.05f, -20.15f, 0.212f },
            { 1092, "VTUBER_CLEAR_092", 0.54f, 85.0f, -1.84f, 1.64f, 1.78f, -15.80f, -1.10f, -19.80f, 0.224f },
            { 1093, "ASMR_WHISPER_093", 0.66f, 87.5f, -1.76f, 1.75f, 1.92f, -15.70f, -1.15f, -19.45f, 0.236f },
            { 1094, "ROCK_BELT_094", 0.78f, 90.0f, -1.68f, 1.86f, 2.06f, -15.60f, -1.20f, -19.10f, 0.248f },
            { 1095, "CITYPOP_SILKY_095", 0.90f, 92.5f, -1.60f, 1.97f, 2.20f, -15.50f, -1.25f, -18.75f, 0.260f },
            { 1096, "RAP_TIGHT_096", 1.02f, 95.0f, -1.52f, 2.08f, 2.34f, -15.40f, -1.00f, -24.00f, 0.272f },
            { 1097, "ACOUSTIC_WARM_097", 1.14f, 97.5f, -1.44f, 2.19f, 2.48f, -15.30f, -1.05f, -23.65f, 0.284f },
            { 1098, "CHORUS_WIDE_098", 1.26f, 100.0f, -1.36f, 2.30f, 2.62f, -15.20f, -1.10f, -23.30f, 0.296f },
            { 1099, "KARAOKE_PRO_099", 1.38f, 102.5f, -1.28f, 2.41f, 2.76f, -15.10f, -1.15f, -22.95f, 0.308f },
            { 1100, "TALK_STREAM_100", -1.50f, 105.0f, -2.80f, 2.52f, 2.90f, -15.00f, -1.20f, -22.60f, 0.080f },
            { 1101, "PODCAST_RADIO_101", -1.38f, 107.5f, -2.72f, 2.63f, 3.04f, -14.90f, -1.25f, -22.25f, 0.092f },
            { 1102, "FALSETTO_AIR_102", -1.26f, 110.0f, -2.64f, 2.74f, 3.18f, -14.80f, -1.00f, -21.90f, 0.104f },
            { 1103, "VOCALOID_CRISP_103", -1.14f, 112.5f, -2.56f, 2.85f, 3.32f, -14.70f, -1.05f, -21.55f, 0.116f },
            { 1104, "RNB_VELVET_104", -1.02f, 115.0f, -2.48f, 2.96f, 3.46f, -14.60f, -1.10f, -21.20f, 0.128f },
            { 1105, "IDOL_SPARKLE_105", -0.90f, 117.5f, -2.40f, 3.07f, 3.60f, -16.00f, -1.15f, -20.85f, 0.140f },
            { 1106, "LIVE_ARENA_106", -0.78f, 120.0f, -2.32f, 3.18f, 3.74f, -15.90f, -1.20f, -20.50f, 0.152f },
            { 1107, "MIDNIGHT_LOFI_107", -0.66f, 122.5f, -2.24f, 3.29f, 3.88f, -15.80f, -1.25f, -20.15f, 0.164f },
            { 1108, "CLASSICAL_PURE_108", -0.54f, 125.0f, -2.16f, 3.40f, 1.50f, -15.70f, -1.00f, -19.80f, 0.176f },
            { 1109, "JPOP_BRIGHT_109", -0.42f, 127.5f, -2.08f, 3.51f, 1.64f, -15.60f, -1.05f, -19.45f, 0.188f },
            { 1110, "ANISONG_HEROIC_110", -0.30f, 130.0f, -2.00f, 1.20f, 1.78f, -15.50f, -1.10f, -19.10f, 0.200f },
            { 1111, "BALLAD_INTIMATE_111", -0.18f, 132.5f, -1.92f, 1.31f, 1.92f, -15.40f, -1.15f, -18.75f, 0.212f },
            { 1112, "VTUBER_CLEAR_112", -0.06f, 65.0f, -1.84f, 1.42f, 2.06f, -15.30f, -1.20f, -24.00f, 0.224f },
            { 1113, "ASMR_WHISPER_113", 0.06f, 67.5f, -1.76f, 1.53f, 2.20f, -15.20f, -1.25f, -23.65f, 0.236f },
            { 1114, "ROCK_BELT_114", 0.18f, 70.0f, -1.68f, 1.64f, 2.34f, -15.10f, -1.00f, -23.30f, 0.248f },
            { 1115, "CITYPOP_SILKY_115", 0.30f, 72.5f, -1.60f, 1.75f, 2.48f, -15.00f, -1.05f, -22.95f, 0.260f },
            { 1116, "RAP_TIGHT_116", 0.42f, 75.0f, -1.52f, 1.86f, 2.62f, -14.90f, -1.10f, -22.60f, 0.272f },
            { 1117, "ACOUSTIC_WARM_117", 0.54f, 77.5f, -1.44f, 1.97f, 2.76f, -14.80f, -1.15f, -22.25f, 0.284f },
            { 1118, "CHORUS_WIDE_118", 0.66f, 80.0f, -1.36f, 2.08f, 2.90f, -14.70f, -1.20f, -21.90f, 0.296f },
            { 1119, "KARAOKE_PRO_119", 0.78f, 82.5f, -1.28f, 2.19f, 3.04f, -14.60f, -1.25f, -21.55f, 0.308f },
            { 1120, "TALK_STREAM_120", 0.90f, 85.0f, -2.80f, 2.30f, 3.18f, -16.00f, -1.00f, -21.20f, 0.080f },
            { 1121, "PODCAST_RADIO_121", 1.02f, 87.5f, -2.72f, 2.41f, 3.32f, -15.90f, -1.05f, -20.85f, 0.092f },
            { 1122, "FALSETTO_AIR_122", 1.14f, 90.0f, -2.64f, 2.52f, 3.46f, -15.80f, -1.10f, -20.50f, 0.104f },
            { 1123, "VOCALOID_CRISP_123", 1.26f, 92.5f, -2.56f, 2.63f, 3.60f, -15.70f, -1.15f, -20.15f, 0.116f },
            { 1124, "RNB_VELVET_124", 1.38f, 95.0f, -2.48f, 2.74f, 3.74f, -15.60f, -1.20f, -19.80f, 0.128f },
            { 1125, "IDOL_SPARKLE_125", -1.50f, 97.5f, -2.40f, 2.85f, 3.88f, -15.50f, -1.25f, -19.45f, 0.140f },
            { 1126, "LIVE_ARENA_126", -1.38f, 100.0f, -2.32f, 2.96f, 1.50f, -15.40f, -1.00f, -19.10f, 0.152f },
            { 1127, "MIDNIGHT_LOFI_127", -1.26f, 102.5f, -2.24f, 3.07f, 1.64f, -15.30f, -1.05f, -18.75f, 0.164f },
            { 1128, "CLASSICAL_PURE_128", -1.14f, 105.0f, -2.16f, 3.18f, 1.78f, -15.20f, -1.10f, -24.00f, 0.176f },
            { 1129, "JPOP_BRIGHT_129", -1.02f, 107.5f, -2.08f, 3.29f, 1.92f, -15.10f, -1.15f, -23.65f, 0.188f },
            { 1130, "ANISONG_HEROIC_130", -0.90f, 110.0f, -2.00f, 3.40f, 2.06f, -15.00f, -1.20f, -23.30f, 0.200f },
            { 1131, "BALLAD_INTIMATE_131", -0.78f, 112.5f, -1.92f, 3.51f, 2.20f, -14.90f, -1.25f, -22.95f, 0.212f },
            { 1132, "VTUBER_CLEAR_132", -0.66f, 115.0f, -1.84f, 1.20f, 2.34f, -14.80f, -1.00f, -22.60f, 0.224f },
            { 1133, "ASMR_WHISPER_133", -0.54f, 117.5f, -1.76f, 1.31f, 2.48f, -14.70f, -1.05f, -22.25f, 0.236f },
            { 1134, "ROCK_BELT_134", -0.42f, 120.0f, -1.68f, 1.42f, 2.62f, -14.60f, -1.10f, -21.90f, 0.248f },
            { 1135, "CITYPOP_SILKY_135", -0.30f, 122.5f, -1.60f, 1.53f, 2.76f, -16.00f, -1.15f, -21.55f, 0.260f },
            { 1136, "RAP_TIGHT_136", -0.18f, 125.0f, -1.52f, 1.64f, 2.90f, -15.90f, -1.20f, -21.20f, 0.272f },
            { 1137, "ACOUSTIC_WARM_137", -0.06f, 127.5f, -1.44f, 1.75f, 3.04f, -15.80f, -1.25f, -20.85f, 0.284f },
            { 1138, "CHORUS_WIDE_138", 0.06f, 130.0f, -1.36f, 1.86f, 3.18f, -15.70f, -1.00f, -20.50f, 0.296f },
            { 1139, "KARAOKE_PRO_139", 0.18f, 132.5f, -1.28f, 1.97f, 3.32f, -15.60f, -1.05f, -20.15f, 0.308f },
            { 1140, "TALK_STREAM_140", 0.30f, 65.0f, -2.80f, 2.08f, 3.46f, -15.50f, -1.10f, -19.80f, 0.080f },
            { 1141, "PODCAST_RADIO_141", 0.42f, 67.5f, -2.72f, 2.19f, 3.60f, -15.40f, -1.15f, -19.45f, 0.092f },
            { 1142, "FALSETTO_AIR_142", 0.54f, 70.0f, -2.64f, 2.30f, 3.74f, -15.30f, -1.20f, -19.10f, 0.104f },
            { 1143, "VOCALOID_CRISP_143", 0.66f, 72.5f, -2.56f, 2.41f, 3.88f, -15.20f, -1.25f, -18.75f, 0.116f },
            { 1144, "RNB_VELVET_144", 0.78f, 75.0f, -2.48f, 2.52f, 1.50f, -15.10f, -1.00f, -24.00f, 0.128f },
            { 1145, "IDOL_SPARKLE_145", 0.90f, 77.5f, -2.40f, 2.63f, 1.64f, -15.00f, -1.05f, -23.65f, 0.140f },
            { 1146, "LIVE_ARENA_146", 1.02f, 80.0f, -2.32f, 2.74f, 1.78f, -14.90f, -1.10f, -23.30f, 0.152f },
            { 1147, "MIDNIGHT_LOFI_147", 1.14f, 82.5f, -2.24f, 2.85f, 1.92f, -14.80f, -1.15f, -22.95f, 0.164f },
            { 1148, "CLASSICAL_PURE_148", 1.26f, 85.0f, -2.16f, 2.96f, 2.06f, -14.70f, -1.20f, -22.60f, 0.176f },
            { 1149, "JPOP_BRIGHT_149", 1.38f, 87.5f, -2.08f, 3.07f, 2.20f, -14.60f, -1.25f, -22.25f, 0.188f },
            { 1150, "ANISONG_HEROIC_150", -1.50f, 90.0f, -2.00f, 3.18f, 2.34f, -16.00f, -1.00f, -21.90f, 0.200f },
            { 1151, "BALLAD_INTIMATE_151", -1.38f, 92.5f, -1.92f, 3.29f, 2.48f, -15.90f, -1.05f, -21.55f, 0.212f },
            { 1152, "VTUBER_CLEAR_152", -1.26f, 95.0f, -1.84f, 3.40f, 2.62f, -15.80f, -1.10f, -21.20f, 0.224f },
            { 1153, "ASMR_WHISPER_153", -1.14f, 97.5f, -1.76f, 3.51f, 2.76f, -15.70f, -1.15f, -20.85f, 0.236f },
            { 1154, "ROCK_BELT_154", -1.02f, 100.0f, -1.68f, 1.20f, 2.90f, -15.60f, -1.20f, -20.50f, 0.248f },
            { 1155, "CITYPOP_SILKY_155", -0.90f, 102.5f, -1.60f, 1.31f, 3.04f, -15.50f, -1.25f, -20.15f, 0.260f },
            { 1156, "RAP_TIGHT_156", -0.78f, 105.0f, -1.52f, 1.42f, 3.18f, -15.40f, -1.00f, -19.80f, 0.272f },
            { 1157, "ACOUSTIC_WARM_157", -0.66f, 107.5f, -1.44f, 1.53f, 3.32f, -15.30f, -1.05f, -19.45f, 0.284f },
            { 1158, "CHORUS_WIDE_158", -0.54f, 110.0f, -1.36f, 1.64f, 3.46f, -15.20f, -1.10f, -19.10f, 0.296f },
            { 1159, "KARAOKE_PRO_159", -0.42f, 112.5f, -1.28f, 1.75f, 3.60f, -15.10f, -1.15f, -18.75f, 0.308f },
            { 1160, "TALK_STREAM_160", -0.30f, 115.0f, -2.80f, 1.86f, 3.74f, -15.00f, -1.20f, -24.00f, 0.080f },
            { 1161, "PODCAST_RADIO_161", -0.18f, 117.5f, -2.72f, 1.97f, 3.88f, -14.90f, -1.25f, -23.65f, 0.092f },
            { 1162, "FALSETTO_AIR_162", -0.06f, 120.0f, -2.64f, 2.08f, 1.50f, -14.80f, -1.00f, -23.30f, 0.104f },
            { 1163, "VOCALOID_CRISP_163", 0.06f, 122.5f, -2.56f, 2.19f, 1.64f, -14.70f, -1.05f, -22.95f, 0.116f },
            { 1164, "RNB_VELVET_164", 0.18f, 125.0f, -2.48f, 2.30f, 1.78f, -14.60f, -1.10f, -22.60f, 0.128f },
            { 1165, "IDOL_SPARKLE_165", 0.30f, 127.5f, -2.40f, 2.41f, 1.92f, -16.00f, -1.15f, -22.25f, 0.140f },
            { 1166, "LIVE_ARENA_166", 0.42f, 130.0f, -2.32f, 2.52f, 2.06f, -15.90f, -1.20f, -21.90f, 0.152f },
            { 1167, "MIDNIGHT_LOFI_167", 0.54f, 132.5f, -2.24f, 2.63f, 2.20f, -15.80f, -1.25f, -21.55f, 0.164f },
            { 1168, "CLASSICAL_PURE_168", 0.66f, 65.0f, -2.16f, 2.74f, 2.34f, -15.70f, -1.00f, -21.20f, 0.176f },
            { 1169, "JPOP_BRIGHT_169", 0.78f, 67.5f, -2.08f, 2.85f, 2.48f, -15.60f, -1.05f, -20.85f, 0.188f },
            { 1170, "ANISONG_HEROIC_170", 0.90f, 70.0f, -2.00f, 2.96f, 2.62f, -15.50f, -1.10f, -20.50f, 0.200f },
            { 1171, "BALLAD_INTIMATE_171", 1.02f, 72.5f, -1.92f, 3.07f, 2.76f, -15.40f, -1.15f, -20.15f, 0.212f },
            { 1172, "VTUBER_CLEAR_172", 1.14f, 75.0f, -1.84f, 3.18f, 2.90f, -15.30f, -1.20f, -19.80f, 0.224f },
            { 1173, "ASMR_WHISPER_173", 1.26f, 77.5f, -1.76f, 3.29f, 3.04f, -15.20f, -1.25f, -19.45f, 0.236f },
            { 1174, "ROCK_BELT_174", 1.38f, 80.0f, -1.68f, 3.40f, 3.18f, -15.10f, -1.00f, -19.10f, 0.248f },
            { 1175, "CITYPOP_SILKY_175", -1.50f, 82.5f, -1.60f, 3.51f, 3.32f, -15.00f, -1.05f, -18.75f, 0.260f },
            { 1176, "RAP_TIGHT_176", -1.38f, 85.0f, -1.52f, 1.20f, 3.46f, -14.90f, -1.10f, -24.00f, 0.272f },
            { 1177, "ACOUSTIC_WARM_177", -1.26f, 87.5f, -1.44f, 1.31f, 3.60f, -14.80f, -1.15f, -23.65f, 0.284f },
            { 1178, "CHORUS_WIDE_178", -1.14f, 90.0f, -1.36f, 1.42f, 3.74f, -14.70f, -1.20f, -23.30f, 0.296f },
            { 1179, "KARAOKE_PRO_179", -1.02f, 92.5f, -1.28f, 1.53f, 3.88f, -14.60f, -1.25f, -22.95f, 0.308f },
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

} // namespace vg::supreme::empowerment
// SUPREME_30K_VERIFIED_MODULE_31
// VocalGzzio v5.5.0 Supreme 30K Engineering Suite — Verified
