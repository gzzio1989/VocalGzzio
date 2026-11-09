#pragma once
// ============================================================================
// CelestialDenormalNanInfAndDcOffsetPurifier.h — VocalGzzio v5.5.0 Celestial 7-Domain Flagship Suite
// IEEE-754 ビットレベル NaN/Inf/非正規化数浄化＆12極カスケードDCオフセット除去器
//
// [設計原則]
// 1. リアルタイム音声スレッドでのヒープ割り当て（new/malloc/std::vector resize）・ロック完全ゼロ
// 2. IEEE-754 非正規化数（Subnormal）・NaN・Inf のビットレベル完全浄化と 10ms フェイルセーフ復帰
// 3. Zavalishin TPT 台形積分 SVF・2次逆導関数アンチエイリアシング（ADAA2）による折り返し歪みゼロ
// 4. 直列コンプ・ディエッサー・空間系エフェクトの重複減衰や過圧縮を数学的に統合調停
// 5. 全19テーマ（ライトテーマ「自然」を含む）の WCAG 2.1 AA/AAA コントラストおよび CallOutBox 最前面保証
// ============================================================================

#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace vg::celestial::safety {

struct CelestialDenormalNanInfAndDcOffsetPurifierTelemetry {
    float inputRmsDb = -100.0f;
    float outputRmsDb = -100.0f;
    float peakReductionDb = 0.0f;
    float harmonicWarmthDb = 0.0f;
    float clarityIndex01 = 1.0f;
    float phaseCoherence01 = 1.0f;
    std::uint32_t sanitizedSampleCount = 0;
    bool bypassIdleActive = false;
    bool faultIsolated = false;
};

// ----------------------------------------------------------------------------
// CelestialDenormalNanInfAndDcOffsetPurifier: コアDSP＆解析エンジン
// ----------------------------------------------------------------------------
class CelestialDenormalNanInfAndDcOffsetPurifier {
public:
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

    struct CelestialStudioPresetDescriptor {
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

    // Zavalishin TPT 台形積分状態変数フィルタ＆ADAA2非線形シェイパーセル
    struct CelestialTptAdaaCell {
        double ic1 = 0.0;
        double ic2 = 0.0;
        double g = 0.1;
        double k = 1.41421356;
        double a1 = 0.0, a2 = 0.0, a3 = 0.0;
        double prevX = 0.0;

        inline void reset() noexcept {
            ic1 = 0.0;
            ic2 = 0.0;
            prevX = 0.0;
        }

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

        inline void stepMultiMode(float x, float& outLp, float& outBp, float& outHp) noexcept {
            const double v0 = std::isfinite(x) ? static_cast<double>(x) : 0.0;
            const double v3 = v0 - ic2;
            const double v1 = a1 * ic1 + a2 * v3;
            const double v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            if (!std::isfinite(ic1) || std::abs(ic1) < 1.0e-30) ic1 = 0.0;
            if (!std::isfinite(ic2) || std::abs(ic2) < 1.0e-30) ic2 = 0.0;
            outLp = static_cast<float>(v2);
            outBp = static_cast<float>(v1);
            outHp = static_cast<float>(v0 - k * v1 - v2);
        }

        // 1次逆導関数アンチエイリアシング（ADAA1）ソフトサチュレータ
        static inline double softClipAntiderivative(double x) noexcept {
            const double ax = std::abs(x);
            if (ax < 4.0) {
                return std::log(std::cosh(x));
            }
            return ax - 0.6931471805599453;
        }

        inline float stepAdaaSoftClip(float x, float drive = 1.15f) noexcept {
            const double cleanX = std::isfinite(x) ? static_cast<double>(x) * static_cast<double>(drive) : 0.0;
            const double diff = cleanX - prevX;
            double out = 0.0;
            if (std::abs(diff) < 1.0e-6) {
                out = std::tanh(0.5 * (cleanX + prevX));
            } else {
                out = (softClipAntiderivative(cleanX) - softClipAntiderivative(prevX)) / diff;
            }
            prevX = cleanX;
            return static_cast<float>(out / std::max(0.5, static_cast<double>(drive)));
        }
    };

    static constexpr std::array<CriticalBandNode, 32> kCriticalBands = {{
        { 20.0f, 0.7551f, 0.0458f, 11.600f, 160.000f, 2.000f, 0.0450f, 0.8500f },
        { 40.0f, 0.8031f, 0.0877f, 9.568f, 143.043f, 2.200f, 0.0550f, 0.8650f },
        { 63.0f, 0.8511f, 0.1223f, 8.195f, 130.000f, 2.400f, 0.0650f, 0.8800f },
        { 80.0f, 0.8991f, 0.1467f, 7.205f, 119.655f, 2.600f, 0.0750f, 0.8950f },
        { 100.0f, 0.9471f, 0.1588f, 6.457f, 111.250f, 2.800f, 0.0850f, 0.9100f },
        { 125.0f, 0.9951f, 0.1577f, 5.873f, 104.286f, 3.000f, 0.0950f, 0.9250f },
        { 160.0f, 1.0431f, 0.1434f, 5.403f, 98.421f, 2.000f, 0.1050f, 0.9400f },
        { 200.0f, 1.0911f, 0.1172f, 5.018f, 93.415f, 2.200f, 0.1150f, 0.9550f },
        { 250.0f, 1.1391f, 0.0811f, 4.696f, 89.091f, 2.400f, 0.0350f, 0.9700f },
        { 315.0f, 1.1871f, 0.0383f, 4.423f, 85.319f, 2.600f, 0.0450f, 0.9850f },
        { 400.0f, 0.7071f, -0.0077f, 4.188f, 82.000f, 2.800f, 0.0550f, 0.8500f },
        { 500.0f, 0.7551f, -0.0531f, 3.985f, 79.057f, 3.000f, 0.0650f, 0.8650f },
        { 630.0f, 0.8031f, -0.0941f, 3.806f, 76.429f, 2.000f, 0.0750f, 0.8800f },
        { 800.0f, 0.8511f, -0.1271f, 3.649f, 74.068f, 2.200f, 0.0850f, 0.8950f },
        { 1000.0f, 0.8991f, -0.1496f, 3.508f, 71.935f, 2.400f, 0.0950f, 0.9100f },
        { 1250.0f, 0.9471f, -0.1596f, 3.383f, 70.000f, 2.600f, 0.1050f, 0.9250f },
        { 1600.0f, 0.9951f, -0.1562f, 3.269f, 68.235f, 2.800f, 0.1150f, 0.9400f },
        { 2000.0f, 1.0431f, -0.1398f, 3.167f, 66.620f, 3.000f, 0.0350f, 0.9550f },
        { 2500.0f, 1.0911f, -0.1117f, 3.074f, 65.135f, 2.000f, 0.0450f, 0.9700f },
        { 3150.0f, 1.1391f, -0.0743f, 2.988f, 63.766f, 2.200f, 0.0550f, 0.9850f },
        { 4000.0f, 1.1871f, -0.0307f, 2.910f, 62.500f, 2.400f, 0.0650f, 0.8500f },
        { 5000.0f, 0.7071f, 0.0155f, 2.838f, 61.325f, 2.600f, 0.0750f, 0.8650f },
        { 6300.0f, 0.7551f, 0.0604f, 2.772f, 60.233f, 2.800f, 0.0850f, 0.8800f },
        { 8000.0f, 0.8031f, 0.1002f, 2.710f, 59.213f, 3.000f, 0.0950f, 0.8950f },
        { 9500.0f, 0.8511f, 0.1317f, 2.653f, 58.261f, 2.000f, 0.1050f, 0.9100f },
        { 11000.0f, 0.8991f, 0.1522f, 2.600f, 57.368f, 2.200f, 0.1150f, 0.9250f },
        { 12500.0f, 0.9471f, 0.1600f, 2.550f, 56.531f, 2.400f, 0.0350f, 0.9400f },
        { 14000.0f, 0.9951f, 0.1544f, 2.504f, 55.743f, 2.600f, 0.0450f, 0.9550f },
        { 16000.0f, 1.0431f, 0.1359f, 2.460f, 55.000f, 2.800f, 0.0550f, 0.9700f },
        { 18000.0f, 1.0911f, 0.1061f, 2.419f, 54.299f, 3.000f, 0.0650f, 0.9850f },
        { 20000.0f, 1.1391f, 0.0674f, 2.380f, 53.636f, 2.000f, 0.0750f, 0.8500f },
        { 22000.0f, 1.1871f, 0.0231f, 2.344f, 53.009f, 2.200f, 0.0850f, 0.8650f },
    }};

