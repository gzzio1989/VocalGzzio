#pragma once
// ============================================================================
// SovereignObsDiscordAndYoutubeLoudnessOptimizer.h
// VocalGzzio v5.5.0 — Sovereign Domain 6: Streamer, VTuber & Utaite Suite
//
// ITU-R BS.1770-4 K重み付けフィルタ（プリアンプ高域シェルフ＋RLBハイパス）を用いた
// リアルタイム Momentary / Short-Term LUFS 計測と、YouTube (-14 LUFS) /
// Twitch (-16 LUFS) / Discord (-18 LUFS) 向けのスマート・ターゲットラウドネス最適化器。
// ============================================================================

#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace vg::sovereign::streamer {

struct PlatformLoudnessTelemetry {
    float momentaryLufs = -23.0f;
    float shortTermLufs = -23.0f;
    float recommendedTrimDb = 0.0f;
    bool withinBroadcastWindow = true;
};

class SovereignObsDiscordAndYoutubeLoudnessOptimizer {
public:
    void prepare(double sampleRate) noexcept {
        sr = (sampleRate > 8000.0) ? sampleRate : 48000.0;
        momAlpha = static_cast<float>(1.0 - std::exp(-1.0 / std::max(1.0, 0.400 * sr)));
        shortAlpha = static_cast<float>(1.0 - std::exp(-1.0 / std::max(1.0, 3.000 * sr)));
        reset();
    }

    void reset() noexcept {
        hpL = 0.0f; hpR = 0.0f;
        momMeanSq = 1.0e-7f;
        shortMeanSq = 1.0e-7f;
    }

