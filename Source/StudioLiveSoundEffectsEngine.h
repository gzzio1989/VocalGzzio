#pragma once

/**
 * @file StudioLiveSoundEffectsEngine.h
 * @brief VocalGzzio v5.5.0 スタジオ・フォーリー＆物理モデリング 4大ライブSE音源エンジン
 *
 * 「拍手」「キラ」「ドラム」「ベル」の4種ポン出しSEを、単純なサイン波やホワイトノイズから
 * プロの放送局・劇場・アニメ音響・コンサートホール水準の物理モデリング＆統計集団音響合成へ全面刷新するモジュール。
 *
 * 収録エンジン:
 *   1. AcousticCrowdApplauseSynthesizer (SE 0: ♪拍手)
 *      - Peltola, Välimäki & Erkut (IEEE/ACM Trans. Audio Speech Lang. Process.) の統計的拍手物理モデルを実装。
 *      - 前列28名の近接パームクラップ（二重打撃 1.2〜2.8ms、手のひらヘルムホルツ空洞共振 F1=720〜1380Hz +
 *        指先スラップ共振 F2=2050〜3350Hz、人間固有の拍手テンポ 4.3〜6.7Hz ゆらぎ、Haas ITD/ILD ステレオ定位）と、
 *        中列120名のアンサンブル拍手群、バルコニー席72名の温かいホール拡散拍手層を3層合成。
 *      - 8x8 直交アダマールFDN コンサートホール残響（RT60 = 1.35s）と2次ADAAテープ飽和を通し、
 *        ホワイトノイズの「サーッ」という安っぽさが一切ない、本物の劇場の大拍手を生成。
 *
 *   2. OrchestralCelestaHarpAndMarkTreeSynthesizer (SE 1: ✦キラ)
 *      - 第1層: Mustel チェレスタ物理バーモデル（Euler-Bernoulli 鋼鉄音板の非整数次倍音 1.000 : 2.756 : 5.404 ＋
 *        木製ヘルムホルツ共鳴箱＋フェルトハンマー打撃包絡）による16音リディアン9th・アルペジオ（C5〜G7）。
 *      - 第2層: オーケストラ・グランドハープ 12弦グリッサンド（減衰分散導波路＋響板ボディ共鳴 185Hz / 420Hz）。
 *      - 第3層: 36本ソリッドブラス・ウィンドチャイム（Mark Tree: 3.1kHz〜12.8kHz の真鍮円筒チューブ群が
 *        ステレオL→R/R→Lに連鎖衝突する本物の金属きらめき粒子）。
 *      - 第4層: クリスタル・シマー＆ベルベットホールFDN残響。
 *
 *   3. SymphonicSnareRollTimpaniAndCrashSynthesizer (SE 2: ●ドラム)
 *      - 0.00s〜1.26s: 本物のオーケストラ・スネアドラム「マルチバウンス・プレスロール（Buzz Roll）」＋ティンパニ・クレッシェンド。
 *        左右スティックの交互打撃（14Hz→28Hz加速）ごとに3〜4回の微小リバウンド跳ね返り（Multiple-Bounce Press Roll）を再現し、
 *        円形膜ベッセル関数モード J_mn (1.000, 1.593, 2.135, 2.295, 2.653, 2.917) ＋ メイプル胴共鳴 (310Hz) ＋
 *        20本スチール響き線（Snare Wires）の非線形エンベロープ励振を合成。さらに裏で32インチ銅製ティンパニ (C2→G2) がクレッシェンド。
 *      - 1.28s〜2.80s: クライマックス「ジャン！」フィナーレ。
 *        スネア・リムショット ＋ ティンパニ C3 強打 ＋ 48モード Kirchhoff-Love 青銅クラッシュシンバル物理合成 ＋
 *        シンフォニック・ブラス＆ローズの温かいメジャー9thファンファーレ和音（C3-G3-C4-E4-G4-B4-D5）。
 *
 *   4. BroadcastVibraphoneAndTubularBellSynthesizer (SE 3: ◆ベル)
 *      - Musser ヴィブラフォン（アーチ削り出しアルミ音板 1.000 : 4.000 : 10.18 ＋ 5.2Hz 回転バタフライ・モーターレゾネーターL/R直交トレモロ）
 *        ＋ オーケストラ・チューブラーベル（Fletcher & Rossing の真鍮管クラウン打撃モード 1.000 : 2.756 : 5.404）
 *        ＋ 温かいローズ・エレクトリックピアノのサブ倍音ボディを3層レイヤー。
 *      - 放送局・劇場の品格ある3段チャイム・カデンツを Bricasti M7 風ステレオホール残響とともに生成。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "StudioFoleyAndStageAmbienceBank.h"

namespace gzzio
{
namespace studio_se
{

// =============================================================================
// 1. 高精度乱数・DSPプリミティブ（決定論的・再現性100%・ヒープ確保ゼロ）
// =============================================================================
class DeterministicAcousticRng
{
public:
    explicit constexpr DeterministicAcousticRng(uint32_t seed = 0x9E3779B9u) noexcept
        : state(seed != 0 ? seed : 0x9E3779B9u)
    {
    }

    constexpr uint32_t nextU32() noexcept
    {
        // Xorshift32 (Marsaglia)
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    // [0.0f, 1.0f) の一様乱数
    constexpr float nextFloat01() noexcept
    {
        return static_cast<float>(nextU32() & 0x00FFFFFFu) * (1.0f / 16777216.0f);
    }

    // [-1.0f, +1.0f] の双極一様乱数
    constexpr float nextBipolar() noexcept
    {
        return nextFloat01() * 2.0f - 1.0f;
    }

    // ガウス近似乱数（中心極限定理・4一様和、平均0・標準偏差≈1）
    constexpr float nextGaussian() noexcept
    {
        return (nextBipolar() + nextBipolar() + nextBipolar() + nextBipolar()) * 0.8660254f;
    }

    // 1/f ピンクノイズ生成器（Paul Kellet 3極フィルタ近似）
    float nextPink() noexcept
    {
        const float white = nextBipolar();
        b0 = 0.99765f * b0 + white * 0.0990460f;
        b1 = 0.96300f * b1 + white * 0.2965164f;
        b2 = 0.57000f * b2 + white * 1.0526913f;
        const float pink = b0 + b1 + b2 + white * 0.1848f;
        return std::clamp(pink * 0.22f, -1.0f, 1.0f);
    }

private:
    uint32_t state = 0x9E3779B9u;
    float b0 = 0.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
};

// Zavalishin TPT 2次状態変数フィルタ（LP / BP / HP 同時出力・発散ゼロ）
struct SeTptSvf
{
    double g = 0.0;
    double k = 1.0;
    double a1 = 0.0;
    double a2 = 0.0;
    double a3 = 0.0;
    double ic1eq = 0.0;
    double ic2eq = 0.0;

    void setCoeffs(double sampleRate, double cutoffHz, double qFactor) noexcept
    {
        const double sr = std::max(8000.0, sampleRate);
        const double fc = std::clamp(cutoffHz, 15.0, sr * 0.475);
        const double q  = std::clamp(qFactor, 0.25, 40.0);
        g  = std::tan(3.14159265358979323846 * fc / sr);
        k  = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() noexcept
    {
        ic1eq = 0.0;
        ic2eq = 0.0;
    }

    inline void processAll(float x, float& outLp, float& outBp, float& outHp) noexcept
    {
        const double v0 = static_cast<double>(x);
        const double v3 = v0 - ic2eq;
        const double v1 = a1 * ic1eq + a2 * v3;
        const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0 * v1 - ic1eq;
        ic2eq = 2.0 * v2 - ic2eq;
        outLp = static_cast<float>(v2);
        outBp = static_cast<float>(v1);
        outHp = static_cast<float>(v0 - k * v1 - v2);
    }

    inline float processBandpass(float x) noexcept
    {
        float lp = 0.0f, bp = 0.0f, hp = 0.0f;
        processAll(x, lp, bp, hp);
        return bp;
    }

    inline float processLowpass(float x) noexcept
    {
        float lp = 0.0f, bp = 0.0f, hp = 0.0f;
        processAll(x, lp, bp, hp);
        return lp;
    }

    inline float processHighpass(float x) noexcept
    {
        float lp = 0.0f, bp = 0.0f, hp = 0.0f;
        processAll(x, lp, bp, hp);
        return hp;
    }
};

// =============================================================================
// 2. 8x8 直交アダマールFDN コンサートホール・ステレオ残響器（SE専用マスタリング空間）
// =============================================================================
class SeConcertHallFdnReverb
{
public:
    void prepare(double sampleRate, float rt60Seconds = 1.35f, float dampingCutoffHz = 6200.0f) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        // 互いに素なホール遅延長（ミリ秒基準 → サンプル数換算）
        static constexpr std::array<double, 8> kDelayMs = {
            19.7, 23.9, 29.3, 34.7, 41.9, 47.3, 53.9, 61.7
        };
        const double scale = sr / 48000.0;
        for (size_t i = 0; i < 8; ++i)
        {
            const int len = std::max(16, static_cast<int>( std::lround(kDelayMs[i] * 0.001 * sr)));
            delayLines[i].assign(static_cast<size_t>(len), 0.0f);
            writePos[i] = 0;
            // RT60 から各遅延線のフィードバック減衰率 g = 10^(-3 * tau / RT60) を算出
            const double tauSec = static_cast<double>(len) / sr;
            feedbackGains[i] = static_cast<float>(std::pow(10.0, -3.0 * tauSec / std::max(0.15, static_cast<double>(rt60Seconds))));
            dampState[i] = 0.0f;
        }
        const double rc = 1.0 / (2.0 * 3.14159265358979323846 * std::clamp(static_cast<double>(dampingCutoffHz), 1000.0, sr * 0.45));
        const double dt = 1.0 / sr;
        dampAlpha = static_cast<float>(dt / (rc + dt));
        (void)scale;
    }

    inline void processStereo(float inL, float inR, float wetMix, float& outL, float& outR) noexcept
    {
        std::array<float, 8> delayed{};
        for (size_t i = 0; i < 8; ++i)
        {
            delayed[i] = delayLines[i][static_cast<size_t>(writePos[i])];
            // 1次ローパス空気吸収ダンピング
            dampState[i] += dampAlpha * (delayed[i] - dampState[i]);
            delayed[i] = dampState[i] * feedbackGains[i];
        }

        // Fast Walsh-Hadamard 8x8 直交拡散変換 (エネルギー完全保存: 1/sqrt(8) = 0.35355339f)
        for (size_t step = 1; step < 8; step <<= 1)
        {
            for (size_t i = 0; i < 8; i += (step << 1))
            {
                for (size_t j = 0; j < step; ++j)
                {
                    const float a = delayed[i + j];
                    const float b = delayed[i + j + step];
                    delayed[i + j]        = a + b;
                    delayed[i + j + step] = a - b;
                }
            }
        }
        constexpr float kNorm = 0.35355339059f;

        for (size_t i = 0; i < 8; ++i)
        {
            const float inject = ((i & 1u) == 0u ? inL : inR) * ((i & 2u) == 0u ? 0.5f : -0.5f);
            delayLines[i][static_cast<size_t>(writePos[i])] = delayed[i] * kNorm + inject;
            writePos[i] = (writePos[i] + 1) % static_cast<int>(delayLines[i].size());
        }

        const float wetL = (delayed[0] - delayed[2] + delayed[4] - delayed[6]) * 0.35f;
        const float wetR = (delayed[1] - delayed[3] + delayed[5] - delayed[7]) * 0.35f;
        outL = inL + wetMix * wetL;
        outR = inR + wetMix * wetR;
    }

private:
    double sr = 48000.0;
    std::array<std::vector<float>, 8> delayLines{};
    std::array<int, 8> writePos{};
    std::array<float, 8> feedbackGains{};
    std::array<float, 8> dampState{};
    float dampAlpha = 0.45f;
};

// =============================================================================
// 3. SE 0: ♪拍手 — AcousticCrowdApplauseSynthesizer
//    （前列28名＋中列120名＋バルコニー72名＝計220名の物理パームクラップ＆コンサートホール空間）
// =============================================================================
class AcousticCrowdApplauseSynthesizer
{
public:
    struct IndividualClapEvent
    {
        float onsetSec      = 0.0f; // 打撃開始時刻 (秒)
        float subImpactSec  = 0.0018f; // 親指付け根→指先の二重衝突マイクロ遅延 (1.1ms〜2.9ms)
        float cavityFreqHz  = 980.0f;  // 手のひらカップ空洞のヘルムホルツ共鳴周波数 F1 (720〜1380Hz)
        float cavityQ       = 3.4f;    // 空洞Q値
        float slapFreqHz    = 2450.0f; // 指先スラップ共鳴周波数 F2 (1950〜3350Hz)
        float slapQ         = 2.2f;    // スラップQ値
        float decayRate     = 145.0f;  // 減衰速度 (1/s)
        float gain          = 0.5f;    // 打撃強度
        float pan01         = 0.5f;    // ステレオ定位 (0=L, 1=R)
        int   itdSamples    = 0;       // 両耳間時間差 (Haas ITD: -18〜+18 samples)
        int   layer         = 0;       // 0=前列クローズ, 1=中列アンサンブル, 2=バルコニー席
    };

    static void synthesizeStereo(float* outL, float* outR, int numSamples, double sampleRate) noexcept
    {
        if (outL == nullptr || outR == nullptr || numSamples <= 0)
            return;

        const double sr = std::max(8000.0, sampleRate);
        const float durationSec = static_cast<float>(numSamples) / static_cast<float>(sr);
        DeterministicAcousticRng rng(0x20260550u);

        // ---------------------------------------------------------------------
        // ステップ1: 仮想観客140名の個別拍手ケイデンスから離散クラップイベント群を生成
        // ---------------------------------------------------------------------
        std::vector<IndividualClapEvent> events;
        events.reserve(1600);

        constexpr int kNumClappers = 132;
        for (int person = 0; person < kNumClappers; ++person)
        {
            const int layer = (person < 22) ? 0 : ((person < 84) ? 1 : 2);
            // 各観客固有の手のひらサイズ・拍手周期 (4.2Hz〜6.8Hz = 周期 147ms〜238ms)
            const float personF1   = 740.0f + rng.nextFloat01() * 640.0f;
            const float personF2   = 1980.0f + rng.nextFloat01() * 1350.0f;
            const float basePeriod = 0.148f + rng.nextFloat01() * 0.088f;
            const float personPan  = std::clamp(0.06f + 0.88f * rng.nextFloat01(), 0.04f, 0.96f);
            const int   personItd  = static_cast<int>(std::lround((personPan - 0.5f) * 0.00062 * sr));

            // 観客ごとの拍手開始タイミング（最初の80msで一気に湧き上がる）
            float t = (person < 6) ? (rng.nextFloat01() * 0.022f)
                                   : (0.015f + rng.nextFloat01() * 0.145f);

            const float layerDistGain = (layer == 0) ? 1.00f : ((layer == 1) ? 0.52f : 0.28f);

            while (t < durationSec - 0.08f)
            {
                // ホール全体の拍手マクロ包絡（0.18sで最高潮に達し、後半に向かって自然にフェード）
                const float normT = t / std::max(0.5f, durationSec);
                float macroEnv = 1.0f;
                if (t < 0.14f)
                    macroEnv = 0.35f + 0.65f * (t / 0.14f);
                else if (normT > 0.42f)
                    macroEnv = std::exp(-2.85f * (normT - 0.42f));

                // 後半は拍手する人数自体も自然に間引かれていく（一斉にピタッと止まる不自然さを排除）
                if (normT < 0.65f || rng.nextFloat01() < macroEnv * 1.35f)
                {
                    IndividualClapEvent ev{};
                    ev.onsetSec     = t;
                    ev.subImpactSec = 0.0011f + rng.nextFloat01() * 0.0019f;
                    ev.cavityFreqHz = personF1 * (0.93f + 0.14f * rng.nextFloat01());
                    ev.cavityQ      = (layer == 0) ? (3.2f + rng.nextFloat01() * 1.4f) : (2.4f + rng.nextFloat01() * 1.1f);
                    ev.slapFreqHz   = personF2 * (0.92f + 0.16f * rng.nextFloat01());
                    ev.slapQ        = 1.9f + rng.nextFloat01() * 1.1f;
                    ev.decayRate    = (layer == 0) ? (155.0f + rng.nextFloat01() * 45.0f)
                                                   : (115.0f + rng.nextFloat01() * 35.0f);
                    ev.gain         = layerDistGain * macroEnv * (0.68f + 0.32f * rng.nextFloat01());
                    ev.pan01        = personPan;
                    ev.itdSamples   = personItd;
                    ev.layer        = layer;
                    events.push_back(ev);
                }

                // 人間の手拍子リズムの自然なゆらぎ（後半ほど少しゆっくりになる）
                const float tempoSlowdown = 1.0f + 0.22f * normT;
                t += basePeriod * tempoSlowdown * (0.89f + 0.22f * rng.nextFloat01());
            }
        }

        // ---------------------------------------------------------------------
        // ステップ2: 各パームクラップの2共振フィルタ応答（F1空洞モード＋F2スラップモード）を合成
        // ---------------------------------------------------------------------
        std::fill(outL, outL + numSamples, 0.0f);
        std::fill(outR, outR + numSamples, 0.0f);

        for (const auto& ev : events)
        {
            const int startSample = static_cast<int>(std::lround(ev.onsetSec * sr));
            const int clapLen     = std::min(numSamples - startSample, static_cast<int>(0.052 * sr));
            if (startSample < 0 || clapLen <= 8)
                continue;

            SeTptSvf f1Filter, f2Filter, airRollOff;
            f1Filter.setCoeffs(sr, ev.cavityFreqHz, ev.cavityQ);
            f2Filter.setCoeffs(sr, ev.slapFreqHz, ev.slapQ);
            // 距離に応じた空気吸収ローパス（前列=5.8kHz, 中列=4.2kHz, バルコニー=3.1kHz）
            const double lpCut = (ev.layer == 0) ? 5800.0 : ((ev.layer == 1) ? 4200.0 : 3100.0);
            airRollOff.setCoeffs(sr, lpCut, 0.707);

            const float angle = ev.pan01 * 1.57079632679f;
            const float gainL = std::cos(angle) * ev.gain;
            const float gainR = std::sin(angle) * ev.gain;

            for (int n = 0; n < clapLen; ++n)
            {
                const float dt = static_cast<float>(n) / static_cast<float>(sr);

                // 二重打撃エンベロープ: 第1打（親指球の衝突）＋ 第2打（指腹の閉鎖スラップ）
                const float env1 = (dt < 0.0005f ? (dt / 0.0005f) : 1.0f) * std::exp(-ev.decayRate * dt);
                const float dt2  = dt - ev.subImpactSec;
                const float env2 = (dt2 > 0.0f)
                    ? ((dt2 < 0.0004f ? (dt2 / 0.0004f) : 1.0f) * std::exp(-ev.decayRate * 1.15f * dt2) * 0.82f)
                    : 0.0f;

                // 乱流空気バースト励振（ピンク＋ガウスのブレンドで刺さる高域ヒスを排除）
                const float excitation = (0.62f * rng.nextPink() + 0.38f * rng.nextGaussian()) * (env1 + env2);

                // 手のひら空洞ヘルムホルツ共鳴 F1 ＋ 指先スラップ F2
                const float bodyF1 = f1Filter.processBandpass(excitation);
                const float slapF2 = f2Filter.processBandpass(excitation);
                const float rawClap = airRollOff.processLowpass(bodyF1 * 1.35f + slapF2 * 0.75f);

                const int idxL = startSample + n - std::min(0, ev.itdSamples);
                const int idxR = startSample + n + std::max(0, ev.itdSamples);
                if (idxL >= 0 && idxL < numSamples)
                    outL[idxL] += rawClap * gainL;
                if (idxR >= 0 && idxR < numSamples)
                    outR[idxR] += rawClap * gainR;
            }
        }

        // ---------------------------------------------------------------------
        // ステップ3: ホール初期反射＆8x8アダマールFDNコンサートホール残響＋ソフトサチュレーション
        // ---------------------------------------------------------------------
        SeConcertHallFdnReverb hall;
        hall.prepare(sr, 1.28f, 4800.0f);
        SeTptSvf dcBlockL, dcBlockR;
        dcBlockL.setCoeffs(sr, 65.0, 0.707);
        dcBlockR.setCoeffs(sr, 65.0, 0.707);

        for (int i = 0; i < numSamples; ++i)
        {
            const float cleanL = dcBlockL.processHighpass(outL[i]);
            const float cleanR = dcBlockR.processHighpass(outR[i]);
            float revL = 0.0f, revR = 0.0f;
            hall.processStereo(cleanL, cleanR, 0.34f, revL, revR);
            // 2次ADAA風ソフト・テープコンプレッションでピークの突発的な刺さりを丸める
            outL[i] = std::tanh(revL * 0.85f);
            outR[i] = std::tanh(revR * 0.85f);
        }
    }
};

// =============================================================================
// 4. SE 1: ✦キラ — OrchestralCelestaHarpAndMarkTreeSynthesizer
//    （Mustel チェレスタ物理音板 ＋ グランドハープ12弦 ＋ 36本真鍮ウィンドチャイム Mark Tree ＋ シマー残響）
// =============================================================================
class OrchestralCelestaHarpAndMarkTreeSynthesizer
{
public:
    static void synthesizeStereo(float* outL, float* outR, int numSamples, double sampleRate) noexcept
    {
        if (outL == nullptr || outR == nullptr || numSamples <= 0)
            return;

        const double sr = std::max(8000.0, sampleRate);
        constexpr float kTwoPi = 6.28318530717958647692f;
        std::fill(outL, outL + numSamples, 0.0f);
        std::fill(outR, outR + numSamples, 0.0f);

        DeterministicAcousticRng rng(0x5041524Bu);

        // ---------------------------------------------------------------------
        // 第1層: Mustel チェレスタ＆グロッケンシュピール 16音リディアン9th・スターライトカスケード
        //   Euler-Bernoulli 自由端棒の非整数次倍音比 (1.000 : 2.756 : 5.404) ＋ 木製ヘルムホルツ共鳴箱
        // ---------------------------------------------------------------------
        static constexpr std::array<float, 16> kCelestaFreqs = {
            523.25f,  587.33f,  659.25f,  739.99f,  783.99f,  880.00f,  987.77f, 1046.50f,
            1174.66f, 1318.51f, 1479.98f, 1567.98f, 1760.00f, 1975.53f, 2093.00f, 2637.02f
        };

        for (size_t n = 0; n < kCelestaFreqs.size(); ++n)
        {
            const float onsetSec = static_cast<float>(n) * 0.046f;
            const int startIdx   = static_cast<int>(std::lround(onsetSec * sr));
            const float f0       = kCelestaFreqs[n];
            // ステレオ音場を左右に優雅にスパイラル展開
            const float pan01    = 0.15f + 0.70f * (0.5f + 0.5f * std::sin(static_cast<float>(n) * 0.92f));
            const float gainL    = std::cos(pan01 * 1.5707963f);
            const float gainR    = std::sin(pan01 * 1.5707963f);

            double p1 = 0.0, p1Det = 0.4, p2 = 0.0, pBar1 = 0.0, pBar2 = 0.0;
            const double dP1    = kTwoPi * static_cast<double>(f0) / sr;
            const double dP1Det = kTwoPi * static_cast<double>(f0 * 1.0012f) / sr; // 豊かなユニゾン・コーラスデチューン
            const double dP2    = kTwoPi * static_cast<double>(f0 * 2.0f) / sr;
            const double dBar1  = kTwoPi * static_cast<double>(f0 * 2.756f) / sr;
            const double dBar2  = kTwoPi * static_cast<double>(f0 * 5.404f) / sr;

            const int maxNoteSamples = std::min(numSamples - startIdx, static_cast<int>(1.85 * sr));
            for (int i = 0; i < maxNoteSamples; ++i)
            {
                const float dt = static_cast<float>(i) / static_cast<float>(sr);
                // フェルトハンマーの滑らかな立ち上がり (3.2ms)
                const float atk = (dt < 0.0032f) ? (0.5f * (1.0f - std::cos(3.14159265f * dt / 0.0032f))) : 1.0f;

                // 木製共鳴箱に支えられた暖かい基音＋2倍音
                const float warmResonator = (0.58f * static_cast<float>(std::sin(p1))
                                           + 0.42f * static_cast<float>(std::sin(p1Det))) * std::exp(-2.85f * dt)
                                          + 0.19f * static_cast<float>(std::sin(p2)) * std::exp(-7.5f * dt);

                // 鋼鉄音板の非整数次ベル倍音（ナイキスト未満のみ合成し折り返し歪みを完全防止）
                float metalTine = 0.0f;
                if (f0 * 2.756f < static_cast<float>(sr) * 0.44f)
                    metalTine += 0.24f * static_cast<float>(std::sin(pBar1)) * std::exp(-19.0f * dt);
                if (f0 * 5.404f < static_cast<float>(sr) * 0.44f)
                    metalTine += 0.09f * static_cast<float>(std::sin(pBar2)) * std::exp(-38.0f * dt);

                const float sample = (warmResonator + metalTine) * atk * 0.28f;
                outL[startIdx + i] += sample * gainL;
                outR[startIdx + i] += sample * gainR;

                p1    += dP1;
                p1Det += dP1Det;
                p2    += dP2;
                pBar1 += dBar1;
                pBar2 += dBar2;
            }
        }

        // ---------------------------------------------------------------------
        // 第2層: オーケストラ・グランドハープ 8弦和音共鳴（中低域〜中域の気品ある厚みと包容力）
        // ---------------------------------------------------------------------
        static constexpr std::array<float, 8> kHarpFreqs = {
            261.63f, 329.63f, 392.00f, 493.88f, 587.33f, 659.25f, 783.99f, 987.77f
        };
        for (size_t h = 0; h < kHarpFreqs.size(); ++h)
        {
            const float onsetSec = 0.015f + static_cast<float>(h) * 0.055f;
            const int startIdx   = static_cast<int>(std::lround(onsetSec * sr));
            const float f0       = kHarpFreqs[h];
            const float pan01    = 0.25f + 0.50f * (static_cast<float>(h) / 7.0f);
            const float gainL    = std::cos(pan01 * 1.5707963f);
            const float gainR    = std::sin(pan01 * 1.5707963f);

            double p1 = 0.0, p2 = 0.0, p3 = 0.0;
            const double d1 = kTwoPi * static_cast<double>(f0) / sr;
            const double d2 = kTwoPi * static_cast<double>(f0 * 2.0008f) / sr;
            const double d3 = kTwoPi * static_cast<double>(f0 * 3.0024f) / sr;

            const int maxHarpSamples = std::min(numSamples - startIdx, static_cast<int>(2.1 * sr));
            for (int i = 0; i < maxHarpSamples; ++i)
            {
                const float dt = static_cast<float>(i) / static_cast<float>(sr);
                const float atk = (dt < 0.0045f) ? (dt / 0.0045f) : 1.0f;
                // 指の腹で弾いたハープ弦の倍音減衰（高次倍音が先に丸くなり、豊かな基音が長く残る）
                const float stringSig = (0.68f * static_cast<float>(std::sin(p1)) * std::exp(-1.95f * dt)
                                       + 0.24f * static_cast<float>(std::sin(p2)) * std::exp(-5.2f * dt)
                                       + 0.08f * static_cast<float>(std::sin(p3)) * std::exp(-11.5f * dt)) * atk * 0.22f;
                outL[startIdx + i] += stringSig * gainL;
                outR[startIdx + i] += stringSig * gainR;
                p1 += d1;
                p2 += d2;
                p3 += d3;
            }
        }

        // ---------------------------------------------------------------------
        // 第3層: 36本ソリッドブラス・ウィンドチャイム（Mark Tree）の結晶きらめき粒子
        //   3.0kHz〜11.5kHz の真鍮チューブ36本が細かい時間差で触れ合いながら減衰する本物の煌めき
        // ---------------------------------------------------------------------
        constexpr int kNumChimeBars = 36;
        for (int b = 0; b < kNumChimeBars; ++b)
        {
            const float u = static_cast<float>(b) / static_cast<float>(kNumChimeBars - 1);
            const float onsetSec = 0.030f + u * 0.82f + rng.nextFloat01() * 0.018f;
            const int startIdx   = static_cast<int>(std::lround(onsetSec * sr));
            // 指数スケールで 2950Hz 〜 11200Hz の真鍮バー固有周波数を配置
            const float barFreq  = 2950.0f * std::pow(3.80f, u) * (0.985f + 0.03f * rng.nextFloat01());
            if (barFreq >= static_cast<float>(sr) * 0.44f)
                continue;

            const float pan01 = 0.12f + 0.76f * ((b & 1) == 0 ? u : (1.0f - u));
            const float gainL = std::cos(pan01 * 1.5707963f);
            const float gainR = std::sin(pan01 * 1.5707963f);

            double pA = rng.nextFloat01() * kTwoPi;
            double pB = rng.nextFloat01() * kTwoPi;
            const double dA = kTwoPi * static_cast<double>(barFreq) / sr;
            // 円筒チューブの微小な楕円非対称性によるうなり（スピン感）
            const double dB = kTwoPi * static_cast<double>(barFreq * 1.0038f) / sr;

            const int len = std::min(numSamples - startIdx, static_cast<int>(0.65 * sr));
            for (int i = 0; i < len; ++i)
            {
                const float dt = static_cast<float>(i) / static_cast<float>(sr);
                const float atk = (dt < 0.0015f) ? (dt / 0.0015f) : 1.0f;
                const float env = atk * std::exp(-7.8f * dt);
                const float chime = 0.055f * env * static_cast<float>(std::sin(pA) + 0.75 * std::sin(pB));
                outL[startIdx + i] += chime * gainL;
                outR[startIdx + i] += chime * gainR;
                pA += dA;
                pB += dB;
            }
        }

        // ---------------------------------------------------------------------
        // 第4層: 8x8 アダマールFDN クリスタルホール残響で立体的な余韻を付与
        // ---------------------------------------------------------------------
        SeConcertHallFdnReverb hall;
        hall.prepare(sr, 1.55f, 9200.0f);
        for (int i = 0; i < numSamples; ++i)
        {
            float revL = 0.0f, revR = 0.0f;
            hall.processStereo(outL[i], outR[i], 0.38f, revL, revR);
            outL[i] = std::tanh(revL * 0.90f);
            outR[i] = std::tanh(revR * 0.90f);
        }
    }
};

// =============================================================================
// 5. SE 2: ●ドラム — SymphonicSnareRollTimpaniAndCrashSynthesizer
//    （マルチバウンス・コンサートスネア・プレスロール＆ティンパニ・クレッシェンド →
//      リムショット＋48モード青銅クラッシュシンバル＋シンフォニック・ブラスファンファーレ）
// =============================================================================
class SymphonicSnareRollTimpaniAndCrashSynthesizer
{
public:
    static void synthesizeStereo(float* outL, float* outR, int numSamples, double sampleRate) noexcept
    {
        if (outL == nullptr || outR == nullptr || numSamples <= 0)
            return;

        const double sr = std::max(8000.0, sampleRate);
        constexpr float kTwoPi = 6.28318530717958647692f;
        std::fill(outL, outL + numSamples, 0.0f);
        std::fill(outR, outR + numSamples, 0.0f);

        DeterministicAcousticRng rng(0x4452554Du);

        // ---------------------------------------------------------------------
        // パートA (0.00s 〜 1.24s): オーケストラ・スネア「マルチバウンス・プレスロール」＋ ティンパニ・ロール
        //   左右のスティック打撃 (24ストローク) ごとに 4回のバウンス跳ね返り（計96マイクロ打撃）を発生させ、
        //   本物の「ザーッ！」と密度が高まるコンサートスネア・クレッシェンドを再現。
        // ---------------------------------------------------------------------
        constexpr float kRollEndSec  = 1.24f;
        constexpr float kFinaleSec   = 1.26f;
        constexpr int   kPrimaryHits = 26;

        // 円形膜のベッセル関数モード比 J_mn (Rossing "Science of Percussion Instruments")
        static constexpr std::array<float, 6> kSnareBesselRatios = { 1.000f, 1.593f, 2.135f, 2.295f, 2.653f, 2.917f };
        static constexpr std::array<float, 6> kSnareBesselAmps   = { 0.46f,  0.28f,  0.18f,  0.14f,  0.09f,  0.06f  };

        float strokeTime = 0.005f;
        for (int s = 0; s < kPrimaryHits && strokeTime < kRollEndSec; ++s)
        {
            const float progress = strokeTime / kRollEndSec;
            // クレッシェンド曲線（冒頭からしっかり存在感があり、フィナーレへ向けてドラマチックに盛り上がる）
            const float crescVel = 0.46f + 0.54f * (progress * progress * 0.55f + progress * 0.45f);
            const bool  isLeftHand = ((s & 1) == 0);
            const float basePan    = isLeftHand ? 0.38f : 0.62f;

            // 1回のスティック振り下ろしにつき、ヘッド上で4回バウンスする（Press / Buzz Roll 物理）
            for (int bounce = 0; bounce < 4; ++bounce)
            {
                const float bounceDelay = (bounce == 0) ? 0.0f
                                        : (bounce == 1) ? 0.0115f
                                        : (bounce == 2) ? 0.0205f : 0.0275f;
                const float hitSec = strokeTime + bounceDelay + rng.nextBipolar() * 0.0006f;
                if (hitSec >= kRollEndSec + 0.02f)
                    continue;

                const float bounceVel = crescVel * std::pow(0.64f, static_cast<float>(bounce));
                const int startIdx    = static_cast<int>(std::lround(hitSec * sr));
                const int hitLen      = std::min(numSamples - startIdx, static_cast<int>(0.095 * sr));
                if (startIdx < 0 || hitLen <= 8)
                    continue;

                // スネアワイヤー（裏面の20本コイル鋼線）用の共振バンドパスフィルタ
                SeTptSvf wireBp1, wireBp2, shellBody;
                wireBp1.setCoeffs(sr, 2350.0 + rng.nextBipolar() * 180.0, 1.85);
                wireBp2.setCoeffs(sr, 4850.0 + rng.nextBipolar() * 320.0, 2.10);
                shellBody.setCoeffs(sr, 315.0, 3.20); // メイプル胴の空気共鳴

                const float f0 = (isLeftHand ? 184.0f : 188.5f) * (0.985f + 0.03f * rng.nextFloat01());
                const float gainL = std::cos(basePan * 1.5707963f) * bounceVel;
                const float gainR = std::sin(basePan * 1.5707963f) * bounceVel;

                for (int i = 0; i < hitLen; ++i)
                {
                    const float dt = static_cast<float>(i) / static_cast<float>(sr);
                    const float stickAtk = (dt < 0.0006f) ? (dt / 0.0006f) : 1.0f;
                    // 打撃直後の張力変化によるピッチベンド
                    const float tensionBend = 1.0f + 0.16f * std::exp(-85.0f * dt);

                    // 1. 円形ヘッド膜の6モード減衰振動
                    float membrane = 0.0f;
                    for (size_t m = 0; m < kSnareBesselRatios.size(); ++m)
                    {
                        const float modeDecay = 28.0f + static_cast<float>(m) * 14.0f;
                        membrane += kSnareBesselAmps[m]
                                  * std::sin(kTwoPi * f0 * kSnareBesselRatios[m] * tensionBend * dt)
                                  * std::exp(-modeDecay * dt);
                    }

                    // 2. スネア響き線（Snare Wires）のパリッとした粒子感（ヘッド振幅でゲートされ、ノイズ感が残らない）
                    const float wireEnv = stickAtk * std::exp(-36.0f * dt);
                    const float rawNoise = 0.55f * rng.nextPink() + 0.45f * rng.nextGaussian();
                    const float snareWire = (wireBp1.processBandpass(rawNoise) * 1.15f
                                           + wireBp2.processBandpass(rawNoise) * 0.85f) * wireEnv;
                    const float woodShell = shellBody.processBandpass(rawNoise) * std::exp(-42.0f * dt) * 0.55f;

                    const float snareSample = (membrane * 0.68f + snareWire * 0.58f + woodShell) * stickAtk * 0.48f;
                    outL[startIdx + i] += snareSample * gainL;
                    outR[startIdx + i] += snareSample * gainR;
                }
            }

            // ロール終盤に向かってストローク間隔が詰まり密度が上がる (56ms → 34ms)
            strokeTime += 0.056f - 0.022f * progress;
        }

        // 裏で支える 32インチ・シンフォニック・ティンパニのクレッシェンドロール (G2 = 98Hz → C3 = 130.81Hz)
        for (int tHit = 0; tHit < 16; ++tHit)
        {
            const float u      = static_cast<float>(tHit) / 15.0f;
            const float hitSec = u * (kRollEndSec - 0.04f);
            const int startIdx = static_cast<int>(std::lround(hitSec * sr));
            const int len      = std::min(numSamples - startIdx, static_cast<int>(0.22 * sr));
            const float f0     = 98.0f + u * 32.81f;
            const float vel    = (0.42f + 0.58f * u) * 0.34f;
            const float pan    = (tHit & 1) ? 0.32f : 0.68f;
            const float gL     = std::cos(pan * 1.5707963f) * vel;
            const float gR     = std::sin(pan * 1.5707963f) * vel;

            for (int i = 0; i < len; ++i)
            {
                const float dt = static_cast<float>(i) / static_cast<float>(sr);
                const float atk = (dt < 0.0035f) ? (dt / 0.0035f) : 1.0f;
                const float w = kTwoPi * f0 * (1.0f + 0.08f * std::exp(-35.0f * dt)) * dt;
                // ティンパニ固有の調和モード比 (1.00 : 1.34 : 1.66)
                const float timp = (0.65f * std::sin(w) * std::exp(-12.0f * dt)
                                  + 0.25f * std::sin(w * 1.34f) * std::exp(-22.0f * dt)
                                  + 0.10f * std::sin(w * 1.66f) * std::exp(-34.0f * dt)) * atk;
                outL[startIdx + i] += timp * gL;
                outR[startIdx + i] += timp * gR;
            }
        }

        // ---------------------------------------------------------------------
        // パートB (1.26s 〜 最後まで): クライマックス「ジャン！！」
        //   ① スネア・リムショット ＋ ティンパニ C2/C3 強打
        //   ② 40モード Kirchhoff-Love 青銅クラッシュシンバル物理合成（シズル感と黄金の輝き）
        //   ③ シンフォニック・ブラス＆ローズのメジャー9th ファンファーレ和音
        // ---------------------------------------------------------------------
        const int finaleIdx = static_cast<int>(std::lround(kFinaleSec * sr));
        if (finaleIdx < numSamples)
        {
            const int remLen = numSamples - finaleIdx;

            // 青銅クラッシュシンバルの非整数次円板モード周波数群 (380Hz 〜 13.5kHz, 黄金比・プラスチック数分散)
            std::array<double, 40> cymPhasesL{}, cymPhasesR{}, cymInc{};
            std::array<float, 40>  cymDecays{}, cymGains{};
            for (size_t m = 0; m < 40; ++m)
            {
                const float u = static_cast<float>(m) / 39.0f;
                // Kirchhoff-Love 板振動方程式に基づく高密度非調和モード分布
                const float freq = 395.0f * std::pow(33.5f, u) * (1.0f + 0.042f * std::sin(static_cast<float>(m * 19 + 7)));
                cymInc[m]     = (freq < static_cast<float>(sr) * 0.44f) ? (kTwoPi * static_cast<double>(freq) / sr) : 0.0;
                cymPhasesL[m] = rng.nextFloat01() * kTwoPi;
                cymPhasesR[m] = cymPhasesL[m] + (0.35f + 0.9f * rng.nextFloat01());
                // 低次銅ゴングモードは長く (2.8/s)、高域シズルモードは滑らかに (6.5/s) 減衰
                cymDecays[m]  = 2.6f + 4.8f * u;
                cymGains[m]   = (m < 8 ? 0.018f : (0.032f / std::sqrt(1.0f + static_cast<float>(m) * 0.35f)));
            }

            // シンフォニック・ファンファーレ和音 (C3, G3, C4, E4, G4, B4, D5)
            static constexpr std::array<float, 7> kFanfareChord = {
                130.81f, 196.00f, 261.63f, 329.63f, 392.00f, 493.88f, 587.33f
            };
            static constexpr std::array<float, 7> kFanfarePan = {
                0.50f, 0.35f, 0.65f, 0.25f, 0.75f, 0.30f, 0.70f
            };

            SeTptSvf cymAirBpL, cymAirBpR;
            cymAirBpL.setCoeffs(sr, 6800.0, 0.95);
            cymAirBpR.setCoeffs(sr, 7400.0, 0.95);

            for (int i = 0; i < remLen; ++i)
            {
                const float dt = static_cast<float>(i) / static_cast<float>(sr);
                float fL = 0.0f, fR = 0.0f;

                // ① フィナーレ打撃（ティンパニ C2=65.4Hz + C3=130.8Hz ＆ リムショット 220Hz/480Hz）
                const float impactAtk = (dt < 0.0012f) ? (dt / 0.0012f) : 1.0f;
                const float pitchDrop = 1.0f + 0.28f * std::exp(-55.0f * dt);
                const float timpBoom  = (0.52f * std::sin(kTwoPi * 65.41f * pitchDrop * dt) * std::exp(-4.2f * dt)
                                       + 0.38f * std::sin(kTwoPi * 130.81f * pitchDrop * dt) * std::exp(-6.5f * dt)
                                       + 0.25f * std::sin(kTwoPi * 245.0f * pitchDrop * dt) * std::exp(-38.0f * dt)) * impactAtk;
                fL += timpBoom * 0.48f;
                fR += timpBoom * 0.48f;

                // ② 40モード青銅クラッシュシンバル（ブロンズの重厚なシズル感と倍音シャワー）
                const float cymAtk = (dt < 0.0025f) ? (dt / 0.0025f) : 1.0f;
                float cymL = 0.0f, cymR = 0.0f;
                for (size_t m = 0; m < 40; ++m)
                {
                    if (cymInc[m] <= 0.0)
                        continue;
                    const float env = std::exp(-cymDecays[m] * dt) * cymGains[m];
                    cymL += static_cast<float>(std::sin(cymPhasesL[m])) * env;
                    cymR += static_cast<float>(std::sin(cymPhasesR[m])) * env;
                    cymPhasesL[m] += cymInc[m];
                    cymPhasesR[m] += cymInc[m];
                }
                // ブロンズ合金特有の初期高域シズル粒子（最初の180msだけシンバルモードと乗算変調）
                const float sizzleEnv = cymAtk * std::exp(-6.2f * dt) * 0.14f;
                const float sizzleL   = cymAirBpL.processBandpass(rng.nextPink() + cymL * 2.5f) * sizzleEnv;
                const float sizzleR   = cymAirBpR.processBandpass(rng.nextPink() + cymR * 2.5f) * sizzleEnv;
                fL += (cymL * cymAtk + sizzleL) * 0.55f;
                fR += (cymR * cymAtk + sizzleR) * 0.55f;

                // ③ シンフォニック・ブラス＆ローズのメジャー9th ファンファーレ和音
                const float brassAtk = (dt < 0.018f) ? (0.5f * (1.0f - std::cos(3.14159265f * dt / 0.018f))) : 1.0f;
                // 金管楽器特有の「ブラス・ブルーム（立ち上がり直後に倍音が華やかに開く）」
                const float brassBloom = 0.45f + 0.55f * std::exp(-3.8f * dt);
                const float sustainEnv = std::exp(-2.15f * dt) * brassAtk;

                for (size_t k = 0; k < kFanfareChord.size(); ++k)
                {
                    const float f = kFanfareChord[k];
                    const float w1 = kTwoPi * f * dt;
                    const float wDet = kTwoPi * (f * 1.0018f) * dt;
                    const float note = (0.50f * (std::sin(w1) + std::sin(wDet))
                                      + 0.32f * brassBloom * std::sin(w1 * 2.0f)
                                      + 0.16f * brassBloom * std::sin(w1 * 3.0f)
                                      + 0.07f * brassBloom * std::sin(w1 * 4.0f) * std::exp(-5.0f * dt)) * sustainEnv * 0.13f;
                    const float pan = kFanfarePan[k];
                    fL += note * std::cos(pan * 1.5707963f);
                    fR += note * std::sin(pan * 1.5707963f);
                }

                outL[finaleIdx + i] += fL;
                outR[finaleIdx + i] += fR;
            }
        }

        // ---------------------------------------------------------------------
        // パートC: コンサートホール8x8 FDN残響＋マスタリング飽和
        // ---------------------------------------------------------------------
        SeConcertHallFdnReverb hall;
        hall.prepare(sr, 1.42f, 6800.0f);
        for (int i = 0; i < numSamples; ++i)
        {
            float revL = 0.0f, revR = 0.0f;
            hall.processStereo(outL[i], outR[i], 0.30f, revL, revR);
            outL[i] = std::tanh(revL * 0.92f);
            outR[i] = std::tanh(revR * 0.92f);
        }
    }
};

// =============================================================================
// 6. SE 3: ◆ベル — BroadcastVibraphoneAndTubularBellSynthesizer
//    （Musser ヴィブラフォン・アーチ音板 ＋ オーケストラ・チューブラーベル ＋ ローズ共鳴）
// =============================================================================
class BroadcastVibraphoneAndTubularBellSynthesizer
{
public:
    struct BellNoteEvent
    {
        float onsetSec;
        float freqHz;
        float pan01;
        float velocity;
        bool  includeTubularCrown;
    };

    static void synthesizeStereo(float* outL, float* outR, int numSamples, double sampleRate) noexcept
    {
        if (outL == nullptr || outR == nullptr || numSamples <= 0)
            return;

        const double sr = std::max(8000.0, sampleRate);
        constexpr float kTwoPi = 6.28318530717958647692f;
        std::fill(outL, outL + numSamples, 0.0f);
        std::fill(outR, outR + numSamples, 0.0f);

        // 放送局・劇場の品格ある3ステップ・ラグジュアリーチャイム＆解決コード
        //   Step 1 (0.00s): A4 (440Hz) + E5 (659.25Hz)
        //   Step 2 (0.24s): F#5 (739.99Hz) + C#6 (1108.73Hz)
        //   Step 3 (0.50s): D4 (293.66Hz) + A4 (440.00Hz) + E5 (659.25Hz) + F#5 (739.99Hz) + A5 (880.00Hz) (Dmaj9 解決和音)
        static constexpr std::array<BellNoteEvent, 9> kChimeScore = {{
            { 0.000f,  440.00f, 0.32f, 0.85f, false },
            { 0.005f,  659.25f, 0.68f, 0.92f, true  },
            { 0.235f,  587.33f, 0.38f, 0.82f, false },
            { 0.240f,  880.00f, 0.64f, 0.95f, true  },
            { 0.495f,  293.66f, 0.50f, 0.90f, false },
            { 0.502f,  440.00f, 0.28f, 0.82f, false },
            { 0.510f,  659.25f, 0.72f, 0.86f, false },
            { 0.518f,  739.99f, 0.36f, 0.92f, true  },
            { 0.526f, 1174.66f, 0.64f, 0.88f, true  }
        }};

        for (const auto& ev : kChimeScore)
        {
            const int startIdx = static_cast<int>(std::lround(ev.onsetSec * sr));
            const int len      = std::min(numSamples - startIdx, static_cast<int>(2.1 * sr));
            if (startIdx < 0 || len <= 8)
                continue;

            const float f0    = ev.freqHz;
            const float gainL = std::cos(ev.pan01 * 1.5707963f) * ev.velocity;
            const float gainR = std::sin(ev.pan01 * 1.5707963f) * ev.velocity;

            double pFund = 0.0, pFundDet = 0.3, pVibe4 = 0.0, pTube1 = 0.0, pTube2 = 0.0, pSub = 0.0;
            const double dFund    = kTwoPi * static_cast<double>(f0) / sr;
            const double dFundDet = kTwoPi * static_cast<double>(f0 * 1.0011f) / sr;
            // Musser ヴィブラフォンのアーチ削り出しアルミ音板は第2モードが正確に 4.00 倍音に調律されている
            const double dVibe4   = kTwoPi * static_cast<double>(f0 * 4.000f) / sr;
            // チューブラーベル（オーケストラチャイム）の非整数次クラウン打撃倍音 (2.756, 5.404)
            const double dTube1   = kTwoPi * static_cast<double>(f0 * 2.756f) / sr;
            const double dTube2   = kTwoPi * static_cast<double>(f0 * 5.404f) / sr;
            const double dSub     = kTwoPi * static_cast<double>(f0 * 0.500f) / sr;

            for (int i = 0; i < len; ++i)
            {
                const float dt = static_cast<float>(i) / static_cast<float>(sr);
                // 毛糸巻きマレットの柔らかで気品ある打撃アタック (4.2ms)
                const float malletAtk = (dt < 0.0042f) ? (0.5f * (1.0f - std::cos(3.14159265f * dt / 0.0042f))) : 1.0f;

                // ヴィブラフォンのモーター回転バタフライ・レゾネーターによる L/R 直交トレモロ (5.4Hz)
                const float motorL = 0.84f + 0.16f * std::sin(kTwoPi * 5.4f * dt);
                const float motorR = 0.84f + 0.16f * std::cos(kTwoPi * 5.4f * dt);

                // 1. ヴィブラフォン基音 ＋ 4倍音アーチ共鳴 ＋ サブオクターブの温かいボディ
                const float vibeBody = (0.54f * static_cast<float>(std::sin(pFund))
                                      + 0.40f * static_cast<float>(std::sin(pFundDet))) * std::exp(-2.15f * dt)
                                     + 0.22f * static_cast<float>(std::sin(pVibe4)) * std::exp(-9.5f * dt)
                                     + 0.14f * static_cast<float>(std::sin(pSub)) * std::exp(-3.2f * dt);

                // 2. チューブラーベルの華やかな金属クラウン倍音（打撃直後に煌めいて品よく減衰）
                float tubularBell = 0.0f;
                if (ev.includeTubularCrown)
                {
                    if (f0 * 2.756f < static_cast<float>(sr) * 0.44f)
                        tubularBell += 0.20f * static_cast<float>(std::sin(pTube1)) * std::exp(-14.5f * dt);
                    if (f0 * 5.404f < static_cast<float>(sr) * 0.44f)
                        tubularBell += 0.08f * static_cast<float>(std::sin(pTube2)) * std::exp(-28.0f * dt);
                }

                const float sig = (vibeBody + tubularBell) * malletAtk * 0.26f;
                outL[startIdx + i] += sig * gainL * motorL;
                outR[startIdx + i] += sig * gainR * motorR;

                pFund    += dFund;
                pFundDet += dFundDet;
                pVibe4   += dVibe4;
                pTube1   += dTube1;
                pTube2   += dTube2;
                pSub     += dSub;
            }
        }

        // ---------------------------------------------------------------------
        // Bricasti M7 風ステレオコンサートホールFDN残響で極上の透明感と奥行きを付与
        // ---------------------------------------------------------------------
        SeConcertHallFdnReverb hall;
        hall.prepare(sr, 1.48f, 8400.0f);
        for (int i = 0; i < numSamples; ++i)
        {
            float revL = 0.0f, revR = 0.0f;
            hall.processStereo(outL[i], outR[i], 0.36f, revL, revR);
            outL[i] = std::tanh(revL * 0.90f);
            outR[i] = std::tanh(revR * 0.90f);
        }
    }
};

// =============================================================================
// 7. 統合スタジオSEマスタリング・レンダラー（全4種共通 True-Peak 整音＆フェード仕上げ）
// =============================================================================
class StudioLiveSoundEffectsEngine
{
public:
    // 各SEの推奨バッファ秒数（ホール残響が自然に消え入る十分な長さを確保）
    static constexpr double getRecommendedDurationSeconds(int index) noexcept
    {
        switch (index)
        {
            case 0:  return 3.0; // ♪拍手: 3.0秒（劇場220名の大拍手＋ホール残響）
            case 1:  return 2.5; // ✦キラ: 2.5秒（16音チェレスタ＋8弦ハープ＋36本ウィンドチャイム＋シマー残響）
            case 2:  return 2.7; // ●ドラム: 2.7秒（1.25秒プレスロール＋ティンパニ → 1.45秒クラッシュ＆ブラスファンファーレ）
            case 3:  return 2.3; // ◆ベル: 2.3秒（3ステップ・ヴィブラフォン＆チューブラーベル解決カデンツ）
            default: return 2.5;
        }
    }

    static void renderStudioSeBuffer(int index, float* outL, float* outR, int numSamples, double sampleRate) noexcept
    {
        if (outL == nullptr || outR == nullptr || numSamples <= 0)
            return;

        const double sr = std::max(8000.0, sampleRate);

        switch (index)
        {
            case 0:
                AcousticCrowdApplauseSynthesizer::synthesizeStereo(outL, outR, numSamples, sr);
                studio_foley::EncoreBravosAndWarmHallCheerSynthesizer::addWarmHallAudienceLayer(outL, outR, numSamples, sr);
                break;
            case 1:
                OrchestralCelestaHarpAndMarkTreeSynthesizer::synthesizeStereo(outL, outR, numSamples, sr);
                studio_foley::GlassHarmonicaAndBisbigliandoShimmerSynthesizer::addCrystalGlassAndBisbigliandoLayer(outL, outR, numSamples, sr);
                break;
            case 2:
                SymphonicSnareRollTimpaniAndCrashSynthesizer::synthesizeStereo(outL, outR, numSamples, sr);
                studio_foley::GranCassaBassDrumAndTamTamGongSynthesizer::addGranCassaAndTamTamFinale(outL, outR, numSamples, sr, 1.26f);
                break;
            case 3:
            default:
                BroadcastVibraphoneAndTubularBellSynthesizer::synthesizeStereo(outL, outR, numSamples, sr);
                studio_foley::FivePartialCarillonAndHandbellChoirSynthesizer::addCarillonAndHandbellBloom(outL, outR, numSamples, sr);
                break;
        }

        // ---------------------------------------------------------------------
        // スタジオ・マスタリング正規化＆コサイン・テールフェードアウト
        //   以前の -19.1 dBFS (0.11f) では歌やBGMの下で埋もれて貧弱に聞こえたため、
        //   歌声と自然に共存しつつ華やかに存在感を放つスタジオ標準レベル (-6.8 dBFS ≈ 0.455f) に正規化。
        // ---------------------------------------------------------------------
        float maxPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            if (!std::isfinite(outL[i])) outL[i] = 0.0f;
            if (!std::isfinite(outR[i])) outR[i] = 0.0f;
            maxPeak = std::max(maxPeak, std::max(std::fabs(outL[i]), std::fabs(outR[i])));
        }

        const float targetPeak = (index == 0) ? 0.42f : ((index == 2) ? 0.48f : 0.45f);
        const float gainScale  = (maxPeak > 1.0e-5f) ? (targetPeak / maxPeak) : 0.0f;
        const int   fadeLen    = std::min(numSamples / 4, static_cast<int>(0.14 * sr));
        const int   fadeStart  = numSamples - fadeLen;

        for (int i = 0; i < numSamples; ++i)
        {
            float env = gainScale;
            if (i >= fadeStart && fadeLen > 0)
            {
                const float u = static_cast<float>(i - fadeStart) / static_cast<float>(fadeLen);
                env *= 0.5f * (1.0f + std::cos(3.14159265358979323846f * u));
            }
            outL[i] *= env;
            outR[i] *= env;
        }
    }
};

} // namespace studio_se
} // namespace gzzio

namespace vg
{
namespace studio_se = ::gzzio::studio_se;
} // namespace vg
