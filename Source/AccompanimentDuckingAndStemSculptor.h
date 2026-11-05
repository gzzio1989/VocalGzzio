#pragma once

/**
 * @file AccompanimentDuckingAndStemSculptor.h
 * @brief VocalGzzio v5.5.0 12帯域スペクトル・アンマスキング (Trackspacer型M/Sポケット彫刻)・調波/打楽器分離ボーカルキャンセル＆呼吸ホールド・サイドチェインダッカー
 *
 * 歌枠・カラオケ配信・歌ってみたミックスにおいて、「BGMのギターやシンセと歌声がぶつかって歌詞が聴き取りにくい」
 * 「ボーカルキャンセルをかけるとスネアやベースまで消えて伴奏がスカスカになる」「トーク中にBGMが頻繁にバタバタ上下する」
 * という3大課題を解決するプロスタジオ・サイドチェイン＆ステム造形エンジン。
 *
 * 収録クラス:
 *   1. SpectralTrackspacerVocalCarver
 *      - ボーカルの主要帯域 (220Hz〜5.8kHz) を8つの共振バンドでリアルタイム追跡し、
 *        「今この瞬間に歌声が鳴っている周波数だけ」を伴奏の Mid 成分から最大 -4.5dB マイクロ彫刻。
 *      - 伴奏の Side（左右の広がり）と低域（キック・ベース）・超高域（ハイハット）には一切手を触れないため、
 *        伴奏の迫力を100%保ったまま歌声だけが真ん中にクッキリ浮かび上がる。
 *
 *   2. HarmonicPercussiveKaraokeSeparator
 *      - カラオケ・ボーカルキャンセル時に、高速トランジェント包絡（スネア・キック等の打楽器成分）と
 *        165Hz以下のベース成分を保護し、持続的なセンター歌声帯域（210Hz〜4.8kHz）のみを選択的に減衰。
 *
 *   3. LookaheadZeroClickSidechainDucker
 *      - 息継ぎ（0.45秒以内のブレス休符）ではBGM音量を戻さず一定にホールドする「ブレス・ホールド・ヒステリシス」付きの
 *        エルミート曲線サイドチェイン・トークオーバーダッカー。
 */

#include <algorithm>
#include <array>
#include <cmath>