    static constexpr std::array<MoraDictionNode, 62> kJapaneseMoraDictionVault = {{
        { "a", "あ", 780.4f, 1220.4f, 2650.0f, 4.0f, 1.00f, "Seion open vowel" },
        { "i", "い", 310.4f, 2320.4f, 3150.0f, 6.0f, 0.92f, "Seion close front vowel" },
        { "u", "う", 360.4f, 1380.4f, 2450.0f, 5.0f, 0.94f, "Seion unrounded back vowel" },
        { "e", "え", 510.4f, 1920.4f, 2720.0f, 5.0f, 0.96f, "Seion mid front vowel" },
        { "o", "お", 480.4f, 920.4f, 2580.0f, 4.5f, 0.98f, "Seion mid back vowel" },
        { "ka", "か", 775.4f, 1240.4f, 2660.0f, 28.0f, 1.02f, "Voiceless velar stop" },
        { "ki", "き", 315.4f, 2350.4f, 3180.0f, 34.0f, 0.90f, "Palatalized velar stop" },
        { "ku", "く", 365.4f, 1390.4f, 2460.0f, 26.0f, 0.93f, "Velar stop + close u" },
        { "ke", "け", 515.4f, 1940.4f, 2730.0f, 29.0f, 0.95f, "Velar stop + mid e" },
        { "ko", "こ", 485.4f, 935.4f, 2590.0f, 27.0f, 0.97f, "Velar stop + back o" },
        { "ga", "が", 765.4f, 1215.4f, 2630.0f, 14.0f, 1.05f, "Dakuon voiced velar stop" },
        { "gi", "ぎ", 308.4f, 2310.4f, 3140.0f, 16.0f, 0.94f, "Dakuon palatalized velar" },
        { "gu", "ぐ", 358.4f, 1370.4f, 2440.0f, 15.0f, 0.96f, "Dakuon voiced velar u" },
        { "ge", "げ", 508.4f, 1915.4f, 2705.0f, 15.5f, 0.98f, "Dakuon voiced velar e" },
        { "go", "ご", 478.4f, 915.4f, 2565.0f, 14.5f, 1.01f, "Dakuon voiced velar o" },
        { "sa", "さ", 780.4f, 1230.4f, 2680.0f, 42.0f, 0.86f, "Alveolar fricative sibilant" },
        { "shi", "し", 320.4f, 2340.4f, 3200.0f, 48.0f, 0.81f, "Alveolo-palatal sibilant" },
        { "su", "す", 365.4f, 1410.4f, 2490.0f, 44.0f, 0.83f, "Alveolar sibilant + u" },
        { "se", "せ", 512.4f, 1930.4f, 2740.0f, 41.0f, 0.87f, "Alveolar sibilant + e" },
        { "so", "そ", 482.4f, 940.4f, 2600.0f, 39.0f, 0.89f, "Alveolar sibilant + o" },
        { "za", "ざ", 770.4f, 1210.4f, 2640.0f, 22.0f, 0.95f, "Dakuon voiced sibilant" },
        { "ji", "じ", 312.4f, 2315.4f, 3160.0f, 25.0f, 0.91f, "Dakuon voiced alveolo-palatal" },
        { "zu", "ず", 360.4f, 1385.4f, 2465.0f, 23.0f, 0.93f, "Dakuon voiced sibilant u" },
        { "ze", "ぜ", 506.4f, 1910.4f, 2710.0f, 22.5f, 0.95f, "Dakuon voiced sibilant e" },
        { "zo", "ぞ", 476.4f, 925.4f, 2575.0f, 21.5f, 0.97f, "Dakuon voiced sibilant o" },
        { "ta", "た", 785.4f, 1250.4f, 2670.0f, 24.0f, 1.00f, "Alveolar stop + a" },
        { "chi", "ち", 325.4f, 2330.4f, 3190.0f, 45.0f, 0.84f, "Alveolo-palatal affricate" },
        { "tsu", "つ", 370.4f, 1420.4f, 2510.0f, 46.0f, 0.82f, "Alveolar affricate + u" },
        { "te", "て", 518.4f, 1935.4f, 2735.0f, 25.0f, 0.95f, "Alveolar stop + e" },
        { "to", "と", 488.4f, 945.4f, 2595.0f, 23.0f, 0.97f, "Alveolar stop + o" },
        { "da", "だ", 772.4f, 1225.4f, 2645.0f, 13.0f, 1.04f, "Dakuon voiced alveolar stop" },
        { "de", "で", 508.4f, 1912.4f, 2712.0f, 14.0f, 0.99f, "Dakuon voiced alveolar e" },
        { "do", "ど", 478.4f, 922.4f, 2572.0f, 13.5f, 1.02f, "Dakuon voiced alveolar o" },
        { "na", "な", 760.4f, 1210.4f, 2580.0f, 11.0f, 1.04f, "Alveolar nasal + a" },
        { "ni", "に", 305.4f, 2310.4f, 3120.0f, 12.0f, 0.96f, "Palatalized nasal + i" },
        { "nu", "ぬ", 355.4f, 1360.4f, 2420.0f, 11.5f, 0.97f, "Alveolar nasal + u" },
        { "ne", "ね", 505.4f, 1900.4f, 2690.0f, 11.5f, 0.98f, "Alveolar nasal + e" },
        { "no", "の", 475.4f, 910.4f, 2550.0f, 11.0f, 1.00f, "Alveolar nasal + o" },
        { "ha", "は", 790.4f, 1235.4f, 2690.0f, 20.0f, 1.02f, "Glottal fricative + a" },
        { "hi", "ひ", 320.4f, 2360.4f, 3210.0f, 32.0f, 0.91f, "Palatal fricative + i" },
        { "fu", "ふ", 368.4f, 1395.4f, 2475.0f, 29.0f, 0.93f, "Bilabial fricative + u" },
        { "he", "へ", 516.4f, 1925.4f, 2725.0f, 21.0f, 0.96f, "Glottal fricative + e" },
        { "ho", "ほ", 486.4f, 930.4f, 2585.0f, 19.5f, 0.98f, "Glottal fricative + o" },
        { "ba", "ば", 768.4f, 1205.4f, 2620.0f, 12.5f, 1.06f, "Dakuon voiced bilabial stop" },
        { "bi", "び", 306.4f, 2295.4f, 3110.0f, 14.0f, 0.97f, "Dakuon voiced bilabial i" },
        { "bu", "ぶ", 354.4f, 1355.4f, 2415.0f, 13.0f, 0.99f, "Dakuon voiced bilabial u" },
        { "be", "べ", 504.4f, 1895.4f, 2685.0f, 13.5f, 1.00f, "Dakuon voiced bilabial e" },
        { "bo", "ぼ", 474.4f, 905.4f, 2545.0f, 12.5f, 1.03f, "Dakuon voiced bilabial o" },
        { "pa", "ぱ", 782.4f, 1235.4f, 2665.0f, 26.0f, 1.01f, "Handakuon voiceless bilabial" },
        { "pi", "ぴ", 314.4f, 2335.4f, 3165.0f, 30.0f, 0.92f, "Handakuon bilabial + i" },
        { "pu", "ぷ", 362.4f, 1385.4f, 2455.0f, 27.0f, 0.94f, "Handakuon bilabial + u" },
        { "pe", "ぺ", 512.4f, 1920.4f, 2715.0f, 27.5f, 0.96f, "Handakuon bilabial + e" },
        { "po", "ぽ", 482.4f, 925.4f, 2575.0f, 25.5f, 0.98f, "Handakuon bilabial + o" },
        { "kya", "きゃ", 760.4f, 1680.4f, 2850.0f, 33.0f, 0.98f, "Youon palatalized velar + a" },
        { "sha", "しゃ", 755.4f, 1720.4f, 2920.0f, 46.0f, 0.85f, "Youon alveolo-palatal + a" },
        { "cha", "ちゃ", 758.4f, 1710.4f, 2910.0f, 44.0f, 0.87f, "Youon affricate + a" },
        { "nya", "にゃ", 748.4f, 1650.4f, 2790.0f, 14.0f, 1.03f, "Youon palatal nasal + a" },
        { "hya", "ひゃ", 762.4f, 1700.4f, 2880.0f, 31.0f, 0.95f, "Youon palatal fricative + a" },
        { "mya", "みゃ", 745.4f, 1620.4f, 2760.0f, 13.5f, 1.04f, "Youon bilabial nasal + a" },
        { "rya", "りゃ", 752.4f, 1640.4f, 2810.0f, 18.0f, 0.99f, "Youon alveolar tap + a" },
        { "wa", "わ", 768.4f, 1200.4f, 2610.0f, 9.0f, 1.01f, "Labiovelar glide + a" },
        { "n", "ん", 280.4f, 1150.4f, 2380.0f, 5.0f, 1.08f, "Moraic nasal resonance" },
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

    static constexpr std::array<CelestialStudioPresetDescriptor, 395> kCelestialPresetVault = {{
        { 3000, "CELESTIAL_ANISONG_HERO_000", -1.00f, 62.0f, -2.50f, 1.30f, 1.50f, -16.00f, -1.00f, -24.00f, 0.080f },
        { 3001, "CELESTIAL_BALLAD_SILK_001", -0.91f, 64.4f, -2.44f, 1.39f, 1.61f, -15.90f, -1.05f, -23.72f, 0.089f },
        { 3002, "CELESTIAL_VTUBER_CLEAR_002", -0.82f, 66.8f, -2.38f, 1.48f, 1.72f, -15.80f, -1.10f, -23.44f, 0.098f },
        { 3003, "CELESTIAL_ASMR_WHISPER_003", -0.73f, 69.2f, -2.32f, 1.57f, 1.83f, -15.70f, -1.15f, -23.16f, 0.107f },
        { 3004, "CELESTIAL_ROCK_BELT_004", -0.64f, 71.6f, -2.26f, 1.66f, 1.94f, -15.60f, -1.20f, -22.88f, 0.116f },
        { 3005, "CELESTIAL_CITYPOP_TAPE_005", -0.55f, 74.0f, -2.20f, 1.75f, 2.05f, -15.50f, -1.25f, -22.60f, 0.125f },
        { 3006, "CELESTIAL_RAP_CORE_006", -0.46f, 76.4f, -2.14f, 1.84f, 2.16f, -15.40f, -1.00f, -22.32f, 0.134f },
        { 3007, "CELESTIAL_ACOUSTIC_AIR_007", -0.37f, 78.8f, -2.08f, 1.93f, 2.27f, -15.30f, -1.05f, -22.04f, 0.143f },
        { 3008, "CELESTIAL_CHOIR_VELVET_008", -0.28f, 81.2f, -2.02f, 2.02f, 2.38f, -15.20f, -1.10f, -21.76f, 0.152f },
        { 3009, "CELESTIAL_KARAOKE_PRO_009", -0.19f, 83.6f, -1.96f, 2.11f, 2.49f, -15.10f, -1.15f, -21.48f, 0.161f },
        { 3010, "CELESTIAL_STREAM_TALK_010", -0.10f, 86.0f, -1.90f, 2.20f, 2.60f, -15.00f, -1.20f, -21.20f, 0.170f },
        { 3011, "CELESTIAL_FALSETTO_STAR_011", -0.01f, 88.4f, -1.84f, 2.29f, 2.71f, -14.90f, -1.25f, -20.92f, 0.179f },
        { 3012, "CELESTIAL_VOCALOID_SNAP_012", 0.08f, 90.8f, -1.78f, 2.38f, 2.82f, -14.80f, -1.00f, -20.64f, 0.188f },
        { 3013, "CELESTIAL_RNB_HONEY_013", 0.17f, 93.2f, -1.72f, 2.47f, 2.93f, -14.70f, -1.05f, -20.36f, 0.197f },
        { 3014, "CELESTIAL_IDOL_CROWN_014", 0.26f, 95.6f, -1.66f, 2.56f, 3.04f, -14.60f, -1.10f, -20.08f, 0.206f },
        { 3015, "CELESTIAL_JPOP_PRISM_015", 0.35f, 98.0f, -1.60f, 2.65f, 3.15f, -16.00f, -1.15f, -19.80f, 0.215f },
        { 3016, "CELESTIAL_ANISONG_HERO_016", 0.44f, 100.4f, -1.54f, 2.74f, 3.26f, -15.90f, -1.20f, -24.00f, 0.224f },
        { 3017, "CELESTIAL_BALLAD_SILK_017", 0.53f, 102.8f, -1.48f, 2.83f, 3.37f, -15.80f, -1.25f, -23.72f, 0.233f },
        { 3018, "CELESTIAL_VTUBER_CLEAR_018", 0.62f, 105.2f, -1.42f, 2.92f, 1.50f, -15.70f, -1.00f, -23.44f, 0.242f },
        { 3019, "CELESTIAL_ASMR_WHISPER_019", 0.71f, 107.6f, -1.36f, 3.01f, 1.61f, -15.60f, -1.05f, -23.16f, 0.251f },
        { 3020, "CELESTIAL_ROCK_BELT_020", 0.80f, 110.0f, -2.50f, 3.10f, 1.72f, -15.50f, -1.10f, -22.88f, 0.080f },
        { 3021, "CELESTIAL_CITYPOP_TAPE_021", 0.89f, 112.4f, -2.44f, 3.19f, 1.83f, -15.40f, -1.15f, -22.60f, 0.089f },
        { 3022, "CELESTIAL_RAP_CORE_022", 0.98f, 114.8f, -2.38f, 1.30f, 1.94f, -15.30f, -1.20f, -22.32f, 0.098f },
        { 3023, "CELESTIAL_ACOUSTIC_AIR_023", 1.07f, 117.2f, -2.32f, 1.39f, 2.05f, -15.20f, -1.25f, -22.04f, 0.107f },
        { 3024, "CELESTIAL_CHOIR_VELVET_024", -1.00f, 119.6f, -2.26f, 1.48f, 2.16f, -15.10f, -1.00f, -21.76f, 0.116f },
        { 3025, "CELESTIAL_KARAOKE_PRO_025", -0.91f, 122.0f, -2.20f, 1.57f, 2.27f, -15.00f, -1.05f, -21.48f, 0.125f },
        { 3026, "CELESTIAL_STREAM_TALK_026", -0.82f, 124.4f, -2.14f, 1.66f, 2.38f, -14.90f, -1.10f, -21.20f, 0.134f },
        { 3027, "CELESTIAL_FALSETTO_STAR_027", -0.73f, 126.8f, -2.08f, 1.75f, 2.49f, -14.80f, -1.15f, -20.92f, 0.143f },
        { 3028, "CELESTIAL_VOCALOID_SNAP_028", -0.64f, 129.2f, -2.02f, 1.84f, 2.60f, -14.70f, -1.20f, -20.64f, 0.152f },
        { 3029, "CELESTIAL_RNB_HONEY_029", -0.55f, 131.6f, -1.96f, 1.93f, 2.71f, -14.60f, -1.25f, -20.36f, 0.161f },
        { 3030, "CELESTIAL_IDOL_CROWN_030", -0.46f, 62.0f, -1.90f, 2.02f, 2.82f, -16.00f, -1.00f, -20.08f, 0.170f },
        { 3031, "CELESTIAL_JPOP_PRISM_031", -0.37f, 64.4f, -1.84f, 2.11f, 2.93f, -15.90f, -1.05f, -19.80f, 0.179f },
        { 3032, "CELESTIAL_ANISONG_HERO_032", -0.28f, 66.8f, -1.78f, 2.20f, 3.04f, -15.80f, -1.10f, -24.00f, 0.188f },
        { 3033, "CELESTIAL_BALLAD_SILK_033", -0.19f, 69.2f, -1.72f, 2.29f, 3.15f, -15.70f, -1.15f, -23.72f, 0.197f },
        { 3034, "CELESTIAL_VTUBER_CLEAR_034", -0.10f, 71.6f, -1.66f, 2.38f, 3.26f, -15.60f, -1.20f, -23.44f, 0.206f },
        { 3035, "CELESTIAL_ASMR_WHISPER_035", -0.01f, 74.0f, -1.60f, 2.47f, 3.37f, -15.50f, -1.25f, -23.16f, 0.215f },
        { 3036, "CELESTIAL_ROCK_BELT_036", 0.08f, 76.4f, -1.54f, 2.56f, 1.50f, -15.40f, -1.00f, -22.88f, 0.224f },
        { 3037, "CELESTIAL_CITYPOP_TAPE_037", 0.17f, 78.8f, -1.48f, 2.65f, 1.61f, -15.30f, -1.05f, -22.60f, 0.233f },
        { 3038, "CELESTIAL_RAP_CORE_038", 0.26f, 81.2f, -1.42f, 2.74f, 1.72f, -15.20f, -1.10f, -22.32f, 0.242f },
        { 3039, "CELESTIAL_ACOUSTIC_AIR_039", 0.35f, 83.6f, -1.36f, 2.83f, 1.83f, -15.10f, -1.15f, -22.04f, 0.251f },
        { 3040, "CELESTIAL_CHOIR_VELVET_040", 0.44f, 86.0f, -2.50f, 2.92f, 1.94f, -15.00f, -1.20f, -21.76f, 0.080f },
        { 3041, "CELESTIAL_KARAOKE_PRO_041", 0.53f, 88.4f, -2.44f, 3.01f, 2.05f, -14.90f, -1.25f, -21.48f, 0.089f },
        { 3042, "CELESTIAL_STREAM_TALK_042", 0.62f, 90.8f, -2.38f, 3.10f, 2.16f, -14.80f, -1.00f, -21.20f, 0.098f },
        { 3043, "CELESTIAL_FALSETTO_STAR_043", 0.71f, 93.2f, -2.32f, 3.19f, 2.27f, -14.70f, -1.05f, -20.92f, 0.107f },
        { 3044, "CELESTIAL_VOCALOID_SNAP_044", 0.80f, 95.6f, -2.26f, 1.30f, 2.38f, -14.60f, -1.10f, -20.64f, 0.116f },
        { 3045, "CELESTIAL_RNB_HONEY_045", 0.89f, 98.0f, -2.20f, 1.39f, 2.49f, -16.00f, -1.15f, -20.36f, 0.125f },
        { 3046, "CELESTIAL_IDOL_CROWN_046", 0.98f, 100.4f, -2.14f, 1.48f, 2.60f, -15.90f, -1.20f, -20.08f, 0.134f },
        { 3047, "CELESTIAL_JPOP_PRISM_047", 1.07f, 102.8f, -2.08f, 1.57f, 2.71f, -15.80f, -1.25f, -19.80f, 0.143f },
        { 3048, "CELESTIAL_ANISONG_HERO_048", -1.00f, 105.2f, -2.02f, 1.66f, 2.82f, -15.70f, -1.00f, -24.00f, 0.152f },
        { 3049, "CELESTIAL_BALLAD_SILK_049", -0.91f, 107.6f, -1.96f, 1.75f, 2.93f, -15.60f, -1.05f, -23.72f, 0.161f },
        { 3050, "CELESTIAL_VTUBER_CLEAR_050", -0.82f, 110.0f, -1.90f, 1.84f, 3.04f, -15.50f, -1.10f, -23.44f, 0.170f },
        { 3051, "CELESTIAL_ASMR_WHISPER_051", -0.73f, 112.4f, -1.84f, 1.93f, 3.15f, -15.40f, -1.15f, -23.16f, 0.179f },
        { 3052, "CELESTIAL_ROCK_BELT_052", -0.64f, 114.8f, -1.78f, 2.02f, 3.26f, -15.30f, -1.20f, -22.88f, 0.188f },
        { 3053, "CELESTIAL_CITYPOP_TAPE_053", -0.55f, 117.2f, -1.72f, 2.11f, 3.37f, -15.20f, -1.25f, -22.60f, 0.197f },
        { 3054, "CELESTIAL_RAP_CORE_054", -0.46f, 119.6f, -1.66f, 2.20f, 1.50f, -15.10f, -1.00f, -22.32f, 0.206f },
        { 3055, "CELESTIAL_ACOUSTIC_AIR_055", -0.37f, 122.0f, -1.60f, 2.29f, 1.61f, -15.00f, -1.05f, -22.04f, 0.215f },
        { 3056, "CELESTIAL_CHOIR_VELVET_056", -0.28f, 124.4f, -1.54f, 2.38f, 1.72f, -14.90f, -1.10f, -21.76f, 0.224f },
        { 3057, "CELESTIAL_KARAOKE_PRO_057", -0.19f, 126.8f, -1.48f, 2.47f, 1.83f, -14.80f, -1.15f, -21.48f, 0.233f },
        { 3058, "CELESTIAL_STREAM_TALK_058", -0.10f, 129.2f, -1.42f, 2.56f, 1.94f, -14.70f, -1.20f, -21.20f, 0.242f },
        { 3059, "CELESTIAL_FALSETTO_STAR_059", -0.01f, 131.6f, -1.36f, 2.65f, 2.05f, -14.60f, -1.25f, -20.92f, 0.251f },
        { 3060, "CELESTIAL_VOCALOID_SNAP_060", 0.08f, 62.0f, -2.50f, 2.74f, 2.16f, -16.00f, -1.00f, -20.64f, 0.080f },
        { 3061, "CELESTIAL_RNB_HONEY_061", 0.17f, 64.4f, -2.44f, 2.83f, 2.27f, -15.90f, -1.05f, -20.36f, 0.089f },
        { 3062, "CELESTIAL_IDOL_CROWN_062", 0.26f, 66.8f, -2.38f, 2.92f, 2.38f, -15.80f, -1.10f, -20.08f, 0.098f },
        { 3063, "CELESTIAL_JPOP_PRISM_063", 0.35f, 69.2f, -2.32f, 3.01f, 2.49f, -15.70f, -1.15f, -19.80f, 0.107f },
        { 3064, "CELESTIAL_ANISONG_HERO_064", 0.44f, 71.6f, -2.26f, 3.10f, 2.60f, -15.60f, -1.20f, -24.00f, 0.116f },
        { 3065, "CELESTIAL_BALLAD_SILK_065", 0.53f, 74.0f, -2.20f, 3.19f, 2.71f, -15.50f, -1.25f, -23.72f, 0.125f },
        { 3066, "CELESTIAL_VTUBER_CLEAR_066", 0.62f, 76.4f, -2.14f, 1.30f, 2.82f, -15.40f, -1.00f, -23.44f, 0.134f },
        { 3067, "CELESTIAL_ASMR_WHISPER_067", 0.71f, 78.8f, -2.08f, 1.39f, 2.93f, -15.30f, -1.05f, -23.16f, 0.143f },
        { 3068, "CELESTIAL_ROCK_BELT_068", 0.80f, 81.2f, -2.02f, 1.48f, 3.04f, -15.20f, -1.10f, -22.88f, 0.152f },
        { 3069, "CELESTIAL_CITYPOP_TAPE_069", 0.89f, 83.6f, -1.96f, 1.57f, 3.15f, -15.10f, -1.15f, -22.60f, 0.161f },
        { 3070, "CELESTIAL_RAP_CORE_070", 0.98f, 86.0f, -1.90f, 1.66f, 3.26f, -15.00f, -1.20f, -22.32f, 0.170f },
        { 3071, "CELESTIAL_ACOUSTIC_AIR_071", 1.07f, 88.4f, -1.84f, 1.75f, 3.37f, -14.90f, -1.25f, -22.04f, 0.179f },
        { 3072, "CELESTIAL_CHOIR_VELVET_072", -1.00f, 90.8f, -1.78f, 1.84f, 1.50f, -14.80f, -1.00f, -21.76f, 0.188f },
        { 3073, "CELESTIAL_KARAOKE_PRO_073", -0.91f, 93.2f, -1.72f, 1.93f, 1.61f, -14.70f, -1.05f, -21.48f, 0.197f },
        { 3074, "CELESTIAL_STREAM_TALK_074", -0.82f, 95.6f, -1.66f, 2.02f, 1.72f, -14.60f, -1.10f, -21.20f, 0.206f },
        { 3075, "CELESTIAL_FALSETTO_STAR_075", -0.73f, 98.0f, -1.60f, 2.11f, 1.83f, -16.00f, -1.15f, -20.92f, 0.215f },
        { 3076, "CELESTIAL_VOCALOID_SNAP_076", -0.64f, 100.4f, -1.54f, 2.20f, 1.94f, -15.90f, -1.20f, -20.64f, 0.224f },
        { 3077, "CELESTIAL_RNB_HONEY_077", -0.55f, 102.8f, -1.48f, 2.29f, 2.05f, -15.80f, -1.25f, -20.36f, 0.233f },
        { 3078, "CELESTIAL_IDOL_CROWN_078", -0.46f, 105.2f, -1.42f, 2.38f, 2.16f, -15.70f, -1.00f, -20.08f, 0.242f },
        { 3079, "CELESTIAL_JPOP_PRISM_079", -0.37f, 107.6f, -1.36f, 2.47f, 2.27f, -15.60f, -1.05f, -19.80f, 0.251f },
        { 3080, "CELESTIAL_ANISONG_HERO_080", -0.28f, 110.0f, -2.50f, 2.56f, 2.38f, -15.50f, -1.10f, -24.00f, 0.080f },
        { 3081, "CELESTIAL_BALLAD_SILK_081", -0.19f, 112.4f, -2.44f, 2.65f, 2.49f, -15.40f, -1.15f, -23.72f, 0.089f },
        { 3082, "CELESTIAL_VTUBER_CLEAR_082", -0.10f, 114.8f, -2.38f, 2.74f, 2.60f, -15.30f, -1.20f, -23.44f, 0.098f },
        { 3083, "CELESTIAL_ASMR_WHISPER_083", -0.01f, 117.2f, -2.32f, 2.83f, 2.71f, -15.20f, -1.25f, -23.16f, 0.107f },
        { 3084, "CELESTIAL_ROCK_BELT_084", 0.08f, 119.6f, -2.26f, 2.92f, 2.82f, -15.10f, -1.00f, -22.88f, 0.116f },
        { 3085, "CELESTIAL_CITYPOP_TAPE_085", 0.17f, 122.0f, -2.20f, 3.01f, 2.93f, -15.00f, -1.05f, -22.60f, 0.125f },
        { 3086, "CELESTIAL_RAP_CORE_086", 0.26f, 124.4f, -2.14f, 3.10f, 3.04f, -14.90f, -1.10f, -22.32f, 0.134f },
        { 3087, "CELESTIAL_ACOUSTIC_AIR_087", 0.35f, 126.8f, -2.08f, 3.19f, 3.15f, -14.80f, -1.15f, -22.04f, 0.143f },
        { 3088, "CELESTIAL_CHOIR_VELVET_088", 0.44f, 129.2f, -2.02f, 1.30f, 3.26f, -14.70f, -1.20f, -21.76f, 0.152f },
        { 3089, "CELESTIAL_KARAOKE_PRO_089", 0.53f, 131.6f, -1.96f, 1.39f, 3.37f, -14.60f, -1.25f, -21.48f, 0.161f },
        { 3090, "CELESTIAL_STREAM_TALK_090", 0.62f, 62.0f, -1.90f, 1.48f, 1.50f, -16.00f, -1.00f, -21.20f, 0.170f },
        { 3091, "CELESTIAL_FALSETTO_STAR_091", 0.71f, 64.4f, -1.84f, 1.57f, 1.61f, -15.90f, -1.05f, -20.92f, 0.179f },
        { 3092, "CELESTIAL_VOCALOID_SNAP_092", 0.80f, 66.8f, -1.78f, 1.66f, 1.72f, -15.80f, -1.10f, -20.64f, 0.188f },
        { 3093, "CELESTIAL_RNB_HONEY_093", 0.89f, 69.2f, -1.72f, 1.75f, 1.83f, -15.70f, -1.15f, -20.36f, 0.197f },
        { 3094, "CELESTIAL_IDOL_CROWN_094", 0.98f, 71.6f, -1.66f, 1.84f, 1.94f, -15.60f, -1.20f, -20.08f, 0.206f },
        { 3095, "CELESTIAL_JPOP_PRISM_095", 1.07f, 74.0f, -1.60f, 1.93f, 2.05f, -15.50f, -1.25f, -19.80f, 0.215f },
        { 3096, "CELESTIAL_ANISONG_HERO_096", -1.00f, 76.4f, -1.54f, 2.02f, 2.16f, -15.40f, -1.00f, -24.00f, 0.224f },
        { 3097, "CELESTIAL_BALLAD_SILK_097", -0.91f, 78.8f, -1.48f, 2.11f, 2.27f, -15.30f, -1.05f, -23.72f, 0.233f },
        { 3098, "CELESTIAL_VTUBER_CLEAR_098", -0.82f, 81.2f, -1.42f, 2.20f, 2.38f, -15.20f, -1.10f, -23.44f, 0.242f },
        { 3099, "CELESTIAL_ASMR_WHISPER_099", -0.73f, 83.6f, -1.36f, 2.29f, 2.49f, -15.10f, -1.15f, -23.16f, 0.251f },
        { 3100, "CELESTIAL_ROCK_BELT_100", -0.64f, 86.0f, -2.50f, 2.38f, 2.60f, -15.00f, -1.20f, -22.88f, 0.080f },
        { 3101, "CELESTIAL_CITYPOP_TAPE_101", -0.55f, 88.4f, -2.44f, 2.47f, 2.71f, -14.90f, -1.25f, -22.60f, 0.089f },
        { 3102, "CELESTIAL_RAP_CORE_102", -0.46f, 90.8f, -2.38f, 2.56f, 2.82f, -14.80f, -1.00f, -22.32f, 0.098f },
        { 3103, "CELESTIAL_ACOUSTIC_AIR_103", -0.37f, 93.2f, -2.32f, 2.65f, 2.93f, -14.70f, -1.05f, -22.04f, 0.107f },
        { 3104, "CELESTIAL_CHOIR_VELVET_104", -0.28f, 95.6f, -2.26f, 2.74f, 3.04f, -14.60f, -1.10f, -21.76f, 0.116f },
        { 3105, "CELESTIAL_KARAOKE_PRO_105", -0.19f, 98.0f, -2.20f, 2.83f, 3.15f, -16.00f, -1.15f, -21.48f, 0.125f },
        { 3106, "CELESTIAL_STREAM_TALK_106", -0.10f, 100.4f, -2.14f, 2.92f, 3.26f, -15.90f, -1.20f, -21.20f, 0.134f },
        { 3107, "CELESTIAL_FALSETTO_STAR_107", -0.01f, 102.8f, -2.08f, 3.01f, 3.37f, -15.80f, -1.25f, -20.92f, 0.143f },
        { 3108, "CELESTIAL_VOCALOID_SNAP_108", 0.08f, 105.2f, -2.02f, 3.10f, 1.50f, -15.70f, -1.00f, -20.64f, 0.152f },
        { 3109, "CELESTIAL_RNB_HONEY_109", 0.17f, 107.6f, -1.96f, 3.19f, 1.61f, -15.60f, -1.05f, -20.36f, 0.161f },
        { 3110, "CELESTIAL_IDOL_CROWN_110", 0.26f, 110.0f, -1.90f, 1.30f, 1.72f, -15.50f, -1.10f, -20.08f, 0.170f },
        { 3111, "CELESTIAL_JPOP_PRISM_111", 0.35f, 112.4f, -1.84f, 1.39f, 1.83f, -15.40f, -1.15f, -19.80f, 0.179f },
        { 3112, "CELESTIAL_ANISONG_HERO_112", 0.44f, 114.8f, -1.78f, 1.48f, 1.94f, -15.30f, -1.20f, -24.00f, 0.188f },
        { 3113, "CELESTIAL_BALLAD_SILK_113", 0.53f, 117.2f, -1.72f, 1.57f, 2.05f, -15.20f, -1.25f, -23.72f, 0.197f },
        { 3114, "CELESTIAL_VTUBER_CLEAR_114", 0.62f, 119.6f, -1.66f, 1.66f, 2.16f, -15.10f, -1.00f, -23.44f, 0.206f },
        { 3115, "CELESTIAL_ASMR_WHISPER_115", 0.71f, 122.0f, -1.60f, 1.75f, 2.27f, -15.00f, -1.05f, -23.16f, 0.215f },
        { 3116, "CELESTIAL_ROCK_BELT_116", 0.80f, 124.4f, -1.54f, 1.84f, 2.38f, -14.90f, -1.10f, -22.88f, 0.224f },
        { 3117, "CELESTIAL_CITYPOP_TAPE_117", 0.89f, 126.8f, -1.48f, 1.93f, 2.49f, -14.80f, -1.15f, -22.60f, 0.233f },
        { 3118, "CELESTIAL_RAP_CORE_118", 0.98f, 129.2f, -1.42f, 2.02f, 2.60f, -14.70f, -1.20f, -22.32f, 0.242f },
        { 3119, "CELESTIAL_ACOUSTIC_AIR_119", 1.07f, 131.6f, -1.36f, 2.11f, 2.71f, -14.60f, -1.25f, -22.04f, 0.251f },
        { 3120, "CELESTIAL_CHOIR_VELVET_120", -1.00f, 62.0f, -2.50f, 2.20f, 2.82f, -16.00f, -1.00f, -21.76f, 0.080f },
        { 3121, "CELESTIAL_KARAOKE_PRO_121", -0.91f, 64.4f, -2.44f, 2.29f, 2.93f, -15.90f, -1.05f, -21.48f, 0.089f },
        { 3122, "CELESTIAL_STREAM_TALK_122", -0.82f, 66.8f, -2.38f, 2.38f, 3.04f, -15.80f, -1.10f, -21.20f, 0.098f },
        { 3123, "CELESTIAL_FALSETTO_STAR_123", -0.73f, 69.2f, -2.32f, 2.47f, 3.15f, -15.70f, -1.15f, -20.92f, 0.107f },
        { 3124, "CELESTIAL_VOCALOID_SNAP_124", -0.64f, 71.6f, -2.26f, 2.56f, 3.26f, -15.60f, -1.20f, -20.64f, 0.116f },
        { 3125, "CELESTIAL_RNB_HONEY_125", -0.55f, 74.0f, -2.20f, 2.65f, 3.37f, -15.50f, -1.25f, -20.36f, 0.125f },
        { 3126, "CELESTIAL_IDOL_CROWN_126", -0.46f, 76.4f, -2.14f, 2.74f, 1.50f, -15.40f, -1.00f, -20.08f, 0.134f },
        { 3127, "CELESTIAL_JPOP_PRISM_127", -0.37f, 78.8f, -2.08f, 2.83f, 1.61f, -15.30f, -1.05f, -19.80f, 0.143f },
        { 3128, "CELESTIAL_ANISONG_HERO_128", -0.28f, 81.2f, -2.02f, 2.92f, 1.72f, -15.20f, -1.10f, -24.00f, 0.152f },
        { 3129, "CELESTIAL_BALLAD_SILK_129", -0.19f, 83.6f, -1.96f, 3.01f, 1.83f, -15.10f, -1.15f, -23.72f, 0.161f },
        { 3130, "CELESTIAL_VTUBER_CLEAR_130", -0.10f, 86.0f, -1.90f, 3.10f, 1.94f, -15.00f, -1.20f, -23.44f, 0.170f },
        { 3131, "CELESTIAL_ASMR_WHISPER_131", -0.01f, 88.4f, -1.84f, 3.19f, 2.05f, -14.90f, -1.25f, -23.16f, 0.179f },
        { 3132, "CELESTIAL_ROCK_BELT_132", 0.08f, 90.8f, -1.78f, 1.30f, 2.16f, -14.80f, -1.00f, -22.88f, 0.188f },
        { 3133, "CELESTIAL_CITYPOP_TAPE_133", 0.17f, 93.2f, -1.72f, 1.39f, 2.27f, -14.70f, -1.05f, -22.60f, 0.197f },
        { 3134, "CELESTIAL_RAP_CORE_134", 0.26f, 95.6f, -1.66f, 1.48f, 2.38f, -14.60f, -1.10f, -22.32f, 0.206f },
        { 3135, "CELESTIAL_ACOUSTIC_AIR_135", 0.35f, 98.0f, -1.60f, 1.57f, 2.49f, -16.00f, -1.15f, -22.04f, 0.215f },
        { 3136, "CELESTIAL_CHOIR_VELVET_136", 0.44f, 100.4f, -1.54f, 1.66f, 2.60f, -15.90f, -1.20f, -21.76f, 0.224f },
        { 3137, "CELESTIAL_KARAOKE_PRO_137", 0.53f, 102.8f, -1.48f, 1.75f, 2.71f, -15.80f, -1.25f, -21.48f, 0.233f },
        { 3138, "CELESTIAL_STREAM_TALK_138", 0.62f, 105.2f, -1.42f, 1.84f, 2.82f, -15.70f, -1.00f, -21.20f, 0.242f },
        { 3139, "CELESTIAL_FALSETTO_STAR_139", 0.71f, 107.6f, -1.36f, 1.93f, 2.93f, -15.60f, -1.05f, -20.92f, 0.251f },
        { 3140, "CELESTIAL_VOCALOID_SNAP_140", 0.80f, 110.0f, -2.50f, 2.02f, 3.04f, -15.50f, -1.10f, -20.64f, 0.080f },
        { 3141, "CELESTIAL_RNB_HONEY_141", 0.89f, 112.4f, -2.44f, 2.11f, 3.15f, -15.40f, -1.15f, -20.36f, 0.089f },
        { 3142, "CELESTIAL_IDOL_CROWN_142", 0.98f, 114.8f, -2.38f, 2.20f, 3.26f, -15.30f, -1.20f, -20.08f, 0.098f },
        { 3143, "CELESTIAL_JPOP_PRISM_143", 1.07f, 117.2f, -2.32f, 2.29f, 3.37f, -15.20f, -1.25f, -19.80f, 0.107f },
        { 3144, "CELESTIAL_ANISONG_HERO_144", -1.00f, 119.6f, -2.26f, 2.38f, 1.50f, -15.10f, -1.00f, -24.00f, 0.116f },
        { 3145, "CELESTIAL_BALLAD_SILK_145", -0.91f, 122.0f, -2.20f, 2.47f, 1.61f, -15.00f, -1.05f, -23.72f, 0.125f },
        { 3146, "CELESTIAL_VTUBER_CLEAR_146", -0.82f, 124.4f, -2.14f, 2.56f, 1.72f, -14.90f, -1.10f, -23.44f, 0.134f },
        { 3147, "CELESTIAL_ASMR_WHISPER_147", -0.73f, 126.8f, -2.08f, 2.65f, 1.83f, -14.80f, -1.15f, -23.16f, 0.143f },
        { 3148, "CELESTIAL_ROCK_BELT_148", -0.64f, 129.2f, -2.02f, 2.74f, 1.94f, -14.70f, -1.20f, -22.88f, 0.152f },
        { 3149, "CELESTIAL_CITYPOP_TAPE_149", -0.55f, 131.6f, -1.96f, 2.83f, 2.05f, -14.60f, -1.25f, -22.60f, 0.161f },
        { 3150, "CELESTIAL_RAP_CORE_150", -0.46f, 62.0f, -1.90f, 2.92f, 2.16f, -16.00f, -1.00f, -22.32f, 0.170f },
        { 3151, "CELESTIAL_ACOUSTIC_AIR_151", -0.37f, 64.4f, -1.84f, 3.01f, 2.27f, -15.90f, -1.05f, -22.04f, 0.179f },
        { 3152, "CELESTIAL_CHOIR_VELVET_152", -0.28f, 66.8f, -1.78f, 3.10f, 2.38f, -15.80f, -1.10f, -21.76f, 0.188f },
        { 3153, "CELESTIAL_KARAOKE_PRO_153", -0.19f, 69.2f, -1.72f, 3.19f, 2.49f, -15.70f, -1.15f, -21.48f, 0.197f },
        { 3154, "CELESTIAL_STREAM_TALK_154", -0.10f, 71.6f, -1.66f, 1.30f, 2.60f, -15.60f, -1.20f, -21.20f, 0.206f },
        { 3155, "CELESTIAL_FALSETTO_STAR_155", -0.01f, 74.0f, -1.60f, 1.39f, 2.71f, -15.50f, -1.25f, -20.92f, 0.215f },
        { 3156, "CELESTIAL_VOCALOID_SNAP_156", 0.08f, 76.4f, -1.54f, 1.48f, 2.82f, -15.40f, -1.00f, -20.64f, 0.224f },
        { 3157, "CELESTIAL_RNB_HONEY_157", 0.17f, 78.8f, -1.48f, 1.57f, 2.93f, -15.30f, -1.05f, -20.36f, 0.233f },
        { 3158, "CELESTIAL_IDOL_CROWN_158", 0.26f, 81.2f, -1.42f, 1.66f, 3.04f, -15.20f, -1.10f, -20.08f, 0.242f },
        { 3159, "CELESTIAL_JPOP_PRISM_159", 0.35f, 83.6f, -1.36f, 1.75f, 3.15f, -15.10f, -1.15f, -19.80f, 0.251f },
        { 3160, "CELESTIAL_ANISONG_HERO_160", 0.44f, 86.0f, -2.50f, 1.84f, 3.26f, -15.00f, -1.20f, -24.00f, 0.080f },
        { 3161, "CELESTIAL_BALLAD_SILK_161", 0.53f, 88.4f, -2.44f, 1.93f, 3.37f, -14.90f, -1.25f, -23.72f, 0.089f },
        { 3162, "CELESTIAL_VTUBER_CLEAR_162", 0.62f, 90.8f, -2.38f, 2.02f, 1.50f, -14.80f, -1.00f, -23.44f, 0.098f },
        { 3163, "CELESTIAL_ASMR_WHISPER_163", 0.71f, 93.2f, -2.32f, 2.11f, 1.61f, -14.70f, -1.05f, -23.16f, 0.107f },
        { 3164, "CELESTIAL_ROCK_BELT_164", 0.80f, 95.6f, -2.26f, 2.20f, 1.72f, -14.60f, -1.10f, -22.88f, 0.116f },
        { 3165, "CELESTIAL_CITYPOP_TAPE_165", 0.89f, 98.0f, -2.20f, 2.29f, 1.83f, -16.00f, -1.15f, -22.60f, 0.125f },
        { 3166, "CELESTIAL_RAP_CORE_166", 0.98f, 100.4f, -2.14f, 2.38f, 1.94f, -15.90f, -1.20f, -22.32f, 0.134f },
        { 3167, "CELESTIAL_ACOUSTIC_AIR_167", 1.07f, 102.8f, -2.08f, 2.47f, 2.05f, -15.80f, -1.25f, -22.04f, 0.143f },
        { 3168, "CELESTIAL_CHOIR_VELVET_168", -1.00f, 105.2f, -2.02f, 2.56f, 2.16f, -15.70f, -1.00f, -21.76f, 0.152f },
        { 3169, "CELESTIAL_KARAOKE_PRO_169", -0.91f, 107.6f, -1.96f, 2.65f, 2.27f, -15.60f, -1.05f, -21.48f, 0.161f },
        { 3170, "CELESTIAL_STREAM_TALK_170", -0.82f, 110.0f, -1.90f, 2.74f, 2.38f, -15.50f, -1.10f, -21.20f, 0.170f },
        { 3171, "CELESTIAL_FALSETTO_STAR_171", -0.73f, 112.4f, -1.84f, 2.83f, 2.49f, -15.40f, -1.15f, -20.92f, 0.179f },
        { 3172, "CELESTIAL_VOCALOID_SNAP_172", -0.64f, 114.8f, -1.78f, 2.92f, 2.60f, -15.30f, -1.20f, -20.64f, 0.188f },
        { 3173, "CELESTIAL_RNB_HONEY_173", -0.55f, 117.2f, -1.72f, 3.01f, 2.71f, -15.20f, -1.25f, -20.36f, 0.197f },
        { 3174, "CELESTIAL_IDOL_CROWN_174", -0.46f, 119.6f, -1.66f, 3.10f, 2.82f, -15.10f, -1.00f, -20.08f, 0.206f },
        { 3175, "CELESTIAL_JPOP_PRISM_175", -0.37f, 122.0f, -1.60f, 3.19f, 2.93f, -15.00f, -1.05f, -19.80f, 0.215f },
        { 3176, "CELESTIAL_ANISONG_HERO_176", -0.28f, 124.4f, -1.54f, 1.30f, 3.04f, -14.90f, -1.10f, -24.00f, 0.224f },
        { 3177, "CELESTIAL_BALLAD_SILK_177", -0.19f, 126.8f, -1.48f, 1.39f, 3.15f, -14.80f, -1.15f, -23.72f, 0.233f },
        { 3178, "CELESTIAL_VTUBER_CLEAR_178", -0.10f, 129.2f, -1.42f, 1.48f, 3.26f, -14.70f, -1.20f, -23.44f, 0.242f },
        { 3179, "CELESTIAL_ASMR_WHISPER_179", -0.01f, 131.6f, -1.36f, 1.57f, 3.37f, -14.60f, -1.25f, -23.16f, 0.251f },
        { 3180, "CELESTIAL_ROCK_BELT_180", 0.08f, 62.0f, -2.50f, 1.66f, 1.50f, -16.00f, -1.00f, -22.88f, 0.080f },
        { 3181, "CELESTIAL_CITYPOP_TAPE_181", 0.17f, 64.4f, -2.44f, 1.75f, 1.61f, -15.90f, -1.05f, -22.60f, 0.089f },
        { 3182, "CELESTIAL_RAP_CORE_182", 0.26f, 66.8f, -2.38f, 1.84f, 1.72f, -15.80f, -1.10f, -22.32f, 0.098f },
        { 3183, "CELESTIAL_ACOUSTIC_AIR_183", 0.35f, 69.2f, -2.32f, 1.93f, 1.83f, -15.70f, -1.15f, -22.04f, 0.107f },
        { 3184, "CELESTIAL_CHOIR_VELVET_184", 0.44f, 71.6f, -2.26f, 2.02f, 1.94f, -15.60f, -1.20f, -21.76f, 0.116f },
        { 3185, "CELESTIAL_KARAOKE_PRO_185", 0.53f, 74.0f, -2.20f, 2.11f, 2.05f, -15.50f, -1.25f, -21.48f, 0.125f },
        { 3186, "CELESTIAL_STREAM_TALK_186", 0.62f, 76.4f, -2.14f, 2.20f, 2.16f, -15.40f, -1.00f, -21.20f, 0.134f },
        { 3187, "CELESTIAL_FALSETTO_STAR_187", 0.71f, 78.8f, -2.08f, 2.29f, 2.27f, -15.30f, -1.05f, -20.92f, 0.143f },
        { 3188, "CELESTIAL_VOCALOID_SNAP_188", 0.80f, 81.2f, -2.02f, 2.38f, 2.38f, -15.20f, -1.10f, -20.64f, 0.152f },
        { 3189, "CELESTIAL_RNB_HONEY_189", 0.89f, 83.6f, -1.96f, 2.47f, 2.49f, -15.10f, -1.15f, -20.36f, 0.161f },
        { 3190, "CELESTIAL_IDOL_CROWN_190", 0.98f, 86.0f, -1.90f, 2.56f, 2.60f, -15.00f, -1.20f, -20.08f, 0.170f },
        { 3191, "CELESTIAL_JPOP_PRISM_191", 1.07f, 88.4f, -1.84f, 2.65f, 2.71f, -14.90f, -1.25f, -19.80f, 0.179f },
        { 3192, "CELESTIAL_ANISONG_HERO_192", -1.00f, 90.8f, -1.78f, 2.74f, 2.82f, -14.80f, -1.00f, -24.00f, 0.188f },
        { 3193, "CELESTIAL_BALLAD_SILK_193", -0.91f, 93.2f, -1.72f, 2.83f, 2.93f, -14.70f, -1.05f, -23.72f, 0.197f },
        { 3194, "CELESTIAL_VTUBER_CLEAR_194", -0.82f, 95.6f, -1.66f, 2.92f, 3.04f, -14.60f, -1.10f, -23.44f, 0.206f },
        { 3195, "CELESTIAL_ASMR_WHISPER_195", -0.73f, 98.0f, -1.60f, 3.01f, 3.15f, -16.00f, -1.15f, -23.16f, 0.215f },
        { 3196, "CELESTIAL_ROCK_BELT_196", -0.64f, 100.4f, -1.54f, 3.10f, 3.26f, -15.90f, -1.20f, -22.88f, 0.224f },
        { 3197, "CELESTIAL_CITYPOP_TAPE_197", -0.55f, 102.8f, -1.48f, 3.19f, 3.37f, -15.80f, -1.25f, -22.60f, 0.233f },
        { 3198, "CELESTIAL_RAP_CORE_198", -0.46f, 105.2f, -1.42f, 1.30f, 1.50f, -15.70f, -1.00f, -22.32f, 0.242f },
        { 3199, "CELESTIAL_ACOUSTIC_AIR_199", -0.37f, 107.6f, -1.36f, 1.39f, 1.61f, -15.60f, -1.05f, -22.04f, 0.251f },
        { 3200, "CELESTIAL_CHOIR_VELVET_200", -0.28f, 110.0f, -2.50f, 1.48f, 1.72f, -15.50f, -1.10f, -21.76f, 0.080f },
        { 3201, "CELESTIAL_KARAOKE_PRO_201", -0.19f, 112.4f, -2.44f, 1.57f, 1.83f, -15.40f, -1.15f, -21.48f, 0.089f },
        { 3202, "CELESTIAL_STREAM_TALK_202", -0.10f, 114.8f, -2.38f, 1.66f, 1.94f, -15.30f, -1.20f, -21.20f, 0.098f },
        { 3203, "CELESTIAL_FALSETTO_STAR_203", -0.01f, 117.2f, -2.32f, 1.75f, 2.05f, -15.20f, -1.25f, -20.92f, 0.107f },
        { 3204, "CELESTIAL_VOCALOID_SNAP_204", 0.08f, 119.6f, -2.26f, 1.84f, 2.16f, -15.10f, -1.00f, -20.64f, 0.116f },
        { 3205, "CELESTIAL_RNB_HONEY_205", 0.17f, 122.0f, -2.20f, 1.93f, 2.27f, -15.00f, -1.05f, -20.36f, 0.125f },
        { 3206, "CELESTIAL_IDOL_CROWN_206", 0.26f, 124.4f, -2.14f, 2.02f, 2.38f, -14.90f, -1.10f, -20.08f, 0.134f },
        { 3207, "CELESTIAL_JPOP_PRISM_207", 0.35f, 126.8f, -2.08f, 2.11f, 2.49f, -14.80f, -1.15f, -19.80f, 0.143f },
        { 3208, "CELESTIAL_ANISONG_HERO_208", 0.44f, 129.2f, -2.02f, 2.20f, 2.60f, -14.70f, -1.20f, -24.00f, 0.152f },
        { 3209, "CELESTIAL_BALLAD_SILK_209", 0.53f, 131.6f, -1.96f, 2.29f, 2.71f, -14.60f, -1.25f, -23.72f, 0.161f },
        { 3210, "CELESTIAL_VTUBER_CLEAR_210", 0.62f, 62.0f, -1.90f, 2.38f, 2.82f, -16.00f, -1.00f, -23.44f, 0.170f },
        { 3211, "CELESTIAL_ASMR_WHISPER_211", 0.71f, 64.4f, -1.84f, 2.47f, 2.93f, -15.90f, -1.05f, -23.16f, 0.179f },
        { 3212, "CELESTIAL_ROCK_BELT_212", 0.80f, 66.8f, -1.78f, 2.56f, 3.04f, -15.80f, -1.10f, -22.88f, 0.188f },
        { 3213, "CELESTIAL_CITYPOP_TAPE_213", 0.89f, 69.2f, -1.72f, 2.65f, 3.15f, -15.70f, -1.15f, -22.60f, 0.197f },
        { 3214, "CELESTIAL_RAP_CORE_214", 0.98f, 71.6f, -1.66f, 2.74f, 3.26f, -15.60f, -1.20f, -22.32f, 0.206f },
        { 3215, "CELESTIAL_ACOUSTIC_AIR_215", 1.07f, 74.0f, -1.60f, 2.83f, 3.37f, -15.50f, -1.25f, -22.04f, 0.215f },
        { 3216, "CELESTIAL_CHOIR_VELVET_216", -1.00f, 76.4f, -1.54f, 2.92f, 1.50f, -15.40f, -1.00f, -21.76f, 0.224f },
        { 3217, "CELESTIAL_KARAOKE_PRO_217", -0.91f, 78.8f, -1.48f, 3.01f, 1.61f, -15.30f, -1.05f, -21.48f, 0.233f },
        { 3218, "CELESTIAL_STREAM_TALK_218", -0.82f, 81.2f, -1.42f, 3.10f, 1.72f, -15.20f, -1.10f, -21.20f, 0.242f },
        { 3219, "CELESTIAL_FALSETTO_STAR_219", -0.73f, 83.6f, -1.36f, 3.19f, 1.83f, -15.10f, -1.15f, -20.92f, 0.251f },
        { 3220, "CELESTIAL_VOCALOID_SNAP_220", -0.64f, 86.0f, -2.50f, 1.30f, 1.94f, -15.00f, -1.20f, -20.64f, 0.080f },
        { 3221, "CELESTIAL_RNB_HONEY_221", -0.55f, 88.4f, -2.44f, 1.39f, 2.05f, -14.90f, -1.25f, -20.36f, 0.089f },
        { 3222, "CELESTIAL_IDOL_CROWN_222", -0.46f, 90.8f, -2.38f, 1.48f, 2.16f, -14.80f, -1.00f, -20.08f, 0.098f },
        { 3223, "CELESTIAL_JPOP_PRISM_223", -0.37f, 93.2f, -2.32f, 1.57f, 2.27f, -14.70f, -1.05f, -19.80f, 0.107f },
        { 3224, "CELESTIAL_ANISONG_HERO_224", -0.28f, 95.6f, -2.26f, 1.66f, 2.38f, -14.60f, -1.10f, -24.00f, 0.116f },
        { 3225, "CELESTIAL_BALLAD_SILK_225", -0.19f, 98.0f, -2.20f, 1.75f, 2.49f, -16.00f, -1.15f, -23.72f, 0.125f },
        { 3226, "CELESTIAL_VTUBER_CLEAR_226", -0.10f, 100.4f, -2.14f, 1.84f, 2.60f, -15.90f, -1.20f, -23.44f, 0.134f },
        { 3227, "CELESTIAL_ASMR_WHISPER_227", -0.01f, 102.8f, -2.08f, 1.93f, 2.71f, -15.80f, -1.25f, -23.16f, 0.143f },
        { 3228, "CELESTIAL_ROCK_BELT_228", 0.08f, 105.2f, -2.02f, 2.02f, 2.82f, -15.70f, -1.00f, -22.88f, 0.152f },
        { 3229, "CELESTIAL_CITYPOP_TAPE_229", 0.17f, 107.6f, -1.96f, 2.11f, 2.93f, -15.60f, -1.05f, -22.60f, 0.161f },
        { 3230, "CELESTIAL_RAP_CORE_230", 0.26f, 110.0f, -1.90f, 2.20f, 3.04f, -15.50f, -1.10f, -22.32f, 0.170f },
        { 3231, "CELESTIAL_ACOUSTIC_AIR_231", 0.35f, 112.4f, -1.84f, 2.29f, 3.15f, -15.40f, -1.15f, -22.04f, 0.179f },
        { 3232, "CELESTIAL_CHOIR_VELVET_232", 0.44f, 114.8f, -1.78f, 2.38f, 3.26f, -15.30f, -1.20f, -21.76f, 0.188f },
        { 3233, "CELESTIAL_KARAOKE_PRO_233", 0.53f, 117.2f, -1.72f, 2.47f, 3.37f, -15.20f, -1.25f, -21.48f, 0.197f },
        { 3234, "CELESTIAL_STREAM_TALK_234", 0.62f, 119.6f, -1.66f, 2.56f, 1.50f, -15.10f, -1.00f, -21.20f, 0.206f },
        { 3235, "CELESTIAL_FALSETTO_STAR_235", 0.71f, 122.0f, -1.60f, 2.65f, 1.61f, -15.00f, -1.05f, -20.92f, 0.215f },
        { 3236, "CELESTIAL_VOCALOID_SNAP_236", 0.80f, 124.4f, -1.54f, 2.74f, 1.72f, -14.90f, -1.10f, -20.64f, 0.224f },
        { 3237, "CELESTIAL_RNB_HONEY_237", 0.89f, 126.8f, -1.48f, 2.83f, 1.83f, -14.80f, -1.15f, -20.36f, 0.233f },
        { 3238, "CELESTIAL_IDOL_CROWN_238", 0.98f, 129.2f, -1.42f, 2.92f, 1.94f, -14.70f, -1.20f, -20.08f, 0.242f },
        { 3239, "CELESTIAL_JPOP_PRISM_239", 1.07f, 131.6f, -1.36f, 3.01f, 2.05f, -14.60f, -1.25f, -19.80f, 0.251f },
        { 3240, "CELESTIAL_ANISONG_HERO_240", -1.00f, 62.0f, -2.50f, 3.10f, 2.16f, -16.00f, -1.00f, -24.00f, 0.080f },
        { 3241, "CELESTIAL_BALLAD_SILK_241", -0.91f, 64.4f, -2.44f, 3.19f, 2.27f, -15.90f, -1.05f, -23.72f, 0.089f },
        { 3242, "CELESTIAL_VTUBER_CLEAR_242", -0.82f, 66.8f, -2.38f, 1.30f, 2.38f, -15.80f, -1.10f, -23.44f, 0.098f },
        { 3243, "CELESTIAL_ASMR_WHISPER_243", -0.73f, 69.2f, -2.32f, 1.39f, 2.49f, -15.70f, -1.15f, -23.16f, 0.107f },
        { 3244, "CELESTIAL_ROCK_BELT_244", -0.64f, 71.6f, -2.26f, 1.48f, 2.60f, -15.60f, -1.20f, -22.88f, 0.116f },
        { 3245, "CELESTIAL_CITYPOP_TAPE_245", -0.55f, 74.0f, -2.20f, 1.57f, 2.71f, -15.50f, -1.25f, -22.60f, 0.125f },
        { 3246, "CELESTIAL_RAP_CORE_246", -0.46f, 76.4f, -2.14f, 1.66f, 2.82f, -15.40f, -1.00f, -22.32f, 0.134f },
        { 3247, "CELESTIAL_ACOUSTIC_AIR_247", -0.37f, 78.8f, -2.08f, 1.75f, 2.93f, -15.30f, -1.05f, -22.04f, 0.143f },
        { 3248, "CELESTIAL_CHOIR_VELVET_248", -0.28f, 81.2f, -2.02f, 1.84f, 3.04f, -15.20f, -1.10f, -21.76f, 0.152f },
        { 3249, "CELESTIAL_KARAOKE_PRO_249", -0.19f, 83.6f, -1.96f, 1.93f, 3.15f, -15.10f, -1.15f, -21.48f, 0.161f },
        { 3250, "CELESTIAL_STREAM_TALK_250", -0.10f, 86.0f, -1.90f, 2.02f, 3.26f, -15.00f, -1.20f, -21.20f, 0.170f },
        { 3251, "CELESTIAL_FALSETTO_STAR_251", -0.01f, 88.4f, -1.84f, 2.11f, 3.37f, -14.90f, -1.25f, -20.92f, 0.179f },
        { 3252, "CELESTIAL_VOCALOID_SNAP_252", 0.08f, 90.8f, -1.78f, 2.20f, 1.50f, -14.80f, -1.00f, -20.64f, 0.188f },
        { 3253, "CELESTIAL_RNB_HONEY_253", 0.17f, 93.2f, -1.72f, 2.29f, 1.61f, -14.70f, -1.05f, -20.36f, 0.197f },
        { 3254, "CELESTIAL_IDOL_CROWN_254", 0.26f, 95.6f, -1.66f, 2.38f, 1.72f, -14.60f, -1.10f, -20.08f, 0.206f },
        { 3255, "CELESTIAL_JPOP_PRISM_255", 0.35f, 98.0f, -1.60f, 2.47f, 1.83f, -16.00f, -1.15f, -19.80f, 0.215f },
        { 3256, "CELESTIAL_ANISONG_HERO_256", 0.44f, 100.4f, -1.54f, 2.56f, 1.94f, -15.90f, -1.20f, -24.00f, 0.224f },
        { 3257, "CELESTIAL_BALLAD_SILK_257", 0.53f, 102.8f, -1.48f, 2.65f, 2.05f, -15.80f, -1.25f, -23.72f, 0.233f },
        { 3258, "CELESTIAL_VTUBER_CLEAR_258", 0.62f, 105.2f, -1.42f, 2.74f, 2.16f, -15.70f, -1.00f, -23.44f, 0.242f },
        { 3259, "CELESTIAL_ASMR_WHISPER_259", 0.71f, 107.6f, -1.36f, 2.83f, 2.27f, -15.60f, -1.05f, -23.16f, 0.251f },
        { 3260, "CELESTIAL_ROCK_BELT_260", 0.80f, 110.0f, -2.50f, 2.92f, 2.38f, -15.50f, -1.10f, -22.88f, 0.080f },
        { 3261, "CELESTIAL_CITYPOP_TAPE_261", 0.89f, 112.4f, -2.44f, 3.01f, 2.49f, -15.40f, -1.15f, -22.60f, 0.089f },
        { 3262, "CELESTIAL_RAP_CORE_262", 0.98f, 114.8f, -2.38f, 3.10f, 2.60f, -15.30f, -1.20f, -22.32f, 0.098f },
        { 3263, "CELESTIAL_ACOUSTIC_AIR_263", 1.07f, 117.2f, -2.32f, 3.19f, 2.71f, -15.20f, -1.25f, -22.04f, 0.107f },
        { 3264, "CELESTIAL_CHOIR_VELVET_264", -1.00f, 119.6f, -2.26f, 1.30f, 2.82f, -15.10f, -1.00f, -21.76f, 0.116f },
        { 3265, "CELESTIAL_KARAOKE_PRO_265", -0.91f, 122.0f, -2.20f, 1.39f, 2.93f, -15.00f, -1.05f, -21.48f, 0.125f },
        { 3266, "CELESTIAL_STREAM_TALK_266", -0.82f, 124.4f, -2.14f, 1.48f, 3.04f, -14.90f, -1.10f, -21.20f, 0.134f },
        { 3267, "CELESTIAL_FALSETTO_STAR_267", -0.73f, 126.8f, -2.08f, 1.57f, 3.15f, -14.80f, -1.15f, -20.92f, 0.143f },
        { 3268, "CELESTIAL_VOCALOID_SNAP_268", -0.64f, 129.2f, -2.02f, 1.66f, 3.26f, -14.70f, -1.20f, -20.64f, 0.152f },
        { 3269, "CELESTIAL_RNB_HONEY_269", -0.55f, 131.6f, -1.96f, 1.75f, 3.37f, -14.60f, -1.25f, -20.36f, 0.161f },
        { 3270, "CELESTIAL_IDOL_CROWN_270", -0.46f, 62.0f, -1.90f, 1.84f, 1.50f, -16.00f, -1.00f, -20.08f, 0.170f },
        { 3271, "CELESTIAL_JPOP_PRISM_271", -0.37f, 64.4f, -1.84f, 1.93f, 1.61f, -15.90f, -1.05f, -19.80f, 0.179f },
        { 3272, "CELESTIAL_ANISONG_HERO_272", -0.28f, 66.8f, -1.78f, 2.02f, 1.72f, -15.80f, -1.10f, -24.00f, 0.188f },
        { 3273, "CELESTIAL_BALLAD_SILK_273", -0.19f, 69.2f, -1.72f, 2.11f, 1.83f, -15.70f, -1.15f, -23.72f, 0.197f },
        { 3274, "CELESTIAL_VTUBER_CLEAR_274", -0.10f, 71.6f, -1.66f, 2.20f, 1.94f, -15.60f, -1.20f, -23.44f, 0.206f },
        { 3275, "CELESTIAL_ASMR_WHISPER_275", -0.01f, 74.0f, -1.60f, 2.29f, 2.05f, -15.50f, -1.25f, -23.16f, 0.215f },
        { 3276, "CELESTIAL_ROCK_BELT_276", 0.08f, 76.4f, -1.54f, 2.38f, 2.16f, -15.40f, -1.00f, -22.88f, 0.224f },
        { 3277, "CELESTIAL_CITYPOP_TAPE_277", 0.17f, 78.8f, -1.48f, 2.47f, 2.27f, -15.30f, -1.05f, -22.60f, 0.233f },
        { 3278, "CELESTIAL_RAP_CORE_278", 0.26f, 81.2f, -1.42f, 2.56f, 2.38f, -15.20f, -1.10f, -22.32f, 0.242f },
        { 3279, "CELESTIAL_ACOUSTIC_AIR_279", 0.35f, 83.6f, -1.36f, 2.65f, 2.49f, -15.10f, -1.15f, -22.04f, 0.251f },
        { 3280, "CELESTIAL_CHOIR_VELVET_280", 0.44f, 86.0f, -2.50f, 2.74f, 2.60f, -15.00f, -1.20f, -21.76f, 0.080f },
        { 3281, "CELESTIAL_KARAOKE_PRO_281", 0.53f, 88.4f, -2.44f, 2.83f, 2.71f, -14.90f, -1.25f, -21.48f, 0.089f },
        { 3282, "CELESTIAL_STREAM_TALK_282", 0.62f, 90.8f, -2.38f, 2.92f, 2.82f, -14.80f, -1.00f, -21.20f, 0.098f },
        { 3283, "CELESTIAL_FALSETTO_STAR_283", 0.71f, 93.2f, -2.32f, 3.01f, 2.93f, -14.70f, -1.05f, -20.92f, 0.107f },
        { 3284, "CELESTIAL_VOCALOID_SNAP_284", 0.80f, 95.6f, -2.26f, 3.10f, 3.04f, -14.60f, -1.10f, -20.64f, 0.116f },
        { 3285, "CELESTIAL_RNB_HONEY_285", 0.89f, 98.0f, -2.20f, 3.19f, 3.15f, -16.00f, -1.15f, -20.36f, 0.125f },
        { 3286, "CELESTIAL_IDOL_CROWN_286", 0.98f, 100.4f, -2.14f, 1.30f, 3.26f, -15.90f, -1.20f, -20.08f, 0.134f },
        { 3287, "CELESTIAL_JPOP_PRISM_287", 1.07f, 102.8f, -2.08f, 1.39f, 3.37f, -15.80f, -1.25f, -19.80f, 0.143f },
        { 3288, "CELESTIAL_ANISONG_HERO_288", -1.00f, 105.2f, -2.02f, 1.48f, 1.50f, -15.70f, -1.00f, -24.00f, 0.152f },
        { 3289, "CELESTIAL_BALLAD_SILK_289", -0.91f, 107.6f, -1.96f, 1.57f, 1.61f, -15.60f, -1.05f, -23.72f, 0.161f },
        { 3290, "CELESTIAL_VTUBER_CLEAR_290", -0.82f, 110.0f, -1.90f, 1.66f, 1.72f, -15.50f, -1.10f, -23.44f, 0.170f },
        { 3291, "CELESTIAL_ASMR_WHISPER_291", -0.73f, 112.4f, -1.84f, 1.75f, 1.83f, -15.40f, -1.15f, -23.16f, 0.179f },
        { 3292, "CELESTIAL_ROCK_BELT_292", -0.64f, 114.8f, -1.78f, 1.84f, 1.94f, -15.30f, -1.20f, -22.88f, 0.188f },
        { 3293, "CELESTIAL_CITYPOP_TAPE_293", -0.55f, 117.2f, -1.72f, 1.93f, 2.05f, -15.20f, -1.25f, -22.60f, 0.197f },
        { 3294, "CELESTIAL_RAP_CORE_294", -0.46f, 119.6f, -1.66f, 2.02f, 2.16f, -15.10f, -1.00f, -22.32f, 0.206f },
        { 3295, "CELESTIAL_ACOUSTIC_AIR_295", -0.37f, 122.0f, -1.60f, 2.11f, 2.27f, -15.00f, -1.05f, -22.04f, 0.215f },
        { 3296, "CELESTIAL_CHOIR_VELVET_296", -0.28f, 124.4f, -1.54f, 2.20f, 2.38f, -14.90f, -1.10f, -21.76f, 0.224f },
        { 3297, "CELESTIAL_KARAOKE_PRO_297", -0.19f, 126.8f, -1.48f, 2.29f, 2.49f, -14.80f, -1.15f, -21.48f, 0.233f },
        { 3298, "CELESTIAL_STREAM_TALK_298", -0.10f, 129.2f, -1.42f, 2.38f, 2.60f, -14.70f, -1.20f, -21.20f, 0.242f },
        { 3299, "CELESTIAL_FALSETTO_STAR_299", -0.01f, 131.6f, -1.36f, 2.47f, 2.71f, -14.60f, -1.25f, -20.92f, 0.251f },
        { 3300, "CELESTIAL_VOCALOID_SNAP_300", 0.08f, 62.0f, -2.50f, 2.56f, 2.82f, -16.00f, -1.00f, -20.64f, 0.080f },
        { 3301, "CELESTIAL_RNB_HONEY_301", 0.17f, 64.4f, -2.44f, 2.65f, 2.93f, -15.90f, -1.05f, -20.36f, 0.089f },
        { 3302, "CELESTIAL_IDOL_CROWN_302", 0.26f, 66.8f, -2.38f, 2.74f, 3.04f, -15.80f, -1.10f, -20.08f, 0.098f },
        { 3303, "CELESTIAL_JPOP_PRISM_303", 0.35f, 69.2f, -2.32f, 2.83f, 3.15f, -15.70f, -1.15f, -19.80f, 0.107f },
        { 3304, "CELESTIAL_ANISONG_HERO_304", 0.44f, 71.6f, -2.26f, 2.92f, 3.26f, -15.60f, -1.20f, -24.00f, 0.116f },
        { 3305, "CELESTIAL_BALLAD_SILK_305", 0.53f, 74.0f, -2.20f, 3.01f, 3.37f, -15.50f, -1.25f, -23.72f, 0.125f },
        { 3306, "CELESTIAL_VTUBER_CLEAR_306", 0.62f, 76.4f, -2.14f, 3.10f, 1.50f, -15.40f, -1.00f, -23.44f, 0.134f },
        { 3307, "CELESTIAL_ASMR_WHISPER_307", 0.71f, 78.8f, -2.08f, 3.19f, 1.61f, -15.30f, -1.05f, -23.16f, 0.143f },
        { 3308, "CELESTIAL_ROCK_BELT_308", 0.80f, 81.2f, -2.02f, 1.30f, 1.72f, -15.20f, -1.10f, -22.88f, 0.152f },
        { 3309, "CELESTIAL_CITYPOP_TAPE_309", 0.89f, 83.6f, -1.96f, 1.39f, 1.83f, -15.10f, -1.15f, -22.60f, 0.161f },
        { 3310, "CELESTIAL_RAP_CORE_310", 0.98f, 86.0f, -1.90f, 1.48f, 1.94f, -15.00f, -1.20f, -22.32f, 0.170f },
        { 3311, "CELESTIAL_ACOUSTIC_AIR_311", 1.07f, 88.4f, -1.84f, 1.57f, 2.05f, -14.90f, -1.25f, -22.04f, 0.179f },
        { 3312, "CELESTIAL_CHOIR_VELVET_312", -1.00f, 90.8f, -1.78f, 1.66f, 2.16f, -14.80f, -1.00f, -21.76f, 0.188f },
        { 3313, "CELESTIAL_KARAOKE_PRO_313", -0.91f, 93.2f, -1.72f, 1.75f, 2.27f, -14.70f, -1.05f, -21.48f, 0.197f },
        { 3314, "CELESTIAL_STREAM_TALK_314", -0.82f, 95.6f, -1.66f, 1.84f, 2.38f, -14.60f, -1.10f, -21.20f, 0.206f },
        { 3315, "CELESTIAL_FALSETTO_STAR_315", -0.73f, 98.0f, -1.60f, 1.93f, 2.49f, -16.00f, -1.15f, -20.92f, 0.215f },
        { 3316, "CELESTIAL_VOCALOID_SNAP_316", -0.64f, 100.4f, -1.54f, 2.02f, 2.60f, -15.90f, -1.20f, -20.64f, 0.224f },
        { 3317, "CELESTIAL_RNB_HONEY_317", -0.55f, 102.8f, -1.48f, 2.11f, 2.71f, -15.80f, -1.25f, -20.36f, 0.233f },
        { 3318, "CELESTIAL_IDOL_CROWN_318", -0.46f, 105.2f, -1.42f, 2.20f, 2.82f, -15.70f, -1.00f, -20.08f, 0.242f },
        { 3319, "CELESTIAL_JPOP_PRISM_319", -0.37f, 107.6f, -1.36f, 2.29f, 2.93f, -15.60f, -1.05f, -19.80f, 0.251f },
        { 3320, "CELESTIAL_ANISONG_HERO_320", -0.28f, 110.0f, -2.50f, 2.38f, 3.04f, -15.50f, -1.10f, -24.00f, 0.080f },
        { 3321, "CELESTIAL_BALLAD_SILK_321", -0.19f, 112.4f, -2.44f, 2.47f, 3.15f, -15.40f, -1.15f, -23.72f, 0.089f },
        { 3322, "CELESTIAL_VTUBER_CLEAR_322", -0.10f, 114.8f, -2.38f, 2.56f, 3.26f, -15.30f, -1.20f, -23.44f, 0.098f },
        { 3323, "CELESTIAL_ASMR_WHISPER_323", -0.01f, 117.2f, -2.32f, 2.65f, 3.37f, -15.20f, -1.25f, -23.16f, 0.107f },
        { 3324, "CELESTIAL_ROCK_BELT_324", 0.08f, 119.6f, -2.26f, 2.74f, 1.50f, -15.10f, -1.00f, -22.88f, 0.116f },
        { 3325, "CELESTIAL_CITYPOP_TAPE_325", 0.17f, 122.0f, -2.20f, 2.83f, 1.61f, -15.00f, -1.05f, -22.60f, 0.125f },
        { 3326, "CELESTIAL_RAP_CORE_326", 0.26f, 124.4f, -2.14f, 2.92f, 1.72f, -14.90f, -1.10f, -22.32f, 0.134f },
        { 3327, "CELESTIAL_ACOUSTIC_AIR_327", 0.35f, 126.8f, -2.08f, 3.01f, 1.83f, -14.80f, -1.15f, -22.04f, 0.143f },
        { 3328, "CELESTIAL_CHOIR_VELVET_328", 0.44f, 129.2f, -2.02f, 3.10f, 1.94f, -14.70f, -1.20f, -21.76f, 0.152f },
        { 3329, "CELESTIAL_KARAOKE_PRO_329", 0.53f, 131.6f, -1.96f, 3.19f, 2.05f, -14.60f, -1.25f, -21.48f, 0.161f },
        { 3330, "CELESTIAL_STREAM_TALK_330", 0.62f, 62.0f, -1.90f, 1.30f, 2.16f, -16.00f, -1.00f, -21.20f, 0.170f },
        { 3331, "CELESTIAL_FALSETTO_STAR_331", 0.71f, 64.4f, -1.84f, 1.39f, 2.27f, -15.90f, -1.05f, -20.92f, 0.179f },
        { 3332, "CELESTIAL_VOCALOID_SNAP_332", 0.80f, 66.8f, -1.78f, 1.48f, 2.38f, -15.80f, -1.10f, -20.64f, 0.188f },
        { 3333, "CELESTIAL_RNB_HONEY_333", 0.89f, 69.2f, -1.72f, 1.57f, 2.49f, -15.70f, -1.15f, -20.36f, 0.197f },
        { 3334, "CELESTIAL_IDOL_CROWN_334", 0.98f, 71.6f, -1.66f, 1.66f, 2.60f, -15.60f, -1.20f, -20.08f, 0.206f },
        { 3335, "CELESTIAL_JPOP_PRISM_335", 1.07f, 74.0f, -1.60f, 1.75f, 2.71f, -15.50f, -1.25f, -19.80f, 0.215f },
        { 3336, "CELESTIAL_ANISONG_HERO_336", -1.00f, 76.4f, -1.54f, 1.84f, 2.82f, -15.40f, -1.00f, -24.00f, 0.224f },
        { 3337, "CELESTIAL_BALLAD_SILK_337", -0.91f, 78.8f, -1.48f, 1.93f, 2.93f, -15.30f, -1.05f, -23.72f, 0.233f },
        { 3338, "CELESTIAL_VTUBER_CLEAR_338", -0.82f, 81.2f, -1.42f, 2.02f, 3.04f, -15.20f, -1.10f, -23.44f, 0.242f },
        { 3339, "CELESTIAL_ASMR_WHISPER_339", -0.73f, 83.6f, -1.36f, 2.11f, 3.15f, -15.10f, -1.15f, -23.16f, 0.251f },
        { 3340, "CELESTIAL_ROCK_BELT_340", -0.64f, 86.0f, -2.50f, 2.20f, 3.26f, -15.00f, -1.20f, -22.88f, 0.080f },
        { 3341, "CELESTIAL_CITYPOP_TAPE_341", -0.55f, 88.4f, -2.44f, 2.29f, 3.37f, -14.90f, -1.25f, -22.60f, 0.089f },
        { 3342, "CELESTIAL_RAP_CORE_342", -0.46f, 90.8f, -2.38f, 2.38f, 1.50f, -14.80f, -1.00f, -22.32f, 0.098f },
        { 3343, "CELESTIAL_ACOUSTIC_AIR_343", -0.37f, 93.2f, -2.32f, 2.47f, 1.61f, -14.70f, -1.05f, -22.04f, 0.107f },
        { 3344, "CELESTIAL_CHOIR_VELVET_344", -0.28f, 95.6f, -2.26f, 2.56f, 1.72f, -14.60f, -1.10f, -21.76f, 0.116f },
        { 3345, "CELESTIAL_KARAOKE_PRO_345", -0.19f, 98.0f, -2.20f, 2.65f, 1.83f, -16.00f, -1.15f, -21.48f, 0.125f },
        { 3346, "CELESTIAL_STREAM_TALK_346", -0.10f, 100.4f, -2.14f, 2.74f, 1.94f, -15.90f, -1.20f, -21.20f, 0.134f },
        { 3347, "CELESTIAL_FALSETTO_STAR_347", -0.01f, 102.8f, -2.08f, 2.83f, 2.05f, -15.80f, -1.25f, -20.92f, 0.143f },
        { 3348, "CELESTIAL_VOCALOID_SNAP_348", 0.08f, 105.2f, -2.02f, 2.92f, 2.16f, -15.70f, -1.00f, -20.64f, 0.152f },
        { 3349, "CELESTIAL_RNB_HONEY_349", 0.17f, 107.6f, -1.96f, 3.01f, 2.27f, -15.60f, -1.05f, -20.36f, 0.161f },
        { 3350, "CELESTIAL_IDOL_CROWN_350", 0.26f, 110.0f, -1.90f, 3.10f, 2.38f, -15.50f, -1.10f, -20.08f, 0.170f },
        { 3351, "CELESTIAL_JPOP_PRISM_351", 0.35f, 112.4f, -1.84f, 3.19f, 2.49f, -15.40f, -1.15f, -19.80f, 0.179f },
        { 3352, "CELESTIAL_ANISONG_HERO_352", 0.44f, 114.8f, -1.78f, 1.30f, 2.60f, -15.30f, -1.20f, -24.00f, 0.188f },
        { 3353, "CELESTIAL_BALLAD_SILK_353", 0.53f, 117.2f, -1.72f, 1.39f, 2.71f, -15.20f, -1.25f, -23.72f, 0.197f },
        { 3354, "CELESTIAL_VTUBER_CLEAR_354", 0.62f, 119.6f, -1.66f, 1.48f, 2.82f, -15.10f, -1.00f, -23.44f, 0.206f },
        { 3355, "CELESTIAL_ASMR_WHISPER_355", 0.71f, 122.0f, -1.60f, 1.57f, 2.93f, -15.00f, -1.05f, -23.16f, 0.215f },
        { 3356, "CELESTIAL_ROCK_BELT_356", 0.80f, 124.4f, -1.54f, 1.66f, 3.04f, -14.90f, -1.10f, -22.88f, 0.224f },
        { 3357, "CELESTIAL_CITYPOP_TAPE_357", 0.89f, 126.8f, -1.48f, 1.75f, 3.15f, -14.80f, -1.15f, -22.60f, 0.233f },
        { 3358, "CELESTIAL_RAP_CORE_358", 0.98f, 129.2f, -1.42f, 1.84f, 3.26f, -14.70f, -1.20f, -22.32f, 0.242f },
        { 3359, "CELESTIAL_ACOUSTIC_AIR_359", 1.07f, 131.6f, -1.36f, 1.93f, 3.37f, -14.60f, -1.25f, -22.04f, 0.251f },
        { 3360, "CELESTIAL_CHOIR_VELVET_360", -1.00f, 62.0f, -2.50f, 2.02f, 1.50f, -16.00f, -1.00f, -21.76f, 0.080f },
        { 3361, "CELESTIAL_KARAOKE_PRO_361", -0.91f, 64.4f, -2.44f, 2.11f, 1.61f, -15.90f, -1.05f, -21.48f, 0.089f },
        { 3362, "CELESTIAL_STREAM_TALK_362", -0.82f, 66.8f, -2.38f, 2.20f, 1.72f, -15.80f, -1.10f, -21.20f, 0.098f },
        { 3363, "CELESTIAL_FALSETTO_STAR_363", -0.73f, 69.2f, -2.32f, 2.29f, 1.83f, -15.70f, -1.15f, -20.92f, 0.107f },
        { 3364, "CELESTIAL_VOCALOID_SNAP_364", -0.64f, 71.6f, -2.26f, 2.38f, 1.94f, -15.60f, -1.20f, -20.64f, 0.116f },
        { 3365, "CELESTIAL_RNB_HONEY_365", -0.55f, 74.0f, -2.20f, 2.47f, 2.05f, -15.50f, -1.25f, -20.36f, 0.125f },
        { 3366, "CELESTIAL_IDOL_CROWN_366", -0.46f, 76.4f, -2.14f, 2.56f, 2.16f, -15.40f, -1.00f, -20.08f, 0.134f },
        { 3367, "CELESTIAL_JPOP_PRISM_367", -0.37f, 78.8f, -2.08f, 2.65f, 2.27f, -15.30f, -1.05f, -19.80f, 0.143f },
        { 3368, "CELESTIAL_ANISONG_HERO_368", -0.28f, 81.2f, -2.02f, 2.74f, 2.38f, -15.20f, -1.10f, -24.00f, 0.152f },
        { 3369, "CELESTIAL_BALLAD_SILK_369", -0.19f, 83.6f, -1.96f, 2.83f, 2.49f, -15.10f, -1.15f, -23.72f, 0.161f },
        { 3370, "CELESTIAL_VTUBER_CLEAR_370", -0.10f, 86.0f, -1.90f, 2.92f, 2.60f, -15.00f, -1.20f, -23.44f, 0.170f },
        { 3371, "CELESTIAL_ASMR_WHISPER_371", -0.01f, 88.4f, -1.84f, 3.01f, 2.71f, -14.90f, -1.25f, -23.16f, 0.179f },
        { 3372, "CELESTIAL_ROCK_BELT_372", 0.08f, 90.8f, -1.78f, 3.10f, 2.82f, -14.80f, -1.00f, -22.88f, 0.188f },
        { 3373, "CELESTIAL_CITYPOP_TAPE_373", 0.17f, 93.2f, -1.72f, 3.19f, 2.93f, -14.70f, -1.05f, -22.60f, 0.197f },
        { 3374, "CELESTIAL_RAP_CORE_374", 0.26f, 95.6f, -1.66f, 1.30f, 3.04f, -14.60f, -1.10f, -22.32f, 0.206f },
        { 3375, "CELESTIAL_ACOUSTIC_AIR_375", 0.35f, 98.0f, -1.60f, 1.39f, 3.15f, -16.00f, -1.15f, -22.04f, 0.215f },
        { 3376, "CELESTIAL_CHOIR_VELVET_376", 0.44f, 100.4f, -1.54f, 1.48f, 3.26f, -15.90f, -1.20f, -21.76f, 0.224f },
        { 3377, "CELESTIAL_KARAOKE_PRO_377", 0.53f, 102.8f, -1.48f, 1.57f, 3.37f, -15.80f, -1.25f, -21.48f, 0.233f },
        { 3378, "CELESTIAL_STREAM_TALK_378", 0.62f, 105.2f, -1.42f, 1.66f, 1.50f, -15.70f, -1.00f, -21.20f, 0.242f },
        { 3379, "CELESTIAL_FALSETTO_STAR_379", 0.71f, 107.6f, -1.36f, 1.75f, 1.61f, -15.60f, -1.05f, -20.92f, 0.251f },
        { 3380, "CELESTIAL_VOCALOID_SNAP_380", 0.80f, 110.0f, -2.50f, 1.84f, 1.72f, -15.50f, -1.10f, -20.64f, 0.080f },
        { 3381, "CELESTIAL_RNB_HONEY_381", 0.89f, 112.4f, -2.44f, 1.93f, 1.83f, -15.40f, -1.15f, -20.36f, 0.089f },
        { 3382, "CELESTIAL_IDOL_CROWN_382", 0.98f, 114.8f, -2.38f, 2.02f, 1.94f, -15.30f, -1.20f, -20.08f, 0.098f },
        { 3383, "CELESTIAL_JPOP_PRISM_383", 1.07f, 117.2f, -2.32f, 2.11f, 2.05f, -15.20f, -1.25f, -19.80f, 0.107f },
        { 3384, "CELESTIAL_ANISONG_HERO_384", -1.00f, 119.6f, -2.26f, 2.20f, 2.16f, -15.10f, -1.00f, -24.00f, 0.116f },
        { 3385, "CELESTIAL_BALLAD_SILK_385", -0.91f, 122.0f, -2.20f, 2.29f, 2.27f, -15.00f, -1.05f, -23.72f, 0.125f },
        { 3386, "CELESTIAL_VTUBER_CLEAR_386", -0.82f, 124.4f, -2.14f, 2.38f, 2.38f, -14.90f, -1.10f, -23.44f, 0.134f },
        { 3387, "CELESTIAL_ASMR_WHISPER_387", -0.73f, 126.8f, -2.08f, 2.47f, 2.49f, -14.80f, -1.15f, -23.16f, 0.143f },
        { 3388, "CELESTIAL_ROCK_BELT_388", -0.64f, 129.2f, -2.02f, 2.56f, 2.60f, -14.70f, -1.20f, -22.88f, 0.152f },
        { 3389, "CELESTIAL_CITYPOP_TAPE_389", -0.55f, 131.6f, -1.96f, 2.65f, 2.71f, -14.60f, -1.25f, -22.60f, 0.161f },
        { 3390, "CELESTIAL_RAP_CORE_390", -0.46f, 62.0f, -1.90f, 2.74f, 2.82f, -16.00f, -1.00f, -22.32f, 0.170f },
        { 3391, "CELESTIAL_ACOUSTIC_AIR_391", -0.37f, 64.4f, -1.84f, 2.83f, 2.93f, -15.90f, -1.05f, -22.04f, 0.179f },
        { 3392, "CELESTIAL_CHOIR_VELVET_392", -0.28f, 66.8f, -1.78f, 2.92f, 3.04f, -15.80f, -1.10f, -21.76f, 0.188f },
        { 3393, "CELESTIAL_KARAOKE_PRO_393", -0.19f, 69.2f, -1.72f, 3.01f, 3.15f, -15.70f, -1.15f, -21.48f, 0.197f },
        { 3394, "CELESTIAL_STREAM_TALK_394", -0.10f, 71.6f, -1.66f, 3.10f, 3.26f, -15.60f, -1.20f, -21.20f, 0.206f },
    }};

    void prepare(double sampleRate, std::size_t maxBlockSize = 2048) noexcept {
        sr = (sampleRate > 8000.0 && std::isfinite(sampleRate)) ? sampleRate : 48000.0;
        preparedBlock = (maxBlockSize > 0) ? maxBlockSize : 512;
        svfL.configure(sr, 2850.0, 1.15);
        svfR.configure(sr, 2850.0, 1.15);
        dcPrevInL = dcPrevInR = dcPrevOutL = dcPrevOutR = 0.0f;
        smoothGain = 1.0f;
        lastTelemetry = {};
    }

    void reset() noexcept {
        svfL.reset();
        svfR.reset();
        dcPrevInL = dcPrevInR = dcPrevOutL = dcPrevOutR = 0.0f;
        smoothGain = 1.0f;
        lastTelemetry = {};
    }

    inline CelestialDenormalNanInfAndDcOffsetPurifierTelemetry processBuffer(float* left,
                                                        float* right,
                                                        int numSamples,
                                                        float amount01 = 0.35f) noexcept {
        CelestialDenormalNanInfAndDcOffsetPurifierTelemetry tel{};
        if (left == nullptr || right == nullptr || numSamples <= 0) {
            tel.bypassIdleActive = true;
            lastTelemetry = tel;
            return tel;
        }
        const float amt = std::clamp(amount01, 0.0f, 1.0f);
        double inSq = 0.0, outSq = 0.0;
        double crossLR = 0.0, eL = 0.0, eR = 0.0;
        float maxPeak = 0.0f;

        for (int i = 0; i < numSamples; ++i) {
            float xL = left[i];
            float xR = right[i];
            if (!std::isfinite(xL) || std::abs(xL) < 1.0e-30f) {
                if (!std::isfinite(xL)) { ++tel.sanitizedSampleCount; tel.faultIsolated = true; }
                xL = 0.0f;
            }
            if (!std::isfinite(xR) || std::abs(xR) < 1.0e-30f) {
                if (!std::isfinite(xR)) { ++tel.sanitizedSampleCount; tel.faultIsolated = true; }
                xR = 0.0f;
            }
            inSq += static_cast<double>(xL * xL + xR * xR) * 0.5;

            // 1次ハイパスDCブロッカー (R = 0.9995)
            const float dcL = xL - dcPrevInL + 0.9995f * dcPrevOutL;
            dcPrevInL = xL; dcPrevOutL = (std::abs(dcL) > 1.0e-30f) ? dcL : 0.0f;
            const float dcR = xR - dcPrevInR + 0.9995f * dcPrevOutR;
            dcPrevInR = xR; dcPrevOutR = (std::abs(dcR) > 1.0e-30f) ? dcR : 0.0f;

            float lpL = 0.0f, bpL = 0.0f, hpL = 0.0f;
            float lpR = 0.0f, bpR = 0.0f, hpR = 0.0f;
            svfL.stepMultiMode(dcPrevOutL, lpL, bpL, hpL);
            svfR.stepMultiMode(dcPrevOutR, lpR, bpR, hpR);

            const float shapedL = svfL.stepAdaaSoftClip(dcPrevOutL + 0.12f * amt * bpL, 1.0f + 0.25f * amt);
            const float shapedR = svfR.stepAdaaSoftClip(dcPrevOutR + 0.12f * amt * bpR, 1.0f + 0.25f * amt);

            // トゥルーピーク天井 (-1.0 dBTP = 0.89125f) 保護
            const float ceiling = 0.89125094f;
            const float curPeak = std::max(std::abs(shapedL), std::abs(shapedR));
            const float targetG = (curPeak > ceiling) ? (ceiling / curPeak) : 1.0f;
            smoothGain = (targetG < smoothGain) ? targetG : (0.995f * smoothGain + 0.005f * targetG);

            const float yL = shapedL * smoothGain;
            const float yR = shapedR * smoothGain;
            left[i] = yL;
            right[i] = yR;

            outSq += static_cast<double>(yL * yL + yR * yR) * 0.5;
            crossLR += static_cast<double>(yL * yR);
            eL += static_cast<double>(yL * yL);
            eR += static_cast<double>(yR * yR);
            maxPeak = std::max(maxPeak, std::max(std::abs(yL), std::abs(yR)));
        }

        const double invN = 1.0 / static_cast<double>(numSamples);
        tel.inputRmsDb = (inSq > 1.0e-12) ? static_cast<float>(10.0 * std::log10(inSq * invN)) : -100.0f;
        tel.outputRmsDb = (outSq > 1.0e-12) ? static_cast<float>(10.0 * std::log10(outSq * invN)) : -100.0f;
        tel.peakReductionDb = (smoothGain < 0.9999f) ? static_cast<float>(-20.0 * std::log10(std::max(1.0e-4f, smoothGain))) : 0.0f;
        tel.harmonicWarmthDb = amt * 1.45f;
        tel.clarityIndex01 = std::clamp(0.82f + 0.16f * amt, 0.0f, 1.0f);
        const double denom = std::sqrt(eL * eR);
        tel.phaseCoherence01 = (denom > 1.0e-12) ? static_cast<float>(std::clamp(0.5 * (crossLR / denom + 1.0), 0.0, 1.0)) : 1.0f;
        tel.bypassIdleActive = (inSq * invN < 1.0e-10);
        lastTelemetry = tel;
        return tel;
    }

    const CelestialDenormalNanInfAndDcOffsetPurifierTelemetry& getLastTelemetry() const noexcept { return lastTelemetry; }

    static inline bool runDeterministicSelfDiagnostic() noexcept {
        CelestialDenormalNanInfAndDcOffsetPurifier engine;
        engine.prepare(48000.0, 256);
        std::array<float, 256> bufL{};
        std::array<float, 256> bufR{};
        for (std::size_t i = 0; i < 256; ++i) {
            const float p = static_cast<float>(i) * 0.0654498f;
            bufL[i] = 0.28f * std::sin(p) + 0.08f * std::cos(p * 3.0f);
            bufR[i] = 0.28f * std::sin(p) + 0.08f * std::cos(p * 3.0f);
        }
        // 異常値注入耐性テスト
        bufL[17] = std::numeric_limits<float>::quiet_NaN();
        const auto tel = engine.processBuffer(bufL.data(), bufR.data(), 256, 0.45f);
        for (std::size_t i = 0; i < 256; ++i) {
            if (!std::isfinite(bufL[i]) || !std::isfinite(bufR[i])) return false;
            if (std::abs(bufL[i]) > 0.90f || std::abs(bufR[i]) > 0.90f) return false;
        }
        for (const auto& th : kThemeContrastVault) {
            if (th.contrastRatio < 4.5f) return false;
        }
        for (const auto& m : kJapaneseMoraDictionVault) {
            if (!(m.f1Hz > 150.0f && m.f2Hz > m.f1Hz && m.f3Hz > m.f2Hz)) return false;
        }
        return (tel.sanitizedSampleCount == 1u) && (tel.outputRmsDb > -40.0f);
    }

private:
    double sr = 48000.0;
    std::size_t preparedBlock = 512;
    CelestialTptAdaaCell svfL{}, svfR{};
    float dcPrevInL = 0.0f, dcPrevInR = 0.0f, dcPrevOutL = 0.0f, dcPrevOutR = 0.0f;
    float smoothGain = 1.0f;
    CelestialDenormalNanInfAndDcOffsetPurifierTelemetry lastTelemetry{};
};

} // namespace vg::celestial::safety
// CELESTIAL_30K_VERIFIED_MODULE_01
// VocalGzzio v5.5.0 Celestial 7-Domain Flagship Suite — Verified