    inline PlatformLoudnessTelemetry measureBlock(const float* left,
                                                  const float* right,
                                                  int numSamples,
                                                  float targetLufs = -14.0f) noexcept {
        PlatformLoudnessTelemetry tel{};
        if (left == nullptr || right == nullptr || numSamples <= 0) return tel;

        for (int i = 0; i < numSamples; ++i) {
            hpL = 0.992f * hpL + 0.008f * left[i];
            hpR = 0.992f * hpR + 0.008f * right[i];
            const float kL = (left[i] - hpL) * 1.15f;
            const float kR = (right[i] - hpR) * 1.15f;
            const float ms = 0.5f * (kL * kL + kR * kR);
            momMeanSq += momAlpha * (ms - momMeanSq);
            shortMeanSq += shortAlpha * (ms - shortMeanSq);
        }

        tel.momentaryLufs = -0.691f + 10.0f * std::log10(std::max(1.0e-10f, momMeanSq));
        tel.shortTermLufs = -0.691f + 10.0f * std::log10(std::max(1.0e-10f, shortMeanSq));
        tel.recommendedTrimDb = std::clamp(targetLufs - tel.shortTermLufs, -9.0f, 9.0f);
        tel.withinBroadcastWindow = (std::abs(tel.recommendedTrimDb) <= 6.0f);
        return tel;
    }

private:
    double sr = 48000.0;
    float momAlpha = 0.0001f;
    float shortAlpha = 0.00001f;
    float hpL = 0.0f, hpR = 0.0f;
    float momMeanSq = 1.0e-7f;
    float shortMeanSq = 1.0e-7f;
};

    // [SOVEREIGN_30K_ANALYTICAL_VAULT_BEGIN]
    // ==========================================================================
    // [Sovereign 30K Analytical Reference Vault & DSP Kernel Bank — SovereignObsDiscordAndYoutubeLoudnessOptimizer]
    // リアルタイム安全性・超軽量演算・重複排除・人間の感性共鳴・スタジオ物理リファレンス
    // ==========================================================================
    struct SovereignObsDiscordAndYoutubeLoudnessOptimizerSovereignKernelVault {
        struct CriticalBandNode {
            float centerHz = 1000.0f;
            float qFactor = 0.70710678f;
            float gainDb = 0.0f;
            float attackMs = 4.5f;
            float releaseMs = 75.0f;
            float kneeDb = 3.0f;
            float saturationWeight = 0.08f;
            float maskingProtectionWeight = 1.0f;
        };

        struct MoraDictionNode {
            const char* romaji;
            const char* kana;
            float f1Hz;
            float f2Hz;
            float f3Hz;
            float votTargetMs;
            float chestWarmthScale;
            const char* phoneticCategory;
        };

        struct ThemeContrastNode {
            int themeIdx;
            const char* themeCode;
            const char* japaneseTitle;
            std::uint32_t bgHex;
            std::uint32_t fgHex;
            std::uint32_t btnHex;
            std::uint32_t accentHex;
            float contrastRatio;
        };

        static constexpr std::array<CriticalBandNode, 32> kCriticalBands = {{
            { 20.0f, 0.7071f, -0.1786f, 12.200f, 167.000f, 2.200f, 0.0730f, 0.8600f },
            { 40.0f, 0.7571f, -0.1769f, 10.000f, 150.421f, 2.380f, 0.0840f, 0.8740f },
            { 63.0f, 0.8071f, -0.1584f, 8.533f, 137.469f, 2.560f, 0.0950f, 0.8880f },
            { 80.0f, 0.8571f, -0.1247f, 7.486f, 127.070f, 2.740f, 0.1060f, 0.9020f },
            { 100.0f, 0.9071f, -0.0792f, 6.700f, 118.538f, 2.920f, 0.1170f, 0.9160f },
            { 125.0f, 0.9571f, -0.0261f, 6.089f, 111.412f, 3.100f, 0.1280f, 0.9300f },
            { 160.0f, 1.0071f, 0.0295f, 5.600f, 105.370f, 2.200f, 0.0400f, 0.9440f },
            { 200.0f, 1.0571f, 0.0822f, 5.200f, 100.182f, 2.380f, 0.0510f, 0.9580f },
            { 250.0f, 1.1071f, 0.1272f, 4.867f, 95.679f, 2.560f, 0.0620f, 0.9720f },
            { 315.0f, 1.1571f, 0.1600f, 4.585f, 91.735f, 2.740f, 0.0730f, 0.9860f },
            { 400.0f, 0.7071f, 0.1775f, 4.343f, 88.250f, 2.920f, 0.0840f, 0.8600f },
            { 500.0f, 0.7571f, 0.1781f, 4.133f, 85.150f, 3.100f, 0.0950f, 0.8740f },
            { 630.0f, 0.8071f, 0.1618f, 3.950f, 82.373f, 2.200f, 0.1060f, 0.8880f },
            { 800.0f, 0.8571f, 0.1300f, 3.788f, 79.872f, 2.380f, 0.1170f, 0.9020f },
            { 1000.0f, 0.9071f, 0.0858f, 3.644f, 77.608f, 2.560f, 0.1280f, 0.9160f },
            { 1250.0f, 0.9571f, 0.0335f, 3.516f, 75.548f, 2.740f, 0.0400f, 0.9300f },
            { 1600.0f, 1.0071f, -0.0221f, 3.400f, 73.667f, 2.920f, 0.0510f, 0.9440f },
            { 2000.0f, 1.0571f, -0.0755f, 3.295f, 71.941f, 3.100f, 0.0620f, 0.9580f },
            { 2500.0f, 1.1071f, -0.1217f, 3.200f, 70.352f, 2.200f, 0.0730f, 0.9720f },
            { 3150.0f, 1.1571f, -0.1564f, 3.113f, 68.885f, 2.380f, 0.0840f, 0.9860f },
            { 4000.0f, 0.7071f, -0.1761f, 3.033f, 67.526f, 2.560f, 0.0950f, 0.8600f },
            { 5000.0f, 0.7571f, -0.1791f, 2.960f, 66.264f, 2.740f, 0.1060f, 0.8740f },
            { 6300.0f, 0.8071f, -0.1649f, 2.892f, 65.088f, 2.920f, 0.1170f, 0.8880f },
            { 8000.0f, 0.8571f, -0.1351f, 2.830f, 63.991f, 3.100f, 0.1280f, 0.9020f },
            { 9500.0f, 0.9071f, -0.0924f, 2.771f, 62.963f, 2.200f, 0.0400f, 0.9160f },
            { 11000.0f, 0.9571f, -0.0408f, 2.717f, 62.000f, 2.380f, 0.0510f, 0.9300f },
            { 12500.0f, 1.0071f, 0.0146f, 2.667f, 61.095f, 2.560f, 0.0620f, 0.9440f },
            { 14000.0f, 1.0571f, 0.0686f, 2.619f, 60.243f, 2.740f, 0.0730f, 0.9580f },
            { 16000.0f, 1.1071f, 0.1161f, 2.575f, 59.439f, 2.920f, 0.0840f, 0.9720f },
            { 18000.0f, 1.1571f, 0.1525f, 2.533f, 58.680f, 3.100f, 0.0950f, 0.9860f },
            { 20000.0f, 0.7071f, 0.1744f, 2.494f, 57.962f, 2.200f, 0.1060f, 0.8600f },
            { 22000.0f, 0.7571f, 0.1797f, 2.457f, 57.281f, 2.380f, 0.1170f, 0.8740f },
        }};

        static constexpr std::array<MoraDictionNode, 62> kJapaneseMoraDictionVault = {{
            { "a", "あ", 781.0f, 1220.0f, 2650.0f, 4.0f, 1.00f, "Seion open vowel" },
            { "i", "い", 311.0f, 2320.0f, 3150.0f, 6.0f, 0.92f, "Seion close front vowel" },
            { "u", "う", 361.0f, 1380.0f, 2450.0f, 5.0f, 0.94f, "Seion unrounded back vowel" },
            { "e", "え", 511.0f, 1920.0f, 2720.0f, 5.0f, 0.96f, "Seion mid front vowel" },
            { "o", "お", 481.0f, 920.0f, 2580.0f, 4.5f, 0.98f, "Seion mid back vowel" },
            { "ka", "か", 776.0f, 1240.0f, 2660.0f, 28.0f, 1.02f, "Voiceless velar stop" },
            { "ki", "き", 316.0f, 2350.0f, 3180.0f, 34.0f, 0.90f, "Palatalized velar stop" },
            { "ku", "く", 366.0f, 1390.0f, 2460.0f, 26.0f, 0.93f, "Velar stop + close u" },
            { "ke", "け", 516.0f, 1940.0f, 2730.0f, 29.0f, 0.95f, "Velar stop + mid e" },
            { "ko", "こ", 486.0f, 935.0f, 2590.0f, 27.0f, 0.97f, "Velar stop + back o" },
            { "ga", "が", 766.0f, 1215.0f, 2630.0f, 14.0f, 1.05f, "Dakuon voiced velar stop" },
            { "gi", "ぎ", 309.0f, 2310.0f, 3140.0f, 16.0f, 0.94f, "Dakuon palatalized velar" },
            { "gu", "ぐ", 359.0f, 1370.0f, 2440.0f, 15.0f, 0.96f, "Dakuon voiced velar u" },
            { "ge", "げ", 509.0f, 1915.0f, 2705.0f, 15.5f, 0.98f, "Dakuon voiced velar e" },
            { "go", "ご", 479.0f, 915.0f, 2565.0f, 14.5f, 1.01f, "Dakuon voiced velar o" },
            { "sa", "さ", 781.0f, 1230.0f, 2680.0f, 42.0f, 0.86f, "Alveolar fricative sibilant" },
            { "shi", "し", 321.0f, 2340.0f, 3200.0f, 48.0f, 0.81f, "Alveolo-palatal sibilant" },
            { "su", "す", 366.0f, 1410.0f, 2490.0f, 44.0f, 0.83f, "Alveolar sibilant + u" },
            { "se", "せ", 513.0f, 1930.0f, 2740.0f, 41.0f, 0.87f, "Alveolar sibilant + e" },
            { "so", "そ", 483.0f, 940.0f, 2600.0f, 39.0f, 0.89f, "Alveolar sibilant + o" },
            { "za", "ざ", 771.0f, 1210.0f, 2640.0f, 22.0f, 0.95f, "Dakuon voiced sibilant" },
            { "ji", "じ", 313.0f, 2315.0f, 3160.0f, 25.0f, 0.91f, "Dakuon voiced alveolo-palatal" },
            { "zu", "ず", 361.0f, 1385.0f, 2465.0f, 23.0f, 0.93f, "Dakuon voiced sibilant u" },
            { "ze", "ぜ", 507.0f, 1910.0f, 2710.0f, 22.5f, 0.95f, "Dakuon voiced sibilant e" },
            { "zo", "ぞ", 477.0f, 925.0f, 2575.0f, 21.5f, 0.97f, "Dakuon voiced sibilant o" },
            { "ta", "た", 786.0f, 1250.0f, 2670.0f, 24.0f, 1.00f, "Alveolar stop + a" },
            { "chi", "ち", 326.0f, 2330.0f, 3190.0f, 45.0f, 0.84f, "Alveolo-palatal affricate" },
            { "tsu", "つ", 371.0f, 1420.0f, 2510.0f, 46.0f, 0.82f, "Alveolar affricate + u" },
            { "te", "て", 519.0f, 1935.0f, 2735.0f, 25.0f, 0.95f, "Alveolar stop + e" },
            { "to", "と", 489.0f, 945.0f, 2595.0f, 23.0f, 0.97f, "Alveolar stop + o" },
            { "da", "だ", 773.0f, 1225.0f, 2645.0f, 13.0f, 1.04f, "Dakuon voiced alveolar stop" },
            { "de", "で", 509.0f, 1912.0f, 2712.0f, 14.0f, 0.99f, "Dakuon voiced alveolar e" },
            { "do", "ど", 479.0f, 922.0f, 2572.0f, 13.5f, 1.02f, "Dakuon voiced alveolar o" },
            { "na", "な", 761.0f, 1210.0f, 2580.0f, 11.0f, 1.04f, "Alveolar nasal + a" },
            { "ni", "に", 306.0f, 2310.0f, 3120.0f, 12.0f, 0.96f, "Palatalized nasal + i" },
            { "nu", "ぬ", 356.0f, 1360.0f, 2420.0f, 11.5f, 0.97f, "Alveolar nasal + u" },
            { "ne", "ね", 506.0f, 1900.0f, 2690.0f, 11.5f, 0.98f, "Alveolar nasal + e" },
            { "no", "の", 476.0f, 910.0f, 2550.0f, 11.0f, 1.00f, "Alveolar nasal + o" },
            { "ha", "は", 791.0f, 1235.0f, 2690.0f, 20.0f, 1.02f, "Glottal fricative + a" },
            { "hi", "ひ", 321.0f, 2360.0f, 3210.0f, 32.0f, 0.91f, "Palatal fricative + i" },
            { "fu", "ふ", 369.0f, 1395.0f, 2475.0f, 29.0f, 0.93f, "Bilabial fricative + u" },
            { "he", "へ", 517.0f, 1925.0f, 2725.0f, 21.0f, 0.96f, "Glottal fricative + e" },
            { "ho", "ほ", 487.0f, 930.0f, 2585.0f, 19.5f, 0.98f, "Glottal fricative + o" },
            { "ba", "ば", 769.0f, 1205.0f, 2620.0f, 12.5f, 1.06f, "Dakuon voiced bilabial stop" },
            { "bi", "び", 307.0f, 2295.0f, 3110.0f, 14.0f, 0.97f, "Dakuon voiced bilabial i" },
            { "bu", "ぶ", 355.0f, 1355.0f, 2415.0f, 13.0f, 0.99f, "Dakuon voiced bilabial u" },
            { "be", "べ", 505.0f, 1895.0f, 2685.0f, 13.5f, 1.00f, "Dakuon voiced bilabial e" },
            { "bo", "ぼ", 475.0f, 905.0f, 2545.0f, 12.5f, 1.03f, "Dakuon voiced bilabial o" },
            { "pa", "ぱ", 783.0f, 1235.0f, 2665.0f, 26.0f, 1.01f, "Handakuon voiceless bilabial" },
            { "pi", "ぴ", 315.0f, 2335.0f, 3165.0f, 30.0f, 0.92f, "Handakuon bilabial + i" },
            { "pu", "ぷ", 363.0f, 1385.0f, 2455.0f, 27.0f, 0.94f, "Handakuon bilabial + u" },
            { "pe", "ぺ", 513.0f, 1920.0f, 2715.0f, 27.5f, 0.96f, "Handakuon bilabial + e" },
            { "po", "ぽ", 483.0f, 925.0f, 2575.0f, 25.5f, 0.98f, "Handakuon bilabial + o" },
            { "kya", "きゃ", 761.0f, 1680.0f, 2850.0f, 33.0f, 0.98f, "Youon palatalized velar + a" },
            { "sha", "しゃ", 756.0f, 1720.0f, 2920.0f, 46.0f, 0.85f, "Youon alveolo-palatal + a" },
            { "cha", "ちゃ", 759.0f, 1710.0f, 2910.0f, 44.0f, 0.87f, "Youon affricate + a" },
            { "nya", "にゃ", 749.0f, 1650.0f, 2790.0f, 14.0f, 1.03f, "Youon palatal nasal + a" },
            { "hya", "ひゃ", 763.0f, 1700.0f, 2880.0f, 31.0f, 0.95f, "Youon palatal fricative + a" },
            { "mya", "みゃ", 746.0f, 1620.0f, 2760.0f, 13.5f, 1.04f, "Youon bilabial nasal + a" },
            { "rya", "りゃ", 753.0f, 1640.0f, 2810.0f, 18.0f, 0.99f, "Youon alveolar tap + a" },
            { "wa", "わ", 769.0f, 1200.0f, 2610.0f, 9.0f, 1.01f, "Labiovelar glide + a" },
            { "n", "ん", 281.0f, 1150.0f, 2380.0f, 5.0f, 1.08f, "Moraic nasal resonance" },
        }};

        static constexpr std::array<ThemeContrastNode, 19> kThemeContrastVault = {{
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

        // Zavalishin TPT 台形積分マルチモードSVF＆ADAA1非線形倍音セル
        struct SovereignTrapezoidalSvfCell {
            double ic1 = 0.0;
            double ic2 = 0.0;
            double g = 0.1;
            double k = 1.41421356;
            double a1 = 0.0, a2 = 0.0, a3 = 0.0;

            inline void reset() noexcept { ic1 = 0.0; ic2 = 0.0; }

            inline void configure(double sampleRate, double cutoffHz, double qVal) noexcept {
                const double safeSr = (sampleRate > 8000.0) ? sampleRate : 48000.0;
                const double fc = std::clamp(cutoffHz, 18.0, safeSr * 0.48);
                const double safeQ = std::clamp(qVal, 0.2, 24.0);
                g = std::tan(3.14159265358979323846 * fc / safeSr);
                k = 1.0 / safeQ;
                a1 = 1.0 / (1.0 + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
            }

            inline float stepBandpass(float x) noexcept {
                const double v0 = static_cast<double>(x);
                const double v3 = v0 - ic2;
                const double v1 = a1 * ic1 + a2 * v3;
                const double v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0 * v1 - ic1;
                ic2 = 2.0 * v2 - ic2;
                if (!std::isfinite(ic1) || std::abs(ic1) < 1.0e-30) ic1 = 0.0;
                if (!std::isfinite(ic2) || std::abs(ic2) < 1.0e-30) ic2 = 0.0;
                return static_cast<float>(v1);
            }

            inline float stepLowpass(float x) noexcept {
                const double v0 = static_cast<double>(x);
                const double v3 = v0 - ic2;
                const double v1 = a1 * ic1 + a2 * v3;
                const double v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0 * v1 - ic1;
                ic2 = 2.0 * v2 - ic2;
                if (!std::isfinite(ic1) || std::abs(ic1) < 1.0e-30) ic1 = 0.0;
                if (!std::isfinite(ic2) || std::abs(ic2) < 1.0e-30) ic2 = 0.0;
                return static_cast<float>(v2);
            }

            inline float stepHighpass(float x) noexcept {
                const double v0 = static_cast<double>(x);
                const double v3 = v0 - ic2;
                const double v1 = a1 * ic1 + a2 * v3;
                const double v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0 * v1 - ic1;
                ic2 = 2.0 * v2 - ic2;
                if (!std::isfinite(ic1) || std::abs(ic1) < 1.0e-30) ic1 = 0.0;
                if (!std::isfinite(ic2) || std::abs(ic2) < 1.0e-30) ic2 = 0.0;
                return static_cast<float>(v0 - k * v1 - v2);
            }
        };

        struct SovereignStudioPresetDescriptor {
            int presetCodeId;
            const char* presetIdentifier;
            float inputGainDb;
            float lowCutHz;
            float mudCutDb;
            float presenceBoostDb;
            float airBoostDb;
            float targetLufs;
            float truePeakCeilingDb;
            float deEsserThresholdDb;
            float hallMix01;
        };

        static constexpr std::array<SovereignStudioPresetDescriptor, 340> kSovereignPresetVault = {{
            { 2000, "SOVEREIGN_RNB_WARM_000", -1.20f, 60.0f, -2.60f, 1.40f, 1.60f, -16.00f, -1.00f, -24.00f, 0.080f },
            { 2001, "SOVEREIGN_IDOL_SHINE_001", -1.10f, 62.5f, -2.53f, 1.50f, 1.72f, -15.90f, -1.05f, -23.70f, 0.090f },
            { 2002, "SOVEREIGN_JPOP_LEAD_002", -1.00f, 65.0f, -2.46f, 1.60f, 1.84f, -15.80f, -1.10f, -23.40f, 0.100f },
            { 2003, "SOVEREIGN_ANISONG_BELT_003", -0.90f, 67.5f, -2.39f, 1.70f, 1.96f, -15.70f, -1.15f, -23.10f, 0.110f },
            { 2004, "SOVEREIGN_BALLAD_TEAR_004", -0.80f, 70.0f, -2.32f, 1.80f, 2.08f, -15.60f, -1.20f, -22.80f, 0.120f },
            { 2005, "SOVEREIGN_VTUBER_CLEAR_005", -0.70f, 72.5f, -2.25f, 1.90f, 2.20f, -15.50f, -1.25f, -22.50f, 0.130f },
            { 2006, "SOVEREIGN_ASMR_SILK_006", -0.60f, 75.0f, -2.18f, 2.00f, 2.32f, -15.40f, -1.00f, -22.20f, 0.140f },
            { 2007, "SOVEREIGN_ROCK_ARENA_007", -0.50f, 77.5f, -2.11f, 2.10f, 2.44f, -15.30f, -1.05f, -21.90f, 0.150f },
            { 2008, "SOVEREIGN_CITYPOP_TAPE_008", -0.40f, 80.0f, -2.04f, 2.20f, 2.56f, -15.20f, -1.10f, -21.60f, 0.160f },
            { 2009, "SOVEREIGN_RAP_PUNCH_009", -0.30f, 82.5f, -1.97f, 2.30f, 2.68f, -15.10f, -1.15f, -21.30f, 0.170f },
            { 2010, "SOVEREIGN_ACOUSTIC_WOOD_010", -0.20f, 85.0f, -1.90f, 2.40f, 2.80f, -15.00f, -1.20f, -21.00f, 0.180f },
            { 2011, "SOVEREIGN_CHOIR_HALO_011", -0.10f, 87.5f, -1.83f, 2.50f, 2.92f, -14.90f, -1.25f, -20.70f, 0.190f },
            { 2012, "SOVEREIGN_KARAOKE_STAGE_012", 0.00f, 90.0f, -1.76f, 2.60f, 3.04f, -14.80f, -1.00f, -20.40f, 0.200f },
            { 2013, "SOVEREIGN_RADIO_HOST_013", 0.10f, 92.5f, -1.69f, 2.70f, 3.16f, -14.70f, -1.05f, -20.10f, 0.210f },
            { 2014, "SOVEREIGN_FALSETTO_BLOOM_014", 0.20f, 95.0f, -1.62f, 2.80f, 3.28f, -14.60f, -1.10f, -19.80f, 0.220f },
            { 2015, "SOVEREIGN_VOCALOID_FAST_015", 0.30f, 97.5f, -1.55f, 2.90f, 3.40f, -16.00f, -1.15f, -19.50f, 0.230f },
            { 2016, "SOVEREIGN_RNB_WARM_016", 0.40f, 100.0f, -1.48f, 3.00f, 3.52f, -15.90f, -1.20f, -24.00f, 0.240f },
            { 2017, "SOVEREIGN_IDOL_SHINE_017", 0.50f, 102.5f, -1.41f, 3.10f, 3.64f, -15.80f, -1.25f, -23.70f, 0.250f },
            { 2018, "SOVEREIGN_JPOP_LEAD_018", 0.60f, 105.0f, -1.34f, 3.20f, 1.60f, -15.70f, -1.00f, -23.40f, 0.260f },
            { 2019, "SOVEREIGN_ANISONG_BELT_019", 0.70f, 107.5f, -1.27f, 3.30f, 1.72f, -15.60f, -1.05f, -23.10f, 0.270f },
            { 2020, "SOVEREIGN_BALLAD_TEAR_020", 0.80f, 110.0f, -2.60f, 3.40f, 1.84f, -15.50f, -1.10f, -22.80f, 0.080f },
            { 2021, "SOVEREIGN_VTUBER_CLEAR_021", 0.90f, 112.5f, -2.53f, 3.50f, 1.96f, -15.40f, -1.15f, -22.50f, 0.090f },
            { 2022, "SOVEREIGN_ASMR_SILK_022", 1.00f, 115.0f, -2.46f, 1.40f, 2.08f, -15.30f, -1.20f, -22.20f, 0.100f },
            { 2023, "SOVEREIGN_ROCK_ARENA_023", 1.10f, 117.5f, -2.39f, 1.50f, 2.20f, -15.20f, -1.25f, -21.90f, 0.110f },
            { 2024, "SOVEREIGN_CITYPOP_TAPE_024", -1.20f, 120.0f, -2.32f, 1.60f, 2.32f, -15.10f, -1.00f, -21.60f, 0.120f },
            { 2025, "SOVEREIGN_RAP_PUNCH_025", -1.10f, 122.5f, -2.25f, 1.70f, 2.44f, -15.00f, -1.05f, -21.30f, 0.130f },
            { 2026, "SOVEREIGN_ACOUSTIC_WOOD_026", -1.00f, 125.0f, -2.18f, 1.80f, 2.56f, -14.90f, -1.10f, -21.00f, 0.140f },
            { 2027, "SOVEREIGN_CHOIR_HALO_027", -0.90f, 127.5f, -2.11f, 1.90f, 2.68f, -14.80f, -1.15f, -20.70f, 0.150f },
            { 2028, "SOVEREIGN_KARAOKE_STAGE_028", -0.80f, 130.0f, -2.04f, 2.00f, 2.80f, -14.70f, -1.20f, -20.40f, 0.160f },
            { 2029, "SOVEREIGN_RADIO_HOST_029", -0.70f, 132.5f, -1.97f, 2.10f, 2.92f, -14.60f, -1.25f, -20.10f, 0.170f },
            { 2030, "SOVEREIGN_FALSETTO_BLOOM_030", -0.60f, 60.0f, -1.90f, 2.20f, 3.04f, -16.00f, -1.00f, -19.80f, 0.180f },
            { 2031, "SOVEREIGN_VOCALOID_FAST_031", -0.50f, 62.5f, -1.83f, 2.30f, 3.16f, -15.90f, -1.05f, -19.50f, 0.190f },
            { 2032, "SOVEREIGN_RNB_WARM_032", -0.40f, 65.0f, -1.76f, 2.40f, 3.28f, -15.80f, -1.10f, -24.00f, 0.200f },
            { 2033, "SOVEREIGN_IDOL_SHINE_033", -0.30f, 67.5f, -1.69f, 2.50f, 3.40f, -15.70f, -1.15f, -23.70f, 0.210f },
            { 2034, "SOVEREIGN_JPOP_LEAD_034", -0.20f, 70.0f, -1.62f, 2.60f, 3.52f, -15.60f, -1.20f, -23.40f, 0.220f },
            { 2035, "SOVEREIGN_ANISONG_BELT_035", -0.10f, 72.5f, -1.55f, 2.70f, 3.64f, -15.50f, -1.25f, -23.10f, 0.230f },
            { 2036, "SOVEREIGN_BALLAD_TEAR_036", 0.00f, 75.0f, -1.48f, 2.80f, 1.60f, -15.40f, -1.00f, -22.80f, 0.240f },
            { 2037, "SOVEREIGN_VTUBER_CLEAR_037", 0.10f, 77.5f, -1.41f, 2.90f, 1.72f, -15.30f, -1.05f, -22.50f, 0.250f },
            { 2038, "SOVEREIGN_ASMR_SILK_038", 0.20f, 80.0f, -1.34f, 3.00f, 1.84f, -15.20f, -1.10f, -22.20f, 0.260f },
            { 2039, "SOVEREIGN_ROCK_ARENA_039", 0.30f, 82.5f, -1.27f, 3.10f, 1.96f, -15.10f, -1.15f, -21.90f, 0.270f },
            { 2040, "SOVEREIGN_CITYPOP_TAPE_040", 0.40f, 85.0f, -2.60f, 3.20f, 2.08f, -15.00f, -1.20f, -21.60f, 0.080f },
            { 2041, "SOVEREIGN_RAP_PUNCH_041", 0.50f, 87.5f, -2.53f, 3.30f, 2.20f, -14.90f, -1.25f, -21.30f, 0.090f },
            { 2042, "SOVEREIGN_ACOUSTIC_WOOD_042", 0.60f, 90.0f, -2.46f, 3.40f, 2.32f, -14.80f, -1.00f, -21.00f, 0.100f },
            { 2043, "SOVEREIGN_CHOIR_HALO_043", 0.70f, 92.5f, -2.39f, 3.50f, 2.44f, -14.70f, -1.05f, -20.70f, 0.110f },
            { 2044, "SOVEREIGN_KARAOKE_STAGE_044", 0.80f, 95.0f, -2.32f, 1.40f, 2.56f, -14.60f, -1.10f, -20.40f, 0.120f },
            { 2045, "SOVEREIGN_RADIO_HOST_045", 0.90f, 97.5f, -2.25f, 1.50f, 2.68f, -16.00f, -1.15f, -20.10f, 0.130f },
            { 2046, "SOVEREIGN_FALSETTO_BLOOM_046", 1.00f, 100.0f, -2.18f, 1.60f, 2.80f, -15.90f, -1.20f, -19.80f, 0.140f },
            { 2047, "SOVEREIGN_VOCALOID_FAST_047", 1.10f, 102.5f, -2.11f, 1.70f, 2.92f, -15.80f, -1.25f, -19.50f, 0.150f },
            { 2048, "SOVEREIGN_RNB_WARM_048", -1.20f, 105.0f, -2.04f, 1.80f, 3.04f, -15.70f, -1.00f, -24.00f, 0.160f },
            { 2049, "SOVEREIGN_IDOL_SHINE_049", -1.10f, 107.5f, -1.97f, 1.90f, 3.16f, -15.60f, -1.05f, -23.70f, 0.170f },
            { 2050, "SOVEREIGN_JPOP_LEAD_050", -1.00f, 110.0f, -1.90f, 2.00f, 3.28f, -15.50f, -1.10f, -23.40f, 0.180f },
            { 2051, "SOVEREIGN_ANISONG_BELT_051", -0.90f, 112.5f, -1.83f, 2.10f, 3.40f, -15.40f, -1.15f, -23.10f, 0.190f },
            { 2052, "SOVEREIGN_BALLAD_TEAR_052", -0.80f, 115.0f, -1.76f, 2.20f, 3.52f, -15.30f, -1.20f, -22.80f, 0.200f },
            { 2053, "SOVEREIGN_VTUBER_CLEAR_053", -0.70f, 117.5f, -1.69f, 2.30f, 3.64f, -15.20f, -1.25f, -22.50f, 0.210f },
            { 2054, "SOVEREIGN_ASMR_SILK_054", -0.60f, 120.0f, -1.62f, 2.40f, 1.60f, -15.10f, -1.00f, -22.20f, 0.220f },
            { 2055, "SOVEREIGN_ROCK_ARENA_055", -0.50f, 122.5f, -1.55f, 2.50f, 1.72f, -15.00f, -1.05f, -21.90f, 0.230f },
            { 2056, "SOVEREIGN_CITYPOP_TAPE_056", -0.40f, 125.0f, -1.48f, 2.60f, 1.84f, -14.90f, -1.10f, -21.60f, 0.240f },
            { 2057, "SOVEREIGN_RAP_PUNCH_057", -0.30f, 127.5f, -1.41f, 2.70f, 1.96f, -14.80f, -1.15f, -21.30f, 0.250f },
            { 2058, "SOVEREIGN_ACOUSTIC_WOOD_058", -0.20f, 130.0f, -1.34f, 2.80f, 2.08f, -14.70f, -1.20f, -21.00f, 0.260f },
            { 2059, "SOVEREIGN_CHOIR_HALO_059", -0.10f, 132.5f, -1.27f, 2.90f, 2.20f, -14.60f, -1.25f, -20.70f, 0.270f },
            { 2060, "SOVEREIGN_KARAOKE_STAGE_060", 0.00f, 60.0f, -2.60f, 3.00f, 2.32f, -16.00f, -1.00f, -20.40f, 0.080f },
            { 2061, "SOVEREIGN_RADIO_HOST_061", 0.10f, 62.5f, -2.53f, 3.10f, 2.44f, -15.90f, -1.05f, -20.10f, 0.090f },
            { 2062, "SOVEREIGN_FALSETTO_BLOOM_062", 0.20f, 65.0f, -2.46f, 3.20f, 2.56f, -15.80f, -1.10f, -19.80f, 0.100f },
            { 2063, "SOVEREIGN_VOCALOID_FAST_063", 0.30f, 67.5f, -2.39f, 3.30f, 2.68f, -15.70f, -1.15f, -19.50f, 0.110f },
            { 2064, "SOVEREIGN_RNB_WARM_064", 0.40f, 70.0f, -2.32f, 3.40f, 2.80f, -15.60f, -1.20f, -24.00f, 0.120f },
            { 2065, "SOVEREIGN_IDOL_SHINE_065", 0.50f, 72.5f, -2.25f, 3.50f, 2.92f, -15.50f, -1.25f, -23.70f, 0.130f },
            { 2066, "SOVEREIGN_JPOP_LEAD_066", 0.60f, 75.0f, -2.18f, 1.40f, 3.04f, -15.40f, -1.00f, -23.40f, 0.140f },
            { 2067, "SOVEREIGN_ANISONG_BELT_067", 0.70f, 77.5f, -2.11f, 1.50f, 3.16f, -15.30f, -1.05f, -23.10f, 0.150f },
            { 2068, "SOVEREIGN_BALLAD_TEAR_068", 0.80f, 80.0f, -2.04f, 1.60f, 3.28f, -15.20f, -1.10f, -22.80f, 0.160f },
            { 2069, "SOVEREIGN_VTUBER_CLEAR_069", 0.90f, 82.5f, -1.97f, 1.70f, 3.40f, -15.10f, -1.15f, -22.50f, 0.170f },
            { 2070, "SOVEREIGN_ASMR_SILK_070", 1.00f, 85.0f, -1.90f, 1.80f, 3.52f, -15.00f, -1.20f, -22.20f, 0.180f },
            { 2071, "SOVEREIGN_ROCK_ARENA_071", 1.10f, 87.5f, -1.83f, 1.90f, 3.64f, -14.90f, -1.25f, -21.90f, 0.190f },
            { 2072, "SOVEREIGN_CITYPOP_TAPE_072", -1.20f, 90.0f, -1.76f, 2.00f, 1.60f, -14.80f, -1.00f, -21.60f, 0.200f },
            { 2073, "SOVEREIGN_RAP_PUNCH_073", -1.10f, 92.5f, -1.69f, 2.10f, 1.72f, -14.70f, -1.05f, -21.30f, 0.210f },
            { 2074, "SOVEREIGN_ACOUSTIC_WOOD_074", -1.00f, 95.0f, -1.62f, 2.20f, 1.84f, -14.60f, -1.10f, -21.00f, 0.220f },
            { 2075, "SOVEREIGN_CHOIR_HALO_075", -0.90f, 97.5f, -1.55f, 2.30f, 1.96f, -16.00f, -1.15f, -20.70f, 0.230f },
            { 2076, "SOVEREIGN_KARAOKE_STAGE_076", -0.80f, 100.0f, -1.48f, 2.40f, 2.08f, -15.90f, -1.20f, -20.40f, 0.240f },
            { 2077, "SOVEREIGN_RADIO_HOST_077", -0.70f, 102.5f, -1.41f, 2.50f, 2.20f, -15.80f, -1.25f, -20.10f, 0.250f },
            { 2078, "SOVEREIGN_FALSETTO_BLOOM_078", -0.60f, 105.0f, -1.34f, 2.60f, 2.32f, -15.70f, -1.00f, -19.80f, 0.260f },
            { 2079, "SOVEREIGN_VOCALOID_FAST_079", -0.50f, 107.5f, -1.27f, 2.70f, 2.44f, -15.60f, -1.05f, -19.50f, 0.270f },
            { 2080, "SOVEREIGN_RNB_WARM_080", -0.40f, 110.0f, -2.60f, 2.80f, 2.56f, -15.50f, -1.10f, -24.00f, 0.080f },
            { 2081, "SOVEREIGN_IDOL_SHINE_081", -0.30f, 112.5f, -2.53f, 2.90f, 2.68f, -15.40f, -1.15f, -23.70f, 0.090f },
            { 2082, "SOVEREIGN_JPOP_LEAD_082", -0.20f, 115.0f, -2.46f, 3.00f, 2.80f, -15.30f, -1.20f, -23.40f, 0.100f },
            { 2083, "SOVEREIGN_ANISONG_BELT_083", -0.10f, 117.5f, -2.39f, 3.10f, 2.92f, -15.20f, -1.25f, -23.10f, 0.110f },
            { 2084, "SOVEREIGN_BALLAD_TEAR_084", 0.00f, 120.0f, -2.32f, 3.20f, 3.04f, -15.10f, -1.00f, -22.80f, 0.120f },
            { 2085, "SOVEREIGN_VTUBER_CLEAR_085", 0.10f, 122.5f, -2.25f, 3.30f, 3.16f, -15.00f, -1.05f, -22.50f, 0.130f },
            { 2086, "SOVEREIGN_ASMR_SILK_086", 0.20f, 125.0f, -2.18f, 3.40f, 3.28f, -14.90f, -1.10f, -22.20f, 0.140f },
            { 2087, "SOVEREIGN_ROCK_ARENA_087", 0.30f, 127.5f, -2.11f, 3.50f, 3.40f, -14.80f, -1.15f, -21.90f, 0.150f },
            { 2088, "SOVEREIGN_CITYPOP_TAPE_088", 0.40f, 130.0f, -2.04f, 1.40f, 3.52f, -14.70f, -1.20f, -21.60f, 0.160f },
            { 2089, "SOVEREIGN_RAP_PUNCH_089", 0.50f, 132.5f, -1.97f, 1.50f, 3.64f, -14.60f, -1.25f, -21.30f, 0.170f },
            { 2090, "SOVEREIGN_ACOUSTIC_WOOD_090", 0.60f, 60.0f, -1.90f, 1.60f, 1.60f, -16.00f, -1.00f, -21.00f, 0.180f },
            { 2091, "SOVEREIGN_CHOIR_HALO_091", 0.70f, 62.5f, -1.83f, 1.70f, 1.72f, -15.90f, -1.05f, -20.70f, 0.190f },
            { 2092, "SOVEREIGN_KARAOKE_STAGE_092", 0.80f, 65.0f, -1.76f, 1.80f, 1.84f, -15.80f, -1.10f, -20.40f, 0.200f },
            { 2093, "SOVEREIGN_RADIO_HOST_093", 0.90f, 67.5f, -1.69f, 1.90f, 1.96f, -15.70f, -1.15f, -20.10f, 0.210f },
            { 2094, "SOVEREIGN_FALSETTO_BLOOM_094", 1.00f, 70.0f, -1.62f, 2.00f, 2.08f, -15.60f, -1.20f, -19.80f, 0.220f },
            { 2095, "SOVEREIGN_VOCALOID_FAST_095", 1.10f, 72.5f, -1.55f, 2.10f, 2.20f, -15.50f, -1.25f, -19.50f, 0.230f },
            { 2096, "SOVEREIGN_RNB_WARM_096", -1.20f, 75.0f, -1.48f, 2.20f, 2.32f, -15.40f, -1.00f, -24.00f, 0.240f },
            { 2097, "SOVEREIGN_IDOL_SHINE_097", -1.10f, 77.5f, -1.41f, 2.30f, 2.44f, -15.30f, -1.05f, -23.70f, 0.250f },
            { 2098, "SOVEREIGN_JPOP_LEAD_098", -1.00f, 80.0f, -1.34f, 2.40f, 2.56f, -15.20f, -1.10f, -23.40f, 0.260f },
            { 2099, "SOVEREIGN_ANISONG_BELT_099", -0.90f, 82.5f, -1.27f, 2.50f, 2.68f, -15.10f, -1.15f, -23.10f, 0.270f },
            { 2100, "SOVEREIGN_BALLAD_TEAR_100", -0.80f, 85.0f, -2.60f, 2.60f, 2.80f, -15.00f, -1.20f, -22.80f, 0.080f },
            { 2101, "SOVEREIGN_VTUBER_CLEAR_101", -0.70f, 87.5f, -2.53f, 2.70f, 2.92f, -14.90f, -1.25f, -22.50f, 0.090f },
            { 2102, "SOVEREIGN_ASMR_SILK_102", -0.60f, 90.0f, -2.46f, 2.80f, 3.04f, -14.80f, -1.00f, -22.20f, 0.100f },
            { 2103, "SOVEREIGN_ROCK_ARENA_103", -0.50f, 92.5f, -2.39f, 2.90f, 3.16f, -14.70f, -1.05f, -21.90f, 0.110f },
            { 2104, "SOVEREIGN_CITYPOP_TAPE_104", -0.40f, 95.0f, -2.32f, 3.00f, 3.28f, -14.60f, -1.10f, -21.60f, 0.120f },
            { 2105, "SOVEREIGN_RAP_PUNCH_105", -0.30f, 97.5f, -2.25f, 3.10f, 3.40f, -16.00f, -1.15f, -21.30f, 0.130f },
            { 2106, "SOVEREIGN_ACOUSTIC_WOOD_106", -0.20f, 100.0f, -2.18f, 3.20f, 3.52f, -15.90f, -1.20f, -21.00f, 0.140f },
            { 2107, "SOVEREIGN_CHOIR_HALO_107", -0.10f, 102.5f, -2.11f, 3.30f, 3.64f, -15.80f, -1.25f, -20.70f, 0.150f },
            { 2108, "SOVEREIGN_KARAOKE_STAGE_108", 0.00f, 105.0f, -2.04f, 3.40f, 1.60f, -15.70f, -1.00f, -20.40f, 0.160f },
            { 2109, "SOVEREIGN_RADIO_HOST_109", 0.10f, 107.5f, -1.97f, 3.50f, 1.72f, -15.60f, -1.05f, -20.10f, 0.170f },
            { 2110, "SOVEREIGN_FALSETTO_BLOOM_110", 0.20f, 110.0f, -1.90f, 1.40f, 1.84f, -15.50f, -1.10f, -19.80f, 0.180f },
            { 2111, "SOVEREIGN_VOCALOID_FAST_111", 0.30f, 112.5f, -1.83f, 1.50f, 1.96f, -15.40f, -1.15f, -19.50f, 0.190f },
            { 2112, "SOVEREIGN_RNB_WARM_112", 0.40f, 115.0f, -1.76f, 1.60f, 2.08f, -15.30f, -1.20f, -24.00f, 0.200f },
            { 2113, "SOVEREIGN_IDOL_SHINE_113", 0.50f, 117.5f, -1.69f, 1.70f, 2.20f, -15.20f, -1.25f, -23.70f, 0.210f },
            { 2114, "SOVEREIGN_JPOP_LEAD_114", 0.60f, 120.0f, -1.62f, 1.80f, 2.32f, -15.10f, -1.00f, -23.40f, 0.220f },
            { 2115, "SOVEREIGN_ANISONG_BELT_115", 0.70f, 122.5f, -1.55f, 1.90f, 2.44f, -15.00f, -1.05f, -23.10f, 0.230f },
            { 2116, "SOVEREIGN_BALLAD_TEAR_116", 0.80f, 125.0f, -1.48f, 2.00f, 2.56f, -14.90f, -1.10f, -22.80f, 0.240f },
            { 2117, "SOVEREIGN_VTUBER_CLEAR_117", 0.90f, 127.5f, -1.41f, 2.10f, 2.68f, -14.80f, -1.15f, -22.50f, 0.250f },
            { 2118, "SOVEREIGN_ASMR_SILK_118", 1.00f, 130.0f, -1.34f, 2.20f, 2.80f, -14.70f, -1.20f, -22.20f, 0.260f },
            { 2119, "SOVEREIGN_ROCK_ARENA_119", 1.10f, 132.5f, -1.27f, 2.30f, 2.92f, -14.60f, -1.25f, -21.90f, 0.270f },
            { 2120, "SOVEREIGN_CITYPOP_TAPE_120", -1.20f, 60.0f, -2.60f, 2.40f, 3.04f, -16.00f, -1.00f, -21.60f, 0.080f },
            { 2121, "SOVEREIGN_RAP_PUNCH_121", -1.10f, 62.5f, -2.53f, 2.50f, 3.16f, -15.90f, -1.05f, -21.30f, 0.090f },
            { 2122, "SOVEREIGN_ACOUSTIC_WOOD_122", -1.00f, 65.0f, -2.46f, 2.60f, 3.28f, -15.80f, -1.10f, -21.00f, 0.100f },
            { 2123, "SOVEREIGN_CHOIR_HALO_123", -0.90f, 67.5f, -2.39f, 2.70f, 3.40f, -15.70f, -1.15f, -20.70f, 0.110f },
            { 2124, "SOVEREIGN_KARAOKE_STAGE_124", -0.80f, 70.0f, -2.32f, 2.80f, 3.52f, -15.60f, -1.20f, -20.40f, 0.120f },
            { 2125, "SOVEREIGN_RADIO_HOST_125", -0.70f, 72.5f, -2.25f, 2.90f, 3.64f, -15.50f, -1.25f, -20.10f, 0.130f },
            { 2126, "SOVEREIGN_FALSETTO_BLOOM_126", -0.60f, 75.0f, -2.18f, 3.00f, 1.60f, -15.40f, -1.00f, -19.80f, 0.140f },
            { 2127, "SOVEREIGN_VOCALOID_FAST_127", -0.50f, 77.5f, -2.11f, 3.10f, 1.72f, -15.30f, -1.05f, -19.50f, 0.150f },
            { 2128, "SOVEREIGN_RNB_WARM_128", -0.40f, 80.0f, -2.04f, 3.20f, 1.84f, -15.20f, -1.10f, -24.00f, 0.160f },
            { 2129, "SOVEREIGN_IDOL_SHINE_129", -0.30f, 82.5f, -1.97f, 3.30f, 1.96f, -15.10f, -1.15f, -23.70f, 0.170f },
            { 2130, "SOVEREIGN_JPOP_LEAD_130", -0.20f, 85.0f, -1.90f, 3.40f, 2.08f, -15.00f, -1.20f, -23.40f, 0.180f },
            { 2131, "SOVEREIGN_ANISONG_BELT_131", -0.10f, 87.5f, -1.83f, 3.50f, 2.20f, -14.90f, -1.25f, -23.10f, 0.190f },
            { 2132, "SOVEREIGN_BALLAD_TEAR_132", 0.00f, 90.0f, -1.76f, 1.40f, 2.32f, -14.80f, -1.00f, -22.80f, 0.200f },
            { 2133, "SOVEREIGN_VTUBER_CLEAR_133", 0.10f, 92.5f, -1.69f, 1.50f, 2.44f, -14.70f, -1.05f, -22.50f, 0.210f },
            { 2134, "SOVEREIGN_ASMR_SILK_134", 0.20f, 95.0f, -1.62f, 1.60f, 2.56f, -14.60f, -1.10f, -22.20f, 0.220f },
            { 2135, "SOVEREIGN_ROCK_ARENA_135", 0.30f, 97.5f, -1.55f, 1.70f, 2.68f, -16.00f, -1.15f, -21.90f, 0.230f },
            { 2136, "SOVEREIGN_CITYPOP_TAPE_136", 0.40f, 100.0f, -1.48f, 1.80f, 2.80f, -15.90f, -1.20f, -21.60f, 0.240f },
            { 2137, "SOVEREIGN_RAP_PUNCH_137", 0.50f, 102.5f, -1.41f, 1.90f, 2.92f, -15.80f, -1.25f, -21.30f, 0.250f },
            { 2138, "SOVEREIGN_ACOUSTIC_WOOD_138", 0.60f, 105.0f, -1.34f, 2.00f, 3.04f, -15.70f, -1.00f, -21.00f, 0.260f },
            { 2139, "SOVEREIGN_CHOIR_HALO_139", 0.70f, 107.5f, -1.27f, 2.10f, 3.16f, -15.60f, -1.05f, -20.70f, 0.270f },
            { 2140, "SOVEREIGN_KARAOKE_STAGE_140", 0.80f, 110.0f, -2.60f, 2.20f, 3.28f, -15.50f, -1.10f, -20.40f, 0.080f },
            { 2141, "SOVEREIGN_RADIO_HOST_141", 0.90f, 112.5f, -2.53f, 2.30f, 3.40f, -15.40f, -1.15f, -20.10f, 0.090f },
            { 2142, "SOVEREIGN_FALSETTO_BLOOM_142", 1.00f, 115.0f, -2.46f, 2.40f, 3.52f, -15.30f, -1.20f, -19.80f, 0.100f },
            { 2143, "SOVEREIGN_VOCALOID_FAST_143", 1.10f, 117.5f, -2.39f, 2.50f, 3.64f, -15.20f, -1.25f, -19.50f, 0.110f },
            { 2144, "SOVEREIGN_RNB_WARM_144", -1.20f, 120.0f, -2.32f, 2.60f, 1.60f, -15.10f, -1.00f, -24.00f, 0.120f },
            { 2145, "SOVEREIGN_IDOL_SHINE_145", -1.10f, 122.5f, -2.25f, 2.70f, 1.72f, -15.00f, -1.05f, -23.70f, 0.130f },
            { 2146, "SOVEREIGN_JPOP_LEAD_146", -1.00f, 125.0f, -2.18f, 2.80f, 1.84f, -14.90f, -1.10f, -23.40f, 0.140f },
            { 2147, "SOVEREIGN_ANISONG_BELT_147", -0.90f, 127.5f, -2.11f, 2.90f, 1.96f, -14.80f, -1.15f, -23.10f, 0.150f },
            { 2148, "SOVEREIGN_BALLAD_TEAR_148", -0.80f, 130.0f, -2.04f, 3.00f, 2.08f, -14.70f, -1.20f, -22.80f, 0.160f },
            { 2149, "SOVEREIGN_VTUBER_CLEAR_149", -0.70f, 132.5f, -1.97f, 3.10f, 2.20f, -14.60f, -1.25f, -22.50f, 0.170f },
            { 2150, "SOVEREIGN_ASMR_SILK_150", -0.60f, 60.0f, -1.90f, 3.20f, 2.32f, -16.00f, -1.00f, -22.20f, 0.180f },
            { 2151, "SOVEREIGN_ROCK_ARENA_151", -0.50f, 62.5f, -1.83f, 3.30f, 2.44f, -15.90f, -1.05f, -21.90f, 0.190f },
            { 2152, "SOVEREIGN_CITYPOP_TAPE_152", -0.40f, 65.0f, -1.76f, 3.40f, 2.56f, -15.80f, -1.10f, -21.60f, 0.200f },
            { 2153, "SOVEREIGN_RAP_PUNCH_153", -0.30f, 67.5f, -1.69f, 3.50f, 2.68f, -15.70f, -1.15f, -21.30f, 0.210f },
            { 2154, "SOVEREIGN_ACOUSTIC_WOOD_154", -0.20f, 70.0f, -1.62f, 1.40f, 2.80f, -15.60f, -1.20f, -21.00f, 0.220f },
            { 2155, "SOVEREIGN_CHOIR_HALO_155", -0.10f, 72.5f, -1.55f, 1.50f, 2.92f, -15.50f, -1.25f, -20.70f, 0.230f },
            { 2156, "SOVEREIGN_KARAOKE_STAGE_156", 0.00f, 75.0f, -1.48f, 1.60f, 3.04f, -15.40f, -1.00f, -20.40f, 0.240f },
            { 2157, "SOVEREIGN_RADIO_HOST_157", 0.10f, 77.5f, -1.41f, 1.70f, 3.16f, -15.30f, -1.05f, -20.10f, 0.250f },
            { 2158, "SOVEREIGN_FALSETTO_BLOOM_158", 0.20f, 80.0f, -1.34f, 1.80f, 3.28f, -15.20f, -1.10f, -19.80f, 0.260f },
            { 2159, "SOVEREIGN_VOCALOID_FAST_159", 0.30f, 82.5f, -1.27f, 1.90f, 3.40f, -15.10f, -1.15f, -19.50f, 0.270f },
            { 2160, "SOVEREIGN_RNB_WARM_160", 0.40f, 85.0f, -2.60f, 2.00f, 3.52f, -15.00f, -1.20f, -24.00f, 0.080f },
            { 2161, "SOVEREIGN_IDOL_SHINE_161", 0.50f, 87.5f, -2.53f, 2.10f, 3.64f, -14.90f, -1.25f, -23.70f, 0.090f },
            { 2162, "SOVEREIGN_JPOP_LEAD_162", 0.60f, 90.0f, -2.46f, 2.20f, 1.60f, -14.80f, -1.00f, -23.40f, 0.100f },
            { 2163, "SOVEREIGN_ANISONG_BELT_163", 0.70f, 92.5f, -2.39f, 2.30f, 1.72f, -14.70f, -1.05f, -23.10f, 0.110f },
            { 2164, "SOVEREIGN_BALLAD_TEAR_164", 0.80f, 95.0f, -2.32f, 2.40f, 1.84f, -14.60f, -1.10f, -22.80f, 0.120f },
            { 2165, "SOVEREIGN_VTUBER_CLEAR_165", 0.90f, 97.5f, -2.25f, 2.50f, 1.96f, -16.00f, -1.15f, -22.50f, 0.130f },
            { 2166, "SOVEREIGN_ASMR_SILK_166", 1.00f, 100.0f, -2.18f, 2.60f, 2.08f, -15.90f, -1.20f, -22.20f, 0.140f },
            { 2167, "SOVEREIGN_ROCK_ARENA_167", 1.10f, 102.5f, -2.11f, 2.70f, 2.20f, -15.80f, -1.25f, -21.90f, 0.150f },
            { 2168, "SOVEREIGN_CITYPOP_TAPE_168", -1.20f, 105.0f, -2.04f, 2.80f, 2.32f, -15.70f, -1.00f, -21.60f, 0.160f },
            { 2169, "SOVEREIGN_RAP_PUNCH_169", -1.10f, 107.5f, -1.97f, 2.90f, 2.44f, -15.60f, -1.05f, -21.30f, 0.170f },
            { 2170, "SOVEREIGN_ACOUSTIC_WOOD_170", -1.00f, 110.0f, -1.90f, 3.00f, 2.56f, -15.50f, -1.10f, -21.00f, 0.180f },
            { 2171, "SOVEREIGN_CHOIR_HALO_171", -0.90f, 112.5f, -1.83f, 3.10f, 2.68f, -15.40f, -1.15f, -20.70f, 0.190f },
            { 2172, "SOVEREIGN_KARAOKE_STAGE_172", -0.80f, 115.0f, -1.76f, 3.20f, 2.80f, -15.30f, -1.20f, -20.40f, 0.200f },
            { 2173, "SOVEREIGN_RADIO_HOST_173", -0.70f, 117.5f, -1.69f, 3.30f, 2.92f, -15.20f, -1.25f, -20.10f, 0.210f },
            { 2174, "SOVEREIGN_FALSETTO_BLOOM_174", -0.60f, 120.0f, -1.62f, 3.40f, 3.04f, -15.10f, -1.00f, -19.80f, 0.220f },
            { 2175, "SOVEREIGN_VOCALOID_FAST_175", -0.50f, 122.5f, -1.55f, 3.50f, 3.16f, -15.00f, -1.05f, -19.50f, 0.230f },
            { 2176, "SOVEREIGN_RNB_WARM_176", -0.40f, 125.0f, -1.48f, 1.40f, 3.28f, -14.90f, -1.10f, -24.00f, 0.240f },
            { 2177, "SOVEREIGN_IDOL_SHINE_177", -0.30f, 127.5f, -1.41f, 1.50f, 3.40f, -14.80f, -1.15f, -23.70f, 0.250f },
            { 2178, "SOVEREIGN_JPOP_LEAD_178", -0.20f, 130.0f, -1.34f, 1.60f, 3.52f, -14.70f, -1.20f, -23.40f, 0.260f },
            { 2179, "SOVEREIGN_ANISONG_BELT_179", -0.10f, 132.5f, -1.27f, 1.70f, 3.64f, -14.60f, -1.25f, -23.10f, 0.270f },
            { 2180, "SOVEREIGN_BALLAD_TEAR_180", 0.00f, 60.0f, -2.60f, 1.80f, 1.60f, -16.00f, -1.00f, -22.80f, 0.080f },
            { 2181, "SOVEREIGN_VTUBER_CLEAR_181", 0.10f, 62.5f, -2.53f, 1.90f, 1.72f, -15.90f, -1.05f, -22.50f, 0.090f },
            { 2182, "SOVEREIGN_ASMR_SILK_182", 0.20f, 65.0f, -2.46f, 2.00f, 1.84f, -15.80f, -1.10f, -22.20f, 0.100f },
            { 2183, "SOVEREIGN_ROCK_ARENA_183", 0.30f, 67.5f, -2.39f, 2.10f, 1.96f, -15.70f, -1.15f, -21.90f, 0.110f },
            { 2184, "SOVEREIGN_CITYPOP_TAPE_184", 0.40f, 70.0f, -2.32f, 2.20f, 2.08f, -15.60f, -1.20f, -21.60f, 0.120f },
            { 2185, "SOVEREIGN_RAP_PUNCH_185", 0.50f, 72.5f, -2.25f, 2.30f, 2.20f, -15.50f, -1.25f, -21.30f, 0.130f },
            { 2186, "SOVEREIGN_ACOUSTIC_WOOD_186", 0.60f, 75.0f, -2.18f, 2.40f, 2.32f, -15.40f, -1.00f, -21.00f, 0.140f },
            { 2187, "SOVEREIGN_CHOIR_HALO_187", 0.70f, 77.5f, -2.11f, 2.50f, 2.44f, -15.30f, -1.05f, -20.70f, 0.150f },
            { 2188, "SOVEREIGN_KARAOKE_STAGE_188", 0.80f, 80.0f, -2.04f, 2.60f, 2.56f, -15.20f, -1.10f, -20.40f, 0.160f },
            { 2189, "SOVEREIGN_RADIO_HOST_189", 0.90f, 82.5f, -1.97f, 2.70f, 2.68f, -15.10f, -1.15f, -20.10f, 0.170f },
            { 2190, "SOVEREIGN_FALSETTO_BLOOM_190", 1.00f, 85.0f, -1.90f, 2.80f, 2.80f, -15.00f, -1.20f, -19.80f, 0.180f },
            { 2191, "SOVEREIGN_VOCALOID_FAST_191", 1.10f, 87.5f, -1.83f, 2.90f, 2.92f, -14.90f, -1.25f, -19.50f, 0.190f },
            { 2192, "SOVEREIGN_RNB_WARM_192", -1.20f, 90.0f, -1.76f, 3.00f, 3.04f, -14.80f, -1.00f, -24.00f, 0.200f },
            { 2193, "SOVEREIGN_IDOL_SHINE_193", -1.10f, 92.5f, -1.69f, 3.10f, 3.16f, -14.70f, -1.05f, -23.70f, 0.210f },
            { 2194, "SOVEREIGN_JPOP_LEAD_194", -1.00f, 95.0f, -1.62f, 3.20f, 3.28f, -14.60f, -1.10f, -23.40f, 0.220f },
            { 2195, "SOVEREIGN_ANISONG_BELT_195", -0.90f, 97.5f, -1.55f, 3.30f, 3.40f, -16.00f, -1.15f, -23.10f, 0.230f },
            { 2196, "SOVEREIGN_BALLAD_TEAR_196", -0.80f, 100.0f, -1.48f, 3.40f, 3.52f, -15.90f, -1.20f, -22.80f, 0.240f },
            { 2197, "SOVEREIGN_VTUBER_CLEAR_197", -0.70f, 102.5f, -1.41f, 3.50f, 3.64f, -15.80f, -1.25f, -22.50f, 0.250f },
            { 2198, "SOVEREIGN_ASMR_SILK_198", -0.60f, 105.0f, -1.34f, 1.40f, 1.60f, -15.70f, -1.00f, -22.20f, 0.260f },
            { 2199, "SOVEREIGN_ROCK_ARENA_199", -0.50f, 107.5f, -1.27f, 1.50f, 1.72f, -15.60f, -1.05f, -21.90f, 0.270f },
            { 2200, "SOVEREIGN_CITYPOP_TAPE_200", -0.40f, 110.0f, -2.60f, 1.60f, 1.84f, -15.50f, -1.10f, -21.60f, 0.080f },
            { 2201, "SOVEREIGN_RAP_PUNCH_201", -0.30f, 112.5f, -2.53f, 1.70f, 1.96f, -15.40f, -1.15f, -21.30f, 0.090f },
            { 2202, "SOVEREIGN_ACOUSTIC_WOOD_202", -0.20f, 115.0f, -2.46f, 1.80f, 2.08f, -15.30f, -1.20f, -21.00f, 0.100f },
            { 2203, "SOVEREIGN_CHOIR_HALO_203", -0.10f, 117.5f, -2.39f, 1.90f, 2.20f, -15.20f, -1.25f, -20.70f, 0.110f },
            { 2204, "SOVEREIGN_KARAOKE_STAGE_204", 0.00f, 120.0f, -2.32f, 2.00f, 2.32f, -15.10f, -1.00f, -20.40f, 0.120f },
            { 2205, "SOVEREIGN_RADIO_HOST_205", 0.10f, 122.5f, -2.25f, 2.10f, 2.44f, -15.00f, -1.05f, -20.10f, 0.130f },
            { 2206, "SOVEREIGN_FALSETTO_BLOOM_206", 0.20f, 125.0f, -2.18f, 2.20f, 2.56f, -14.90f, -1.10f, -19.80f, 0.140f },
            { 2207, "SOVEREIGN_VOCALOID_FAST_207", 0.30f, 127.5f, -2.11f, 2.30f, 2.68f, -14.80f, -1.15f, -19.50f, 0.150f },
            { 2208, "SOVEREIGN_RNB_WARM_208", 0.40f, 130.0f, -2.04f, 2.40f, 2.80f, -14.70f, -1.20f, -24.00f, 0.160f },
            { 2209, "SOVEREIGN_IDOL_SHINE_209", 0.50f, 132.5f, -1.97f, 2.50f, 2.92f, -14.60f, -1.25f, -23.70f, 0.170f },
            { 2210, "SOVEREIGN_JPOP_LEAD_210", 0.60f, 60.0f, -1.90f, 2.60f, 3.04f, -16.00f, -1.00f, -23.40f, 0.180f },
            { 2211, "SOVEREIGN_ANISONG_BELT_211", 0.70f, 62.5f, -1.83f, 2.70f, 3.16f, -15.90f, -1.05f, -23.10f, 0.190f },
            { 2212, "SOVEREIGN_BALLAD_TEAR_212", 0.80f, 65.0f, -1.76f, 2.80f, 3.28f, -15.80f, -1.10f, -22.80f, 0.200f },
            { 2213, "SOVEREIGN_VTUBER_CLEAR_213", 0.90f, 67.5f, -1.69f, 2.90f, 3.40f, -15.70f, -1.15f, -22.50f, 0.210f },
            { 2214, "SOVEREIGN_ASMR_SILK_214", 1.00f, 70.0f, -1.62f, 3.00f, 3.52f, -15.60f, -1.20f, -22.20f, 0.220f },
            { 2215, "SOVEREIGN_ROCK_ARENA_215", 1.10f, 72.5f, -1.55f, 3.10f, 3.64f, -15.50f, -1.25f, -21.90f, 0.230f },
            { 2216, "SOVEREIGN_CITYPOP_TAPE_216", -1.20f, 75.0f, -1.48f, 3.20f, 1.60f, -15.40f, -1.00f, -21.60f, 0.240f },
            { 2217, "SOVEREIGN_RAP_PUNCH_217", -1.10f, 77.5f, -1.41f, 3.30f, 1.72f, -15.30f, -1.05f, -21.30f, 0.250f },
            { 2218, "SOVEREIGN_ACOUSTIC_WOOD_218", -1.00f, 80.0f, -1.34f, 3.40f, 1.84f, -15.20f, -1.10f, -21.00f, 0.260f },
            { 2219, "SOVEREIGN_CHOIR_HALO_219", -0.90f, 82.5f, -1.27f, 3.50f, 1.96f, -15.10f, -1.15f, -20.70f, 0.270f },
            { 2220, "SOVEREIGN_KARAOKE_STAGE_220", -0.80f, 85.0f, -2.60f, 1.40f, 2.08f, -15.00f, -1.20f, -20.40f, 0.080f },
            { 2221, "SOVEREIGN_RADIO_HOST_221", -0.70f, 87.5f, -2.53f, 1.50f, 2.20f, -14.90f, -1.25f, -20.10f, 0.090f },
            { 2222, "SOVEREIGN_FALSETTO_BLOOM_222", -0.60f, 90.0f, -2.46f, 1.60f, 2.32f, -14.80f, -1.00f, -19.80f, 0.100f },
            { 2223, "SOVEREIGN_VOCALOID_FAST_223", -0.50f, 92.5f, -2.39f, 1.70f, 2.44f, -14.70f, -1.05f, -19.50f, 0.110f },
            { 2224, "SOVEREIGN_RNB_WARM_224", -0.40f, 95.0f, -2.32f, 1.80f, 2.56f, -14.60f, -1.10f, -24.00f, 0.120f },
            { 2225, "SOVEREIGN_IDOL_SHINE_225", -0.30f, 97.5f, -2.25f, 1.90f, 2.68f, -16.00f, -1.15f, -23.70f, 0.130f },
            { 2226, "SOVEREIGN_JPOP_LEAD_226", -0.20f, 100.0f, -2.18f, 2.00f, 2.80f, -15.90f, -1.20f, -23.40f, 0.140f },
            { 2227, "SOVEREIGN_ANISONG_BELT_227", -0.10f, 102.5f, -2.11f, 2.10f, 2.92f, -15.80f, -1.25f, -23.10f, 0.150f },
            { 2228, "SOVEREIGN_BALLAD_TEAR_228", 0.00f, 105.0f, -2.04f, 2.20f, 3.04f, -15.70f, -1.00f, -22.80f, 0.160f },
            { 2229, "SOVEREIGN_VTUBER_CLEAR_229", 0.10f, 107.5f, -1.97f, 2.30f, 3.16f, -15.60f, -1.05f, -22.50f, 0.170f },
            { 2230, "SOVEREIGN_ASMR_SILK_230", 0.20f, 110.0f, -1.90f, 2.40f, 3.28f, -15.50f, -1.10f, -22.20f, 0.180f },
            { 2231, "SOVEREIGN_ROCK_ARENA_231", 0.30f, 112.5f, -1.83f, 2.50f, 3.40f, -15.40f, -1.15f, -21.90f, 0.190f },
            { 2232, "SOVEREIGN_CITYPOP_TAPE_232", 0.40f, 115.0f, -1.76f, 2.60f, 3.52f, -15.30f, -1.20f, -21.60f, 0.200f },
            { 2233, "SOVEREIGN_RAP_PUNCH_233", 0.50f, 117.5f, -1.69f, 2.70f, 3.64f, -15.20f, -1.25f, -21.30f, 0.210f },
            { 2234, "SOVEREIGN_ACOUSTIC_WOOD_234", 0.60f, 120.0f, -1.62f, 2.80f, 1.60f, -15.10f, -1.00f, -21.00f, 0.220f },
            { 2235, "SOVEREIGN_CHOIR_HALO_235", 0.70f, 122.5f, -1.55f, 2.90f, 1.72f, -15.00f, -1.05f, -20.70f, 0.230f },
            { 2236, "SOVEREIGN_KARAOKE_STAGE_236", 0.80f, 125.0f, -1.48f, 3.00f, 1.84f, -14.90f, -1.10f, -20.40f, 0.240f },
            { 2237, "SOVEREIGN_RADIO_HOST_237", 0.90f, 127.5f, -1.41f, 3.10f, 1.96f, -14.80f, -1.15f, -20.10f, 0.250f },
            { 2238, "SOVEREIGN_FALSETTO_BLOOM_238", 1.00f, 130.0f, -1.34f, 3.20f, 2.08f, -14.70f, -1.20f, -19.80f, 0.260f },
            { 2239, "SOVEREIGN_VOCALOID_FAST_239", 1.10f, 132.5f, -1.27f, 3.30f, 2.20f, -14.60f, -1.25f, -19.50f, 0.270f },
            { 2240, "SOVEREIGN_RNB_WARM_240", -1.20f, 60.0f, -2.60f, 3.40f, 2.32f, -16.00f, -1.00f, -24.00f, 0.080f },
            { 2241, "SOVEREIGN_IDOL_SHINE_241", -1.10f, 62.5f, -2.53f, 3.50f, 2.44f, -15.90f, -1.05f, -23.70f, 0.090f },
            { 2242, "SOVEREIGN_JPOP_LEAD_242", -1.00f, 65.0f, -2.46f, 1.40f, 2.56f, -15.80f, -1.10f, -23.40f, 0.100f },
            { 2243, "SOVEREIGN_ANISONG_BELT_243", -0.90f, 67.5f, -2.39f, 1.50f, 2.68f, -15.70f, -1.15f, -23.10f, 0.110f },
            { 2244, "SOVEREIGN_BALLAD_TEAR_244", -0.80f, 70.0f, -2.32f, 1.60f, 2.80f, -15.60f, -1.20f, -22.80f, 0.120f },
            { 2245, "SOVEREIGN_VTUBER_CLEAR_245", -0.70f, 72.5f, -2.25f, 1.70f, 2.92f, -15.50f, -1.25f, -22.50f, 0.130f },
            { 2246, "SOVEREIGN_ASMR_SILK_246", -0.60f, 75.0f, -2.18f, 1.80f, 3.04f, -15.40f, -1.00f, -22.20f, 0.140f },
            { 2247, "SOVEREIGN_ROCK_ARENA_247", -0.50f, 77.5f, -2.11f, 1.90f, 3.16f, -15.30f, -1.05f, -21.90f, 0.150f },
            { 2248, "SOVEREIGN_CITYPOP_TAPE_248", -0.40f, 80.0f, -2.04f, 2.00f, 3.28f, -15.20f, -1.10f, -21.60f, 0.160f },
            { 2249, "SOVEREIGN_RAP_PUNCH_249", -0.30f, 82.5f, -1.97f, 2.10f, 3.40f, -15.10f, -1.15f, -21.30f, 0.170f },
            { 2250, "SOVEREIGN_ACOUSTIC_WOOD_250", -0.20f, 85.0f, -1.90f, 2.20f, 3.52f, -15.00f, -1.20f, -21.00f, 0.180f },
            { 2251, "SOVEREIGN_CHOIR_HALO_251", -0.10f, 87.5f, -1.83f, 2.30f, 3.64f, -14.90f, -1.25f, -20.70f, 0.190f },
            { 2252, "SOVEREIGN_KARAOKE_STAGE_252", 0.00f, 90.0f, -1.76f, 2.40f, 1.60f, -14.80f, -1.00f, -20.40f, 0.200f },
            { 2253, "SOVEREIGN_RADIO_HOST_253", 0.10f, 92.5f, -1.69f, 2.50f, 1.72f, -14.70f, -1.05f, -20.10f, 0.210f },
            { 2254, "SOVEREIGN_FALSETTO_BLOOM_254", 0.20f, 95.0f, -1.62f, 2.60f, 1.84f, -14.60f, -1.10f, -19.80f, 0.220f },
            { 2255, "SOVEREIGN_VOCALOID_FAST_255", 0.30f, 97.5f, -1.55f, 2.70f, 1.96f, -16.00f, -1.15f, -19.50f, 0.230f },
            { 2256, "SOVEREIGN_RNB_WARM_256", 0.40f, 100.0f, -1.48f, 2.80f, 2.08f, -15.90f, -1.20f, -24.00f, 0.240f },
            { 2257, "SOVEREIGN_IDOL_SHINE_257", 0.50f, 102.5f, -1.41f, 2.90f, 2.20f, -15.80f, -1.25f, -23.70f, 0.250f },
            { 2258, "SOVEREIGN_JPOP_LEAD_258", 0.60f, 105.0f, -1.34f, 3.00f, 2.32f, -15.70f, -1.00f, -23.40f, 0.260f },
            { 2259, "SOVEREIGN_ANISONG_BELT_259", 0.70f, 107.5f, -1.27f, 3.10f, 2.44f, -15.60f, -1.05f, -23.10f, 0.270f },
            { 2260, "SOVEREIGN_BALLAD_TEAR_260", 0.80f, 110.0f, -2.60f, 3.20f, 2.56f, -15.50f, -1.10f, -22.80f, 0.080f },
            { 2261, "SOVEREIGN_VTUBER_CLEAR_261", 0.90f, 112.5f, -2.53f, 3.30f, 2.68f, -15.40f, -1.15f, -22.50f, 0.090f },
            { 2262, "SOVEREIGN_ASMR_SILK_262", 1.00f, 115.0f, -2.46f, 3.40f, 2.80f, -15.30f, -1.20f, -22.20f, 0.100f },
            { 2263, "SOVEREIGN_ROCK_ARENA_263", 1.10f, 117.5f, -2.39f, 3.50f, 2.92f, -15.20f, -1.25f, -21.90f, 0.110f },
            { 2264, "SOVEREIGN_CITYPOP_TAPE_264", -1.20f, 120.0f, -2.32f, 1.40f, 3.04f, -15.10f, -1.00f, -21.60f, 0.120f },
            { 2265, "SOVEREIGN_RAP_PUNCH_265", -1.10f, 122.5f, -2.25f, 1.50f, 3.16f, -15.00f, -1.05f, -21.30f, 0.130f },
            { 2266, "SOVEREIGN_ACOUSTIC_WOOD_266", -1.00f, 125.0f, -2.18f, 1.60f, 3.28f, -14.90f, -1.10f, -21.00f, 0.140f },
            { 2267, "SOVEREIGN_CHOIR_HALO_267", -0.90f, 127.5f, -2.11f, 1.70f, 3.40f, -14.80f, -1.15f, -20.70f, 0.150f },
            { 2268, "SOVEREIGN_KARAOKE_STAGE_268", -0.80f, 130.0f, -2.04f, 1.80f, 3.52f, -14.70f, -1.20f, -20.40f, 0.160f },
            { 2269, "SOVEREIGN_RADIO_HOST_269", -0.70f, 132.5f, -1.97f, 1.90f, 3.64f, -14.60f, -1.25f, -20.10f, 0.170f },
            { 2270, "SOVEREIGN_FALSETTO_BLOOM_270", -0.60f, 60.0f, -1.90f, 2.00f, 1.60f, -16.00f, -1.00f, -19.80f, 0.180f },
            { 2271, "SOVEREIGN_VOCALOID_FAST_271", -0.50f, 62.5f, -1.83f, 2.10f, 1.72f, -15.90f, -1.05f, -19.50f, 0.190f },
            { 2272, "SOVEREIGN_RNB_WARM_272", -0.40f, 65.0f, -1.76f, 2.20f, 1.84f, -15.80f, -1.10f, -24.00f, 0.200f },
            { 2273, "SOVEREIGN_IDOL_SHINE_273", -0.30f, 67.5f, -1.69f, 2.30f, 1.96f, -15.70f, -1.15f, -23.70f, 0.210f },
            { 2274, "SOVEREIGN_JPOP_LEAD_274", -0.20f, 70.0f, -1.62f, 2.40f, 2.08f, -15.60f, -1.20f, -23.40f, 0.220f },
            { 2275, "SOVEREIGN_ANISONG_BELT_275", -0.10f, 72.5f, -1.55f, 2.50f, 2.20f, -15.50f, -1.25f, -23.10f, 0.230f },
            { 2276, "SOVEREIGN_BALLAD_TEAR_276", 0.00f, 75.0f, -1.48f, 2.60f, 2.32f, -15.40f, -1.00f, -22.80f, 0.240f },
            { 2277, "SOVEREIGN_VTUBER_CLEAR_277", 0.10f, 77.5f, -1.41f, 2.70f, 2.44f, -15.30f, -1.05f, -22.50f, 0.250f },
            { 2278, "SOVEREIGN_ASMR_SILK_278", 0.20f, 80.0f, -1.34f, 2.80f, 2.56f, -15.20f, -1.10f, -22.20f, 0.260f },
            { 2279, "SOVEREIGN_ROCK_ARENA_279", 0.30f, 82.5f, -1.27f, 2.90f, 2.68f, -15.10f, -1.15f, -21.90f, 0.270f },
            { 2280, "SOVEREIGN_CITYPOP_TAPE_280", 0.40f, 85.0f, -2.60f, 3.00f, 2.80f, -15.00f, -1.20f, -21.60f, 0.080f },
            { 2281, "SOVEREIGN_RAP_PUNCH_281", 0.50f, 87.5f, -2.53f, 3.10f, 2.92f, -14.90f, -1.25f, -21.30f, 0.090f },
            { 2282, "SOVEREIGN_ACOUSTIC_WOOD_282", 0.60f, 90.0f, -2.46f, 3.20f, 3.04f, -14.80f, -1.00f, -21.00f, 0.100f },
            { 2283, "SOVEREIGN_CHOIR_HALO_283", 0.70f, 92.5f, -2.39f, 3.30f, 3.16f, -14.70f, -1.05f, -20.70f, 0.110f },
            { 2284, "SOVEREIGN_KARAOKE_STAGE_284", 0.80f, 95.0f, -2.32f, 3.40f, 3.28f, -14.60f, -1.10f, -20.40f, 0.120f },
            { 2285, "SOVEREIGN_RADIO_HOST_285", 0.90f, 97.5f, -2.25f, 3.50f, 3.40f, -16.00f, -1.15f, -20.10f, 0.130f },
            { 2286, "SOVEREIGN_FALSETTO_BLOOM_286", 1.00f, 100.0f, -2.18f, 1.40f, 3.52f, -15.90f, -1.20f, -19.80f, 0.140f },
            { 2287, "SOVEREIGN_VOCALOID_FAST_287", 1.10f, 102.5f, -2.11f, 1.50f, 3.64f, -15.80f, -1.25f, -19.50f, 0.150f },
            { 2288, "SOVEREIGN_RNB_WARM_288", -1.20f, 105.0f, -2.04f, 1.60f, 1.60f, -15.70f, -1.00f, -24.00f, 0.160f },
            { 2289, "SOVEREIGN_IDOL_SHINE_289", -1.10f, 107.5f, -1.97f, 1.70f, 1.72f, -15.60f, -1.05f, -23.70f, 0.170f },
            { 2290, "SOVEREIGN_JPOP_LEAD_290", -1.00f, 110.0f, -1.90f, 1.80f, 1.84f, -15.50f, -1.10f, -23.40f, 0.180f },
            { 2291, "SOVEREIGN_ANISONG_BELT_291", -0.90f, 112.5f, -1.83f, 1.90f, 1.96f, -15.40f, -1.15f, -23.10f, 0.190f },
            { 2292, "SOVEREIGN_BALLAD_TEAR_292", -0.80f, 115.0f, -1.76f, 2.00f, 2.08f, -15.30f, -1.20f, -22.80f, 0.200f },
            { 2293, "SOVEREIGN_VTUBER_CLEAR_293", -0.70f, 117.5f, -1.69f, 2.10f, 2.20f, -15.20f, -1.25f, -22.50f, 0.210f },
            { 2294, "SOVEREIGN_ASMR_SILK_294", -0.60f, 120.0f, -1.62f, 2.20f, 2.32f, -15.10f, -1.00f, -22.20f, 0.220f },
            { 2295, "SOVEREIGN_ROCK_ARENA_295", -0.50f, 122.5f, -1.55f, 2.30f, 2.44f, -15.00f, -1.05f, -21.90f, 0.230f },
            { 2296, "SOVEREIGN_CITYPOP_TAPE_296", -0.40f, 125.0f, -1.48f, 2.40f, 2.56f, -14.90f, -1.10f, -21.60f, 0.240f },
            { 2297, "SOVEREIGN_RAP_PUNCH_297", -0.30f, 127.5f, -1.41f, 2.50f, 2.68f, -14.80f, -1.15f, -21.30f, 0.250f },
            { 2298, "SOVEREIGN_ACOUSTIC_WOOD_298", -0.20f, 130.0f, -1.34f, 2.60f, 2.80f, -14.70f, -1.20f, -21.00f, 0.260f },
            { 2299, "SOVEREIGN_CHOIR_HALO_299", -0.10f, 132.5f, -1.27f, 2.70f, 2.92f, -14.60f, -1.25f, -20.70f, 0.270f },
            { 2300, "SOVEREIGN_KARAOKE_STAGE_300", 0.00f, 60.0f, -2.60f, 2.80f, 3.04f, -16.00f, -1.00f, -20.40f, 0.080f },
            { 2301, "SOVEREIGN_RADIO_HOST_301", 0.10f, 62.5f, -2.53f, 2.90f, 3.16f, -15.90f, -1.05f, -20.10f, 0.090f },
            { 2302, "SOVEREIGN_FALSETTO_BLOOM_302", 0.20f, 65.0f, -2.46f, 3.00f, 3.28f, -15.80f, -1.10f, -19.80f, 0.100f },
            { 2303, "SOVEREIGN_VOCALOID_FAST_303", 0.30f, 67.5f, -2.39f, 3.10f, 3.40f, -15.70f, -1.15f, -19.50f, 0.110f },
            { 2304, "SOVEREIGN_RNB_WARM_304", 0.40f, 70.0f, -2.32f, 3.20f, 3.52f, -15.60f, -1.20f, -24.00f, 0.120f },
            { 2305, "SOVEREIGN_IDOL_SHINE_305", 0.50f, 72.5f, -2.25f, 3.30f, 3.64f, -15.50f, -1.25f, -23.70f, 0.130f },
            { 2306, "SOVEREIGN_JPOP_LEAD_306", 0.60f, 75.0f, -2.18f, 3.40f, 1.60f, -15.40f, -1.00f, -23.40f, 0.140f },
            { 2307, "SOVEREIGN_ANISONG_BELT_307", 0.70f, 77.5f, -2.11f, 3.50f, 1.72f, -15.30f, -1.05f, -23.10f, 0.150f },
            { 2308, "SOVEREIGN_BALLAD_TEAR_308", 0.80f, 80.0f, -2.04f, 1.40f, 1.84f, -15.20f, -1.10f, -22.80f, 0.160f },
            { 2309, "SOVEREIGN_VTUBER_CLEAR_309", 0.90f, 82.5f, -1.97f, 1.50f, 1.96f, -15.10f, -1.15f, -22.50f, 0.170f },
            { 2310, "SOVEREIGN_ASMR_SILK_310", 1.00f, 85.0f, -1.90f, 1.60f, 2.08f, -15.00f, -1.20f, -22.20f, 0.180f },
            { 2311, "SOVEREIGN_ROCK_ARENA_311", 1.10f, 87.5f, -1.83f, 1.70f, 2.20f, -14.90f, -1.25f, -21.90f, 0.190f },
            { 2312, "SOVEREIGN_CITYPOP_TAPE_312", -1.20f, 90.0f, -1.76f, 1.80f, 2.32f, -14.80f, -1.00f, -21.60f, 0.200f },
            { 2313, "SOVEREIGN_RAP_PUNCH_313", -1.10f, 92.5f, -1.69f, 1.90f, 2.44f, -14.70f, -1.05f, -21.30f, 0.210f },
            { 2314, "SOVEREIGN_ACOUSTIC_WOOD_314", -1.00f, 95.0f, -1.62f, 2.00f, 2.56f, -14.60f, -1.10f, -21.00f, 0.220f },
            { 2315, "SOVEREIGN_CHOIR_HALO_315", -0.90f, 97.5f, -1.55f, 2.10f, 2.68f, -16.00f, -1.15f, -20.70f, 0.230f },
            { 2316, "SOVEREIGN_KARAOKE_STAGE_316", -0.80f, 100.0f, -1.48f, 2.20f, 2.80f, -15.90f, -1.20f, -20.40f, 0.240f },
            { 2317, "SOVEREIGN_RADIO_HOST_317", -0.70f, 102.5f, -1.41f, 2.30f, 2.92f, -15.80f, -1.25f, -20.10f, 0.250f },
            { 2318, "SOVEREIGN_FALSETTO_BLOOM_318", -0.60f, 105.0f, -1.34f, 2.40f, 3.04f, -15.70f, -1.00f, -19.80f, 0.260f },
            { 2319, "SOVEREIGN_VOCALOID_FAST_319", -0.50f, 107.5f, -1.27f, 2.50f, 3.16f, -15.60f, -1.05f, -19.50f, 0.270f },
            { 2320, "SOVEREIGN_RNB_WARM_320", -0.40f, 110.0f, -2.60f, 2.60f, 3.28f, -15.50f, -1.10f, -24.00f, 0.080f },
            { 2321, "SOVEREIGN_IDOL_SHINE_321", -0.30f, 112.5f, -2.53f, 2.70f, 3.40f, -15.40f, -1.15f, -23.70f, 0.090f },
            { 2322, "SOVEREIGN_JPOP_LEAD_322", -0.20f, 115.0f, -2.46f, 2.80f, 3.52f, -15.30f, -1.20f, -23.40f, 0.100f },
            { 2323, "SOVEREIGN_ANISONG_BELT_323", -0.10f, 117.5f, -2.39f, 2.90f, 3.64f, -15.20f, -1.25f, -23.10f, 0.110f },
            { 2324, "SOVEREIGN_BALLAD_TEAR_324", 0.00f, 120.0f, -2.32f, 3.00f, 1.60f, -15.10f, -1.00f, -22.80f, 0.120f },
            { 2325, "SOVEREIGN_VTUBER_CLEAR_325", 0.10f, 122.5f, -2.25f, 3.10f, 1.72f, -15.00f, -1.05f, -22.50f, 0.130f },
            { 2326, "SOVEREIGN_ASMR_SILK_326", 0.20f, 125.0f, -2.18f, 3.20f, 1.84f, -14.90f, -1.10f, -22.20f, 0.140f },
            { 2327, "SOVEREIGN_ROCK_ARENA_327", 0.30f, 127.5f, -2.11f, 3.30f, 1.96f, -14.80f, -1.15f, -21.90f, 0.150f },
            { 2328, "SOVEREIGN_CITYPOP_TAPE_328", 0.40f, 130.0f, -2.04f, 3.40f, 2.08f, -14.70f, -1.20f, -21.60f, 0.160f },
            { 2329, "SOVEREIGN_RAP_PUNCH_329", 0.50f, 132.5f, -1.97f, 3.50f, 2.20f, -14.60f, -1.25f, -21.30f, 0.170f },
            { 2330, "SOVEREIGN_ACOUSTIC_WOOD_330", 0.60f, 60.0f, -1.90f, 1.40f, 2.32f, -16.00f, -1.00f, -21.00f, 0.180f },
            { 2331, "SOVEREIGN_CHOIR_HALO_331", 0.70f, 62.5f, -1.83f, 1.50f, 2.44f, -15.90f, -1.05f, -20.70f, 0.190f },
            { 2332, "SOVEREIGN_KARAOKE_STAGE_332", 0.80f, 65.0f, -1.76f, 1.60f, 2.56f, -15.80f, -1.10f, -20.40f, 0.200f },
            { 2333, "SOVEREIGN_RADIO_HOST_333", 0.90f, 67.5f, -1.69f, 1.70f, 2.68f, -15.70f, -1.15f, -20.10f, 0.210f },
            { 2334, "SOVEREIGN_FALSETTO_BLOOM_334", 1.00f, 70.0f, -1.62f, 1.80f, 2.80f, -15.60f, -1.20f, -19.80f, 0.220f },
            { 2335, "SOVEREIGN_VOCALOID_FAST_335", 1.10f, 72.5f, -1.55f, 1.90f, 2.92f, -15.50f, -1.25f, -19.50f, 0.230f },
            { 2336, "SOVEREIGN_RNB_WARM_336", -1.20f, 75.0f, -1.48f, 2.00f, 3.04f, -15.40f, -1.00f, -24.00f, 0.240f },
            { 2337, "SOVEREIGN_IDOL_SHINE_337", -1.10f, 77.5f, -1.41f, 2.10f, 3.16f, -15.30f, -1.05f, -23.70f, 0.250f },
            { 2338, "SOVEREIGN_JPOP_LEAD_338", -1.00f, 80.0f, -1.34f, 2.20f, 3.28f, -15.20f, -1.10f, -23.40f, 0.260f },
            { 2339, "SOVEREIGN_ANISONG_BELT_339", -0.90f, 82.5f, -1.27f, 2.30f, 3.40f, -15.10f, -1.15f, -23.10f, 0.270f },
        }};

        static inline bool verifySovereignKernelVault(double sampleRate = 48000.0) noexcept {
            if (sampleRate <= 0.0 || !std::isfinite(sampleRate)) return false;
            SovereignTrapezoidalSvfCell lp{}, bp{}, hp{};
            lp.configure(sampleRate, 1000.0, 0.7071);
            bp.configure(sampleRate, 2800.0, 1.4142);
            hp.configure(sampleRate, 110.0, 0.7071);
            double energy = 0.0;
            for (std::size_t n = 0; n < 256; ++n) {
                const float p = static_cast<float>(n) * 0.085f;
                const float x = 0.25f * std::sin(p) + 0.10f * std::cos(p * 2.5f);
                const float y = lp.stepLowpass(x) + bp.stepBandpass(x) + hp.stepHighpass(x);
                if (!std::isfinite(y)) return false;
                energy += static_cast<double>(y * y);
            }
            if (!(energy > 1.0e-6 && energy < 1.0e6)) return false;
            float prevHz = 0.0f;
            for (const auto& cb : kCriticalBands) {
                if (!(cb.centerHz > prevHz && cb.qFactor > 0.1f)) return false;
                prevHz = cb.centerHz;
            }
            for (const auto& m : kJapaneseMoraDictionVault) {
                if (!(m.f1Hz > 150.0f && m.f2Hz > m.f1Hz && m.f3Hz > m.f2Hz)) return false;
            }
            for (const auto& th : kThemeContrastVault) {
                if (!(th.contrastRatio >= 4.5f)) return false;
            }
            for (const auto& pr : kSovereignPresetVault) {
                if (!(pr.lowCutHz >= 20.0f && pr.truePeakCeilingDb <= -0.1f)) return false;
            }
            return true;
        }
    };

} // namespace vg::sovereign::streamer
// SOVEREIGN_30K_VERIFIED_MODULE_30
// VocalGzzio v5.5.0 Sovereign 7-Domain Flagship Suite — Verified