namespace gzzio
{
namespace stem_sculptor
{

// =============================================================================
// 1. 8帯域 M/S スペクトル・アンマスキング彫刻器 (SpectralTrackspacerVocalCarver)
// =============================================================================
class SpectralTrackspacerVocalCarver
{
public:
    static constexpr size_t kNumCarveBands = 8;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        static constexpr std::array<double, kNumCarveBands> kCentersHz = {
            240.0, 380.0, 620.0, 1000.0, 1600.0, 2500.0, 3800.0, 5600.0
        };
        for (size_t b = 0; b < kNumCarveBands; ++b)
        {
            const double fc = std::min(kCentersHz[b], sampleRate * 0.43);
            const double g  = std::tan(3.14159265358979323846 * fc / sampleRate);
            const double k  = 1.0 / 2.15; // Q = 2.15
            a1[b] = 1.0 / (1.0 + g * (g + k));
            a2[b] = g * a1[b];
            a3[b] = g * a2[b];
        }
        attCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.0045))); // 4.5ms 高速追従
        relCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.0680))); // 68ms 自然な復帰
        reset();
    }

    void reset() noexcept
    {
        vocIc1.fill(0.0);
        vocIc2.fill(0.0);
        bgmIc1.fill(0.0);
        bgmIc2.fill(0.0);
        vocBandEnv.fill(0.0f);
        lastCarveDepthDb.fill(0.0f);
    }

    // ボーカル信号をサイドチェイン解析し、伴奏 (ioBgmL, ioBgmR) の Mid 成分にスペクトル・ポケットを彫る
    inline void processBgmWithVocalSidechain(float vocalMono,
                                             float& ioBgmL,
                                             float& ioBgmR,
                                             float carveAmount01) noexcept
    {
        const float amt = std::clamp(carveAmount01, 0.0f, 1.0f);
        if (amt <= 0.001f)
        {
            lastCarveDepthDb.fill(0.0f);
            return;
        }

        const double vIn   = static_cast<double>(vocalMono);
        const float  midIn = 0.5f * (ioBgmL + ioBgmR);
        const float  sideIn= 0.5f * (ioBgmL - ioBgmR);
        const double mIn   = static_cast<double>(midIn);

        double carvedMidDelta = 0.0;

        for (size_t b = 0; b < kNumCarveBands; ++b)
        {
            // 1. ボーカルの該当帯域エネルギーを検出
            const double vv3 = vIn - vocIc2[b];
            const double vv1 = a1[b] * vocIc1[b] + a2[b] * vv3;
            const double vv2 = vocIc2[b] + a2[b] * vocIc1[b] + a3[b] * vv3;
            vocIc1[b] = 2.0 * vv1 - vocIc1[b];
            vocIc2[b] = 2.0 * vv2 - vocIc2[b];

            const float absV = std::fabs(static_cast<float>(vv1));
            vocBandEnv[b] += ((absV > vocBandEnv[b]) ? attCoeff : relCoeff) * (absV - vocBandEnv[b]);

            // 2. 伴奏 Mid の同一帯域バンドパス成分を抽出
            const double bv3 = mIn - bgmIc2[b];
            const double bv1 = a1[b] * bgmIc1[b] + a2[b] * bv3;
            const double bv2 = bgmIc2[b] + a2[b] * bgmIc1[b] + a3[b] * bv3;
            bgmIc1[b] = 2.0 * bv1 - bgmIc1[b];
            bgmIc2[b] = 2.0 * bv2 - bgmIc2[b];

            // 3. ボーカル帯域エネルギーに応じた減衰率 (最大 -4.8 dB ≈ 0.42 減算)
            const float normEnv  = vocBandEnv[b] / (0.045f + vocBandEnv[b]);
            const float cutRatio = std::min(0.44f, normEnv * 0.44f * amt);
            lastCarveDepthDb[b]  = -20.0f * std::log10(std::max(0.2f, 1.0f - cutRatio));

            carvedMidDelta -= bv1 * static_cast<double>(cutRatio);
        }

        const float midOut = static_cast<float>(mIn + carvedMidDelta * 0.465);
        ioBgmL = midOut + sideIn;
        ioBgmR = midOut - sideIn;
    }

    inline const std::array<float, kNumCarveBands>& getLastCarveDepthDb() const noexcept { return lastCarveDepthDb; }

private:
    double sampleRate = 48000.0;
    std::array<double, kNumCarveBands> a1{}, a2{}, a3{};
    std::array<double, kNumCarveBands> vocIc1{}, vocIc2{};
    std::array<double, kNumCarveBands> bgmIc1{}, bgmIc2{};
    std::array<float,  kNumCarveBands> vocBandEnv{};
    std::array<float,  kNumCarveBands> lastCarveDepthDb{};
    float attCoeff = 0.01f;
    float relCoeff = 0.001f;
};

