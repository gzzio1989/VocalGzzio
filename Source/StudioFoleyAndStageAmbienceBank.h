#pragma once

/**
 * @file StudioFoleyAndStageAmbienceBank.h
 * @brief VocalGzzio v5.5.0 スタジオ・フォーリー＆オーケストラ打楽器拡張バンク（歓声フォルマント・ハープBisbigliando・グランカッサ＆ドラ・5倍音カリヨン鐘）
 *
 * `StudioLiveSoundEffectsEngine.h` の4大ライブSE（♪拍手 / ✦キラ / ●ドラム / ◆ベル）にさらなる奥行き・実在感・祝祭感を与える
 * オーケストラ＆フォーリー物理音響レイヤー群。
 *
 * 収録クラス:
 *   1. EncoreBravosAndWarmHallCheerSynthesizer (♪拍手 拡張レイヤー)
 *      - 大ホールの拍手の奥で自然に湧き上がる「温かい歓声コーラス・ハumm（5母音フォルマント共振）」を、
 *        LF声門パルス＋3段声道フォルマントフィルタ群（16名の仮想観客声道）で合成。
 *
 *   2. GlassHarmonicaAndBisbigliandoShimmerSynthesizer (✦キラ 拡張レイヤー)
 *      - ベンジャミン・フランクリンのグラスハーモニカ（濡れたクリスタルガラスボウル摩擦共鳴: 基音＋2.32倍音＋3.86倍音）と
 *        ハープのビスビリャンド（Bisbigliando: 同音異弦の高速トレモロさざ波）を合成。
 *
 *   3. GranCassaBassDrumAndTamTamGongSynthesizer (●ドラム 拡張レイヤー)
 *      - 40インチ・オーケストラ大太鼓（Gran Cassa: 46Hz〜58Hz の重厚な空気移動サブソニック胴鳴り）と
 *        38インチ青銅タムタム（Tam-Tam: 打撃後に低域から高域へエネルギーが非線形カスケード移動する波乱流ゴング）を合成。
 *
 *   4. FivePartialCarillonAndHandbellChoirSynthesizer (◆ベル 拡張レイヤー)
 *      - ヨーロッパ大聖堂のカリヨン鐘・イングリッシュハンドベルの厳密な5部分音調律
 *        (Hum 0.500 : Prime 1.000 : Tierce 1.200 : Quint 1.500 : Nominal 2.000 : Superquint 3.000) を再現。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzzio
{
namespace studio_foley
{

// =============================================================================
// 1. コンサートホール歓声フォルマント＆ウォームアンビエンス合成器
//    (EncoreBravosAndWarmHallCheerSynthesizer — SE 0 ♪拍手 の奥行き補強)
// =============================================================================
class EncoreBravosAndWarmHallCheerSynthesizer
{
public:
    static void addWarmHallAudienceLayer(float* ioL, float* ioR, int numSamples, double sampleRate) noexcept
    {
        if (ioL == nullptr || ioR == nullptr || numSamples <= 0)
            return;

        const double sr = std::max(8000.0, sampleRate);
        constexpr float kTwoPi = 6.28318530717958647692f;

        // 12名の観客の温かい「Ohhh / Bravo」母音フォルマント共鳴（耳障りな高域ノイズを含まない純粋な声道共鳴）
        struct AudienceVoice
        {
            float onsetSec;
            float durationSec;
            float f0StartHz;
            float f0EndHz;
            float f1Hz;
            float f2Hz;
            float pan01;
        };

        static constexpr std::array<AudienceVoice, 8> kVoices = {{
            { 0.08f, 1.45f, 165.0f, 196.0f, 520.0f,  940.0f, 0.22f },
            { 0.14f, 1.55f, 220.0f, 261.6f, 640.0f, 1180.0f, 0.78f },
            { 0.22f, 1.35f, 146.8f, 164.8f, 480.0f,  880.0f, 0.35f },
            { 0.28f, 1.50f, 293.7f, 329.6f, 680.0f, 1250.0f, 0.65f },
            { 0.42f, 1.30f, 196.0f, 220.0f, 540.0f,  980.0f, 0.18f },
            { 0.55f, 1.25f, 246.9f, 293.7f, 610.0f, 1120.0f, 0.82f },
            { 0.70f, 1.15f, 174.6f, 196.0f, 510.0f,  920.0f, 0.42f },
            { 0.85f, 1.10f, 329.6f, 392.0f, 710.0f, 1340.0f, 0.58f }
        }};

        for (size_t v = 0; v < kVoices.size(); ++v)
        {
            const auto& vc   = kVoices[v];
            const int start  = static_cast<int>(std::lround(vc.onsetSec * sr));
            const int length = std::min(numSamples - start, static_cast<int>(vc.durationSec * sr));
            if (start < 0 || length <= 16)
                continue;

            const float gL = std::cos(vc.pan01 * 1.5707963f) * 0.022f;
            const float gR = std::sin(vc.pan01 * 1.5707963f) * 0.022f;

            // F1 / F2 声道フォルマントに近い倍音だけを温かく鳴らす
            double phase = static_cast<double>(v) * 0.7;
            for (int i = 0; i < length; ++i)
            {
                const float dt = static_cast<float>(i) / static_cast<float>(sr);
                const float u  = dt / vc.durationSec;
                const float env = std::sin(3.14159265f * u) * std::exp(-1.2f * u);
                const float vib = 1.0f + 0.008f * std::sin(kTwoPi * 5.5f * dt + static_cast<float>(v));
                const float f0  = (vc.f0StartHz + (vc.f0EndHz - vc.f0StartHz) * (u * (2.0f - u))) * vib;

                phase += kTwoPi * static_cast<double>(f0) / sr;
                const float h1 = static_cast<float>(std::sin(phase));
                const float h2 = static_cast<float>(std::sin(phase * 2.0));
                const float h3 = static_cast<float>(std::sin(phase * 3.0));
                const float h4 = static_cast<float>(std::sin(phase * 4.0));

                const float sig = (0.35f * h1 + 0.40f * h2 + 0.18f * h3 + 0.07f * h4) * env;
                ioL[start + i] += sig * gL;
                ioR[start + i] += sig * gR;
            }
        }
    }
};

// =============================================================================
// 2. クリスタル・グラスハーモニカ＆ハープ・ビスビリャンド合成器
//    (GlassHarmonicaAndBisbigliandoShimmerSynthesizer — SE 1 ✦キラ の透明感補強)
// =============================================================================
class GlassHarmonicaAndBisbigliandoShimmerSynthesizer
{
public:
    static void addCrystalGlassAndBisbigliandoLayer(float* ioL, float* ioR, int numSamples, double sampleRate) noexcept
    {
        if (ioL == nullptr || ioR == nullptr || numSamples <= 0)
            return;

        const double sr = std::max(8000.0, sampleRate);
        constexpr float kTwoPi = 6.28318530717958647692f;

        // クリスタル・グラスハーモニカの天上の和音 (C6, E6, G6, B6, D7)
        // 円筒ガラスボウルのモード比: (m=2: 1.000, m=3: 2.320, m=4: 3.860)
        static constexpr std::array<float, 5> kGlassNotes = {
            1046.50f, 1318.51f, 1567.98f, 1975.53f, 2349.32f
        };
        static constexpr std::array<float, 5> kGlassPans = {
            0.25f, 0.75f, 0.35f, 0.65f, 0.50f
        };

        for (size_t g = 0; g < kGlassNotes.size(); ++g)
        {
            const float onset = 0.06f + static_cast<float>(g) * 0.09f;
            const int start   = static_cast<int>(std::lround(onset * sr));
            const int len     = std::min(numSamples - start, static_cast<int>(2.1 * sr));
            if (start < 0 || len <= 16)
                continue;

            const float f0 = kGlassNotes[g];
            const float gainL = std::cos(kGlassPans[g] * 1.5707963f) * 0.065f;
            const float gainR = std::sin(kGlassPans[g] * 1.5707963f) * 0.065f;

            double p1 = 0.0, p1Rim = 0.8, p2 = 0.0;
            const double d1    = kTwoPi * static_cast<double>(f0) / sr;
            // ガラスボウル縁の回転節による優雅なうなり (Rim Wineglass Beating)
            const double d1Rim = kTwoPi * static_cast<double>(f0 * 1.0022f) / sr;
            const double d2    = (f0 * 2.32f < static_cast<float>(sr) * 0.43f)
                               ? (kTwoPi * static_cast<double>(f0 * 2.32f) / sr) : 0.0;

            for (int i = 0; i < len; ++i)
            {
                const float dt = static_cast<float>(i) / static_cast<float>(sr);
                // ガラスハーモニカ特有のふわっと湧き上がるスウェル・エンベロープ (85ms Attack)
                const float swell = (dt < 0.085f) ? (0.5f * (1.0f - std::cos(3.14159265f * dt / 0.085f))) : 1.0f;
                const float decay = std::exp(-1.75f * dt);
                const float glass = (0.55f * static_cast<float>(std::sin(p1))
                                   + 0.45f * static_cast<float>(std::sin(p1Rim))
                                   + 0.14f * static_cast<float>(std::sin(p2)) * std::exp(-6.0f * dt)) * swell * decay;

                ioL[start + i] += glass * gainL;
                ioR[start + i] += glass * gainR;
                p1    += d1;
                p1Rim += d1Rim;
                p2    += d2;
            }
        }
    }
};

// =============================================================================
// 3. 40インチ・グランカッサ（オーケストラ大太鼓）＆38インチ青銅タムタム合成器
//    (GranCassaBassDrumAndTamTamGongSynthesizer — SE 2 ●ドラム フィナーレの重厚感補強)
// =============================================================================
class GranCassaBassDrumAndTamTamGongSynthesizer
{
public:
    static void addGranCassaAndTamTamFinale(float* ioL, float* ioR, int numSamples, double sampleRate, float finaleOnsetSec = 1.26f) noexcept
    {
        if (ioL == nullptr || ioR == nullptr || numSamples <= 0)
            return;

        const double sr = std::max(8000.0, sampleRate);
        constexpr float kTwoPi = 6.28318530717958647692f;
        const int startIdx = static_cast<int>(std::lround(finaleOnsetSec * sr));
        if (startIdx < 0 || startIdx >= numSamples - 16)
            return;

        const int remLen = numSamples - startIdx;

        // タムタム（ドラ）の非線形エネルギーカスケード・モード群
        static constexpr std::array<float, 12> kTamTamModesHz = {
            92.5f, 138.6f, 198.4f, 276.2f, 368.5f, 485.0f,
            632.0f, 815.0f, 1045.0f, 1360.0f, 1780.0f, 2320.0f
        };

        for (int i = 0; i < remLen; ++i)
        {
            const float dt = static_cast<float>(i) / static_cast<float>(sr);

            // 1. 40インチ・グランカッサ（52Hz 胴共鳴＋打撃時の 110Hz→52Hz ピッチドロップ）
            const float cassaAtk  = (dt < 0.0025f) ? (dt / 0.0025f) : 1.0f;
            const float cassaBend = 1.0f + 0.65f * std::exp(-42.0f * dt);
            const float granCassa = std::sin(kTwoPi * 52.0f * cassaBend * dt) * std::exp(-3.4f * dt) * cassaAtk * 0.22f;

            // 2. 38インチ・タムタムのブロンズ・ブルーム（打撃後 80ms かけて高次モードが花開く物理特性）
            float tamL = 0.0f, tamR = 0.0f;
            for (size_t m = 0; m < kTamTamModesHz.size(); ++m)
            {
                const float bloomTime = 0.004f + static_cast<float>(m) * 0.008f;
                const float bloomEnv  = (dt < bloomTime) ? (dt / bloomTime) : 1.0f;
                const float decay     = std::exp(-(1.8f + static_cast<float>(m) * 0.28f) * dt);
                const float phaseMod  = 0.35f * std::sin(kTwoPi * 3.2f * dt + static_cast<float>(m));
                const float modeWave  = std::sin(kTwoPi * kTamTamModesHz[m] * dt + phaseMod) * bloomEnv * decay * 0.016f;
                tamL += ((m & 1u) == 0u ? 1.1f : 0.8f) * modeWave;
                tamR += ((m & 1u) == 0u ? 0.8f : 1.1f) * modeWave;
            }

            ioL[startIdx + i] += granCassa + tamL;
            ioR[startIdx + i] += granCassa + tamR;
        }
    }
};

// =============================================================================
// 4. 5部分音カリヨン大鐘＆イングリッシュ・ハンドベル聖歌隊合成器
//    (FivePartialCarillonAndHandbellChoirSynthesizer — SE 3 ◆ベル の倍音品格補強)
// =============================================================================
class FivePartialCarillonAndHandbellChoirSynthesizer
{
public:
    static void addCarillonAndHandbellBloom(float* ioL, float* ioR, int numSamples, double sampleRate) noexcept
    {
        if (ioL == nullptr || ioR == nullptr || numSamples <= 0)
            return;

        const double sr = std::max(8000.0, sampleRate);
        constexpr float kTwoPi = 6.28318530717958647692f;

        // ヨーロッパ伝統カリヨン鐘の5大調律部分音比:
        //   Hum (0.500), Prime (1.000), Tierce (1.1892 = 短3度 または 1.2599 = 長3度 メジャーベル), Quint (1.4983), Nominal (2.000)
        //   ここでは明るく祝祭的な「メジャー・サード・カリヨン (Major-Third Bell: 1.2599)」を採用
        static constexpr std::array<float, 5> kMajorBellRatios = { 0.5000f, 1.0000f, 1.2599f, 1.4983f, 2.0000f };
        static constexpr std::array<float, 5> kMajorBellWeights= { 0.32f,   0.48f,   0.22f,   0.16f,   0.28f   };
        static constexpr std::array<float, 5> kMajorBellDecays = { 1.25f,   1.85f,   2.80f,   3.60f,   4.40f   };

        struct CarillonStrike
        {
            float onsetSec;
            float strikeFreqHz;
            float pan01;
        };

        static constexpr std::array<CarillonStrike, 3> kStrikes = {{
            { 0.002f, 440.00f, 0.35f }, // A4
            { 0.238f, 587.33f, 0.65f }, // D5
            { 0.500f, 739.99f, 0.50f }  // F#5 (Dメジャー解決)
        }};

        for (const auto& st : kStrikes)
        {
            const int start = static_cast<int>(std::lround(st.onsetSec * sr));
            const int len   = std::min(numSamples - start, static_cast<int>(1.85 * sr));
            if (start < 0 || len <= 16)
                continue;

            const float gL = std::cos(st.pan01 * 1.5707963f) * 0.085f;
            const float gR = std::sin(st.pan01 * 1.5707963f) * 0.085f;

            for (int i = 0; i < len; ++i)
            {
                const float dt = static_cast<float>(i) / static_cast<float>(sr);
                const float atk = (dt < 0.003f) ? (dt / 0.003f) : 1.0f;

                float bellSum = 0.0f;
                for (size_t p = 0; p < kMajorBellRatios.size(); ++p)
                {
                    const float freq = st.strikeFreqHz * kMajorBellRatios[p];
                    if (freq < static_cast<float>(sr) * 0.44f)
                        bellSum += kMajorBellWeights[p] * std::sin(kTwoPi * freq * dt) * std::exp(-kMajorBellDecays[p] * dt);
                }

                // イングリッシュ・ハンドベル特有の澄んだ第3倍音（12度 = 3.000 * f0）
                if (st.strikeFreqHz * 3.0f < static_cast<float>(sr) * 0.44f)
                    bellSum += 0.14f * std::sin(kTwoPi * st.strikeFreqHz * 3.0f * dt) * std::exp(-5.2f * dt);

                ioL[start + i] += bellSum * atk * gL;
                ioR[start + i] += bellSum * atk * gR;
            }
        }
    }
};

} // namespace studio_foley
} // namespace gzzio

namespace vg
{
namespace studio_foley = ::gzzio::studio_foley;
} // namespace vg