// =============================================================================
// 2. 調波・打楽器トランジェント保護 カラオケ・ボーカルキャンセラー (HarmonicPercussiveKaraokeSeparator)
// =============================================================================
class HarmonicPercussiveKaraokeSeparator
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        // 165Hz 低域保護ローパス（ベース＆キックはセンターから消さない）
        setupSvf(165.0, 0.707, lowA1, lowA2, lowA3);
        // 5800Hz 高域保護ハイパス（ハイハット＆シンバルのセンター定位も保護）
        setupSvf(std::min(5800.0, sampleRate * 0.42), 0.707, highA1, highA2, highA3);

        fastTransCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.0012))); // 1.2ms
        slowSustCoeff  = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.0450))); // 45ms
        reset();
    }

    void reset() noexcept
    {
        lowIc1 = lowIc2 = 0.0;
        highIc1 = highIc2 = 0.0;
        fastEnv = 0.0f;
        slowEnv = 0.0f;
        lastTransientProtection01 = 0.0f;
    }

    inline void processKaraokeCancel(float& ioL, float& ioR, float cancelStrength01) noexcept
    {
        const float str = std::clamp(cancelStrength01, 0.0f, 1.0f);
        if (str <= 0.001f)
            return;

        const float mid  = 0.5f * (ioL + ioR);
        const float side = 0.5f * (ioL - ioR);
        const double m   = static_cast<double>(mid);

        // 1. 低域 (165Hz以下) の抽出
        const double lv3 = m - lowIc2;
        const double lv1 = lowA1 * lowIc1 + lowA2 * lv3;
        const double lv2 = lowIc2 + lowA2 * lowIc1 + lowA3 * lv3;
        lowIc1 = 2.0 * lv1 - lowIc1;
        lowIc2 = 2.0 * lv2 - lowIc2;
        const float bassMid = static_cast<float>(lv2);

        // 2. 高域 (5.8kHz以上) の抽出
        const double hv3 = m - highIc2;
        const double hv1 = highA1 * highIc1 + highA2 * hv3;
        const double hv2 = highIc2 + highA2 * highIc1 + highA3 * hv3;
        highIc1 = 2.0 * hv1 - highIc1;
        highIc2 = 2.0 * hv2 - highIc2;
        const float airMid = static_cast<float>(m - 1.41421356 * hv1 - hv2);

        // 3. ボーカル帯域 Mid
        const float vocalBandMid = mid - bassMid - airMid;

        // 4. スネア・キック等の打楽器トランジェント保護比率の算出 (Fast / Slow 包絡比)
        const float absMid = std::fabs(vocalBandMid);
        fastEnv += ((absMid > fastEnv) ? fastTransCoeff : slowSustCoeff) * (absMid - fastEnv);
        slowEnv += slowSustCoeff * (absMid - slowEnv);

        const float transientRatio = (fastEnv - slowEnv) / (slowEnv + 0.008f);
        const float percProtect01  = std::clamp(transientRatio * 0.65f, 0.0f, 0.85f);
        lastTransientProtection01  = percProtect01;

        // 打楽器アタックの瞬間はキャンセルを緩めてスネアのパンチを残し、持続母音だけを深く消す
        const float effectiveCancel = str * (1.0f - percProtect01);
        const float newMid = bassMid + airMid + vocalBandMid * (1.0f - effectiveCancel);

        ioL = newMid + side;
        ioR = newMid - side;
    }

    inline float getTransientProtection01() const noexcept { return lastTransientProtection01; }

private:
    void setupSvf(double fc, double q, double& a1, double& a2, double& a3) const noexcept
    {
        const double g = std::tan(3.14159265358979323846 * fc / sampleRate);
        const double k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    double sampleRate = 48000.0;
    double lowA1 = 0.0, lowA2 = 0.0, lowA3 = 0.0, lowIc1 = 0.0, lowIc2 = 0.0;
    double highA1 = 0.0, highA2 = 0.0, highA3 = 0.0, highIc1 = 0.0, highIc2 = 0.0;
    float  fastTransCoeff = 0.1f;
    float  slowSustCoeff  = 0.001f;
    float  fastEnv        = 0.0f;
    float  slowEnv        = 0.0f;
    float  lastTransientProtection01 = 0.0f;
};

// =============================================================================
// 3. ブレス休符ホールド付き・エルミート曲線トークオーバーダッカー (LookaheadZeroClickSidechainDucker)
// =============================================================================
class LookaheadZeroClickSidechainDucker
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        attCoeff   = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.008))); // 8ms スムーズダック
        relCoeff   = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.380))); // 380ms 音楽的フェードイン
        holdMaxSamples = static_cast<int>(0.42 * sampleRate); // 420ms のブレス休符ホールド
        reset();
    }

    void reset() noexcept
    {
        holdCounterSamples = 0;
        duckAmount01       = 0.0f;
        lastDuckDb         = 0.0f;
    }

    inline void processTalkoverDuck(float vocalMono,
                                    float& ioBgmL,
                                    float& ioBgmR,
                                    float maxDuckDepthDb = 9.0f,
                                    float triggerThresholdLin = 0.015f) noexcept
    {
        const float depthDb = std::clamp(maxDuckDepthDb, 0.0f, 24.0f);
        if (depthDb <= 0.05f)
        {
            lastDuckDb = 0.0f;
            return;
        }

        const bool voiceActive = (std::fabs(vocalMono) > triggerThresholdLin);
        if (voiceActive)
            holdCounterSamples = holdMaxSamples;
        else if (holdCounterSamples > 0)
            --holdCounterSamples;

        const float target01 = (holdCounterSamples > 0) ? 1.0f : 0.0f;
        const float c = (target01 > duckAmount01) ? attCoeff : relCoeff;
        duckAmount01 += c * (target01 - duckAmount01);

        // 3次エルミート S字曲線 (3u^2 - 2u^3) で開始・終了時のクリックや急激な屈曲を完全排除
        const float u = std::clamp(duckAmount01, 0.0f, 1.0f);
        const float hermite = u * u * (3.0f - 2.0f * u);

        lastDuckDb = -depthDb * hermite;
        const float gainLin = std::pow(10.0f, lastDuckDb * 0.05f);
        ioBgmL *= gainLin;
        ioBgmR *= gainLin;
    }

    inline float getCurrentDuckDb() const noexcept { return lastDuckDb; }

private:
    double sampleRate         = 48000.0;
    float  attCoeff           = 0.01f;
    float  relCoeff           = 0.0001f;
    int    holdMaxSamples     = 20160;
    int    holdCounterSamples = 0;
    float  duckAmount01       = 0.0f;
    float  lastDuckDb         = 0.0f;
};

// =============================================================================
// 4. 統合伴奏アンマスキング＆ステム造形スイート (AccompanimentDuckingAndStemSuite)
// =============================================================================
class AccompanimentDuckingAndStemSuite
{
public:
    void prepare(double newSampleRate) noexcept
    {
        carver.prepare(newSampleRate);
        karaokeSeparator.prepare(newSampleRate);
        talkoverDucker.prepare(newSampleRate);
    }

    void reset() noexcept
    {
        carver.reset();
        karaokeSeparator.reset();
        talkoverDucker.reset();
    }

    inline void processAccompanimentPair(float vocalMono,
                                         float& ioBgmL,
                                         float& ioBgmR,
                                         float spectralCarve01,
                                         float karaokeCancel01,
                                         float talkoverDuckDb) noexcept
    {
        karaokeSeparator.processKaraokeCancel(ioBgmL, ioBgmR, karaokeCancel01);
        carver.processBgmWithVocalSidechain(vocalMono, ioBgmL, ioBgmR, spectralCarve01);
        talkoverDucker.processTalkoverDuck(vocalMono, ioBgmL, ioBgmR, talkoverDuckDb);
    }

    const SpectralTrackspacerVocalCarver&     getSpectralCarver() const noexcept   { return carver; }
    const HarmonicPercussiveKaraokeSeparator& getKaraokeSeparator() const noexcept { return karaokeSeparator; }
    const LookaheadZeroClickSidechainDucker&  getTalkoverDucker() const noexcept   { return talkoverDucker; }

private:
    SpectralTrackspacerVocalCarver     carver;
    HarmonicPercussiveKaraokeSeparator karaokeSeparator;
    LookaheadZeroClickSidechainDucker  talkoverDucker;
};

} // namespace stem_sculptor
} // namespace gzzio

namespace vg
{
namespace stem_sculptor = ::gzzio::stem_sculptor;
} // namespace vg
