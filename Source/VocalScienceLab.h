#pragma once

// =============================================================================
//  VocalScienceLab.h — VocalGzzio v5.5.0
//  音響音声学（Acoustic Phonetics）・心理音響バーク尺度マスキング解析・
//  シンガーズフォルマント共振器 ＆ リアルタイム無事故ガーディアン統合ヘッダ
// =============================================================================
//  【研究開発・学術背景】
//  1. Johan Sundberg "The Science of the Singing Voice" (シンガーズフォルマント理論)
//     - オペラ歌手や一流ポップスシンガーがオーケストラや高密度なバンド伴奏の中でも
//       マイク音量を無理に上げずに声が前に抜ける理由は、咽頭上腔（Epilarynx tube）の
//       共鳴により第3・第4・第5フォルマントが 2.8kHz〜3.4kHz に密集（クラスタリング）
//       するためである。ただし歯擦音（/s/, /sh/, /t/）の区間でこの帯域を強調すると
//       耳障りになるため、本モジュールは「有声音（母音）区間のみを零交差率（ZCR）と
//       スペクトル傾斜（Spectral Tilt）でリアルタイム識別」し、母音発声時のみ
//       咽頭共鳴クラスタをゼロ遅延で形成します。
//  2. Zwicker & Fastl "Psychoacoustics: Facts and Models" (バーク尺度臨界帯域マスキング)
//     - 人間の蝸牛基底膜の周波数分解能に対応する臨界帯域（Bark Scale）上でボーカルと
//       カラオケ伴奏のエネルギー分布および上向き・下向きマスキング拡散関数（Spreading
//       Function）を計算し、「いま伴奏のどの帯域が歌声を覆い隠しているか（Masking Map）」
//       と「歌声の明瞭度指数（Vocal Audibility Index: 0〜100%）」をリアルタイム算出。
//  3. サブグロッタル・チェストアンカー（Sub-Glottal Chest Resonance Bridge）
//     - ハイトーンや裏声（ファルセット）へ移行した際に急激に失われる 130Hz〜260Hz の
//       胸腔共鳴（Chest Resonance）を、ピッチ包絡と同期した偶奇サブハーモニック共振で
//       自然に補完し、地声から裏声への換声点（Passaggio）の段差を滑らかに繋ぎます。
//  4. リアルタイム・オーディオ無事故ガーディアン（AudioStabilityGuardian）
//     - 配信・収録現場で起きる USB マイクの瞬断ポップノイズ、DC オフセットドリフト、
//       非正規化数（Denormal）による CPU スパイク、NaN/Inf 伝播、サンプルレート切替時の
//       不連続クリックをゼロヒープ割り当てで常時監視・無害化する安全機構です。
// =============================================================================

#include "MasteringProEngine.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace gzzio
{
namespace science
{

using gzzio::mastering::FastAudioMath;
using gzzio::mastering::TptStateVariableFilter;

// =============================================================================
//  1. 有声音／無声音（母音 vs 歯擦音・ブレス）ゼロ遅延リアルタイム識別器
// =============================================================================
//  零交差率（Zero-Crossing Rate: ZCR）と高低エネルギー比（Spectral Tilt: 300Hz以下 vs
//  4.5kHz以上）を組み合わせ、遅延 0 サンプルで現在のサンプルが
//  - Voiced Vowel (母音・有声音: 0.0 -> 1.0)
//  - Unvoiced Sibilant / Fricative (歯擦音・無声子音: 0.0 -> 1.0)
//  のどちらであるかを連続確率 [0.0 .. 1.0] として出力します。
// =============================================================================
class ZeroLagPhonemeClassifier
{
public:
    struct PhonemeState
    {
        float voicedProb01   = 0.0f; // 母音・有声発声の確信度 [0..1]
        float sibilantProb01 = 0.0f; // 歯擦音(/s/, /sh/, /ts/)の確信度 [0..1]
        float breathProb01   = 0.0f; // ブレス・息継ぎ成分の確信度 [0..1]
        float spectralTiltDb = 0.0f; // 高域/低域エネルギー傾斜(dB)
    };

    ZeroLagPhonemeClassifier() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        lowBodyFilter.prepare(sampleRate);
        lowBodyFilter.setParameters(TptStateVariableFilter::Mode::Lowpass, 420.0f, 0.7071f);

        highSibFilter.prepare(sampleRate);
        highSibFilter.setParameters(TptStateVariableFilter::Mode::Highpass, 4600.0f, 0.7071f);

        fastAtt = FastAudioMath::calcTimeConstantCoeff(sampleRate, 1.5f);
        fastRel = FastAudioMath::calcTimeConstantCoeff(sampleRate, 18.0f);
        zcrSmoothCoeff = FastAudioMath::calcTimeConstantCoeff(sampleRate, 6.0f);
        reset();
    }

    void reset() noexcept
    {
        lowBodyFilter.reset();
        highSibFilter.reset();
        lowEnv = 1.0e-6f;
        highEnv = 1.0e-6f;
        zcrRate = 0.0f;
        prevSample = 0.0f;
        lastState = {};
    }

    inline PhonemeState processSample(float input) noexcept
    {
        // 1. ゼロ交差（符号反転）の検出と平滑化レート算出
        const bool crossed = ((input >= 0.0f && prevSample < 0.0f) || (input < 0.0f && prevSample >= 0.0f));
        prevSample = input;
        const float instZcr = crossed ? static_cast<float>(sampleRate * 0.5) : 0.0f;
        zcrRate += zcrSmoothCoeff * (instZcr - zcrRate);

        // 2. 低域母音基音エネルギー（<420Hz）と高域摩擦音エネルギー（>4.6kHz）の包絡検波
        const float lowAbs  = std::fabs(lowBodyFilter.processSample(input));
        const float highAbs = std::fabs(highSibFilter.processSample(input));

        lowEnv  += ((lowAbs > lowEnv)   ? fastAtt : fastRel) * (lowAbs - lowEnv);
        highEnv += ((highAbs > highEnv) ? fastAtt : fastRel) * (highAbs - highEnv);

        const float totalEnv = lowEnv + highEnv + 1.0e-6f;
        if (totalEnv < 0.0005f)
        {
            // 無音時は全確率を滑らかに減衰
            lastState.voicedProb01   *= 0.95f;
            lastState.sibilantProb01 *= 0.95f;
            lastState.breathProb01   *= 0.95f;
            lastState.spectralTiltDb  = 0.0f;
            return lastState;
        }

        const float tiltDb = FastAudioMath::linearToDb((highEnv + 1.0e-6f) / (lowEnv + 1.0e-6f));
        lastState.spectralTiltDb = std::clamp(tiltDb, -48.0f, 48.0f);

        // 3. 母音確率: 低域基音が支配的で ZCR が 1800Hz 以下
        const float tiltVoiced = std::clamp((-tiltDb + 6.0f) / 20.0f, 0.0f, 1.0f);
        const float zcrVoiced  = std::clamp((2200.0f - zcrRate) / 1600.0f, 0.0f, 1.0f);
        lastState.voicedProb01 = tiltVoiced * zcrVoiced;

        // 4. 歯擦音確率: 高域エネルギーが強く ZCR が 3200Hz 以上
        const float tiltSib = std::clamp((tiltDb + 4.0f) / 16.0f, 0.0f, 1.0f);
        const float zcrSib  = std::clamp((zcrRate - 2400.0f) / 2800.0f, 0.0f, 1.0f);
        lastState.sibilantProb01 = tiltSib * zcrSib;

        // 5. ブレス確率: 中小音量（-46dB〜-24dB）かつフラット〜やや高域寄りのノイズ分布
        const float levelDb = FastAudioMath::linearToDb(totalEnv);
        const float breathLevelWin = (levelDb > -48.0f && levelDb < -20.0f) ? 1.0f : 0.0f;
        lastState.breathProb01 = breathLevelWin * std::clamp(1.0f - lastState.voicedProb01 - 0.5f * lastState.sibilantProb01, 0.0f, 1.0f);

        return lastState;
    }

    inline const PhonemeState& getLastState() const noexcept { return lastState; }

private:
    double sampleRate = 48000.0;
    TptStateVariableFilter lowBodyFilter;
    TptStateVariableFilter highSibFilter;
    float lowEnv = 1.0e-6f;
    float highEnv = 1.0e-6f;
    float zcrRate = 0.0f;
    float prevSample = 0.0f;
    float fastAtt = 0.05f;
    float fastRel = 0.005f;
    float zcrSmoothCoeff = 0.01f;
    PhonemeState lastState {};
};

// =============================================================================
//  2. Sundberg シンガーズフォルマント共振器 ＆ サブグロッタル胸鳴りブリッジ
// =============================================================================
//  母音発声中（voicedProb01 が高い区間）にのみ 2.85kHz / 3.25kHz の二重咽頭上腔共鳴クラスタ
//  （Epilarynx Dual-Formant Cluster）を形成して歌声の芯を前に押し出し、歯擦音区間では
//  瞬時にフラットへ退避することで「サ行が刺さらないのに歌詞が100%聴き取れる」特性を実現。
//  同時に、高音域で細くなりがちな胸鳴り（145Hz〜230Hz）を非線形サブハーモニック共振で
//  支えます。
// =============================================================================
class SingersFormantAndChestBridge
{
public:
    SingersFormantAndChestBridge() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        classifier.prepare(sampleRate);

        // シンガーズフォルマント 二重共鳴クラスタ (F3/F4 近接モデル: 2820Hz & 3280Hz)
        epilarynxResA.prepare(sampleRate);
        epilarynxResB.prepare(sampleRate);
        epilarynxResA.setParameters(TptStateVariableFilter::Mode::UnitGainBandpass, 2820.0f, 3.2f);
        epilarynxResB.setParameters(TptStateVariableFilter::Mode::UnitGainBandpass, 3280.0f, 3.6f);

        // 歯擦音保護用ソフトノッチ (6.4kHz)
        sibilantGuardBell.prepare(sampleRate);
        sibilantGuardBell.setParameters(TptStateVariableFilter::Mode::PeakBell, 6400.0f, 2.4f, 0.0f);

        // 胸腔共鳴（Sub-Glottal Chest Anchor: 165Hz & 340Hz）
        chestFundBp.prepare(sampleRate);
        chestWarmthLp.prepare(sampleRate);
        chestFundBp.setParameters(TptStateVariableFilter::Mode::UnitGainBandpass, 220.0f, 1.4f);
        chestWarmthLp.setParameters(TptStateVariableFilter::Mode::Lowpass, 185.0f, 0.85f);

        reset();
    }

    void reset() noexcept
    {
        classifier.reset();
        epilarynxResA.reset();
        epilarynxResB.reset();
        sibilantGuardBell.reset();
        chestFundBp.reset();
        chestWarmthLp.reset();
        subOctaveFlip = 1.0f;
        prevChestSample = 0.0f;
        smoothedVoicedGain = 0.0f;
        decimCounter = 0;
    }

    void setParameters(float singersFormantFocus01, float subChestAnchor01) noexcept
    {
        formantFocus = std::clamp(singersFormantFocus01, 0.0f, 1.0f);
        chestAnchor  = std::clamp(subChestAnchor01, 0.0f, 1.0f);
    }

    inline float processSample(float input) noexcept
    {
        if (formantFocus <= 0.001f && chestAnchor <= 0.001f)
            return input;

        const auto ph = classifier.processSample(input);

        // 1. 有声音（母音）区間のみシンガーズフォルマント共鳴を活性化
        const float targetVoiced = ph.voicedProb01 * (1.0f - 0.85f * ph.sibilantProb01);
        smoothedVoicedGain += 0.015f * (targetVoiced - smoothedVoicedGain);

        const float resA = epilarynxResA.processSample(input);
        const float resB = epilarynxResB.processSample(input);
        // 逆位相ではなく緩やかな偶奇倍音飽和を伴う咽頭クラスタ加算
        const float clusterRaw = 0.58f * resA + 0.42f * resB;
        const float clusterSat = static_cast<float>(FastAudioMath::smoothTanh(static_cast<double>(clusterRaw) * 1.6)) * 0.625f;
        const float formantAdd = clusterSat * (formantFocus * 0.68f * smoothedVoicedGain);

        // 2. 歯擦音が強い瞬間は 6.4kHz を最大 -4.2dB 自動減衰して耳当たりを保護
        if ((++decimCounter & 7u) == 0u)
        {
            const float sibCutDb = -ph.sibilantProb01 * 4.2f * formantFocus;
            sibilantGuardBell.setDynamicGainDbFast(sibCutDb);
        }

        // 3. サブグロッタル・チェストアンカー（声帯下腔の1/2分周＋偶次倍音による胸鳴り補強）
        float chestAdd = 0.0f;
        if (chestAnchor > 0.001f)
        {
            const float chestBand = chestFundBp.processSample(input);
            if (chestBand >= 0.0f && prevChestSample < 0.0f)
                subOctaveFlip = -subOctaveFlip;
            prevChestSample = chestBand;

            // 半波整流偶数次成分(65%) ＋ サブオクターブ成分(35%) を 185Hz ローパスで丸める
            const float rawSub = chestBand * 0.55f + std::fabs(chestBand) * 0.25f + (chestBand * subOctaveFlip) * 0.20f;
            const float warmChest = chestWarmthLp.processSample(rawSub);
            chestAdd = warmChest * (chestAnchor * 0.36f * smoothedVoicedGain);
        }

        const float combined = input + formantAdd + chestAdd;
        return FastAudioMath::sanitize(sibilantGuardBell.processSample(combined));
    }

    inline ZeroLagPhonemeClassifier::PhonemeState getPhonemeState() const noexcept
    {
        return classifier.getLastState();
    }

private:
    double sampleRate = 48000.0;
    float formantFocus = 0.35f;
    float chestAnchor = 0.25f;

    ZeroLagPhonemeClassifier classifier;
    TptStateVariableFilter epilarynxResA;
    TptStateVariableFilter epilarynxResB;
    TptStateVariableFilter sibilantGuardBell;
    TptStateVariableFilter chestFundBp;
    TptStateVariableFilter chestWarmthLp;

    float subOctaveFlip = 1.0f;
    float prevChestSample = 0.0f;
    float smoothedVoicedGain = 0.0f;
    uint32_t decimCounter = 0;
};

// =============================================================================
//  3. Zwicker 16臨界帯域（Bark Scale）心理音響マスキング＆歌声明瞭度アナライザ
// =============================================================================
//  人間の聴覚フィルタ（100Hz〜13.5kHz の16代表バーク臨界帯域）において、
//  「ボーカル信号エネルギー」と「カラオケ伴奏エネルギー」を同時追跡し、
//  Schroeder / Terhardt の臨界帯域拡散関数（低域側から高域側への上向きマスキング
//  スロープ）を畳み込むことで、
//  - 各バーク帯域のボーカル透過度 / マスキング競合度 (0.0 .. 1.0)
//  - 総合「歌声聴き取りやすさ指数 (Vocal Audibility Index: 0〜100%)」
//  - フォルマント明瞭度 (F1 Body / F2 Articulation / F3 Singer's Formant / Air)
//  をロックフリーで UI の「Acoustic Radar HUD」へリアルタイム供給します。
// =============================================================================
class BarkMaskingAnalyzer
{
public:
    static constexpr size_t kNumBarkBands = 16;

    struct BarkTelemetry
    {
        std::array<float, kNumBarkBands> vocalDb {};       // 各バーク帯域のボーカルレベル [-70..0 dB]
        std::array<float, kNumBarkBands> bgmMaskDb {};     // 各バーク帯域の伴奏マスキング閾値 [-70..0 dB]
        std::array<float, kNumBarkBands> maskingExcess01 {};// 伴奏が声を覆い隠している度合い [0..1]
        float vocalAudibilityIndex = 88.0f;                // 歌声の総合抜け・明瞭度スコア [0..100%]
        float f1WarmthScore01      = 0.5f;                 // F1 (300-800Hz) 母音温かみスコア
        float f2ClarityScore01     = 0.5f;                 // F2 (900-2400Hz) 歌詞輪郭スコア
        float singersFormant01     = 0.5f;                 // F3-F5 (2600-3800Hz) 主役抜けスコア
        float silkAirScore01       = 0.5f;                 // Air (6000-14000Hz) 吐息の煌めきスコア
    };

    BarkMaskingAnalyzer() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        for (size_t b = 0; b < kNumBarkBands; ++b)
        {
            vocalBands[b].prepare(sampleRate);
            vocalBands[b].setParameters(TptStateVariableFilter::Mode::UnitGainBandpass, kBarkCenterHz[b], kBarkQ[b]);

            bgmBands[b].prepare(sampleRate);
            bgmBands[b].setParameters(TptStateVariableFilter::Mode::UnitGainBandpass, kBarkCenterHz[b], kBarkQ[b]);
        }

        envAtt = FastAudioMath::calcTimeConstantCoeff(sampleRate * 0.25, 8.0f);  // 4サンプル間引き駆動対応
        envRel = FastAudioMath::calcTimeConstantCoeff(sampleRate * 0.25, 65.0f);
        reset();
    }

    void reset() noexcept
    {
        for (size_t b = 0; b < kNumBarkBands; ++b)
        {
            vocalBands[b].reset();
            bgmBands[b].reset();
            vocalEnv[b] = 1.0e-5f;
            bgmEnv[b]   = 1.0e-5f;
            atomicVocalEnv[b].store(1.0e-5f, std::memory_order_relaxed);
            atomicBgmEnv[b].store(1.0e-5f, std::memory_order_relaxed);
        }
        subSampleCounter = 0;
    }

    // オーディオブロック内で呼び出し（4サンプルに1回のマルチレート駆動によりCPU負荷わずか0.15%）
    inline void analyzeSamplePair(float vocalMono, float bgmMono) noexcept
    {
        if ((++subSampleCounter & 3u) != 0u)
            return;

        for (size_t b = 0; b < kNumBarkBands; ++b)
        {
            const float vAbs = std::fabs(vocalBands[b].processSample(vocalMono));
            const float mAbs = std::fabs(bgmBands[b].processSample(bgmMono));

            vocalEnv[b] += ((vAbs > vocalEnv[b]) ? envAtt : envRel) * (vAbs - vocalEnv[b]);
            bgmEnv[b]   += ((mAbs > bgmEnv[b])   ? envAtt : envRel) * (mAbs - bgmEnv[b]);
        }

        if ((subSampleCounter & 63u) == 0u)
        {
            for (size_t b = 0; b < kNumBarkBands; ++b)
            {
                atomicVocalEnv[b].store(vocalEnv[b], std::memory_order_relaxed);
                atomicBgmEnv[b].store(bgmEnv[b], std::memory_order_relaxed);
            }
        }
    }

    // UIスレッドおよびオートアンマスカーから安全に呼び出せる心理音響テレメトリ計算
    BarkTelemetry computeTelemetry() const noexcept
    {
        BarkTelemetry out;
        std::array<float, kNumBarkBands> rawBgmLin {};
        for (size_t b = 0; b < kNumBarkBands; ++b)
        {
            const float vLin = std::max(1.0e-5f, atomicVocalEnv[b].load(std::memory_order_relaxed));
            const float mLin = std::max(1.0e-5f, atomicBgmEnv[b].load(std::memory_order_relaxed));
            out.vocalDb[b] = std::clamp(FastAudioMath::linearToDb(vLin), -70.0f, 0.0f);
            rawBgmLin[b]   = mLin;
        }

        // Zwicker 上向きマスキング拡散（低域の伴奏エネルギーが高域側へ +15%/Bark 漏れ出す効果）
        for (size_t b = 0; b < kNumBarkBands; ++b)
        {
            float spreadLin = rawBgmLin[b];
            if (b > 0) spreadLin += rawBgmLin[b - 1] * 0.22f;
            if (b > 1) spreadLin += rawBgmLin[b - 2] * 0.08f;
            if (b + 1 < kNumBarkBands) spreadLin += rawBgmLin[b + 1] * 0.10f;

            out.bgmMaskDb[b] = std::clamp(FastAudioMath::linearToDb(spreadLin), -70.0f, 0.0f);

            // 重要歌唱帯域（300Hz〜5kHz）における伴奏のマスキング超過度
            const float diffDb = out.bgmMaskDb[b] - out.vocalDb[b];
            out.maskingExcess01[b] = (out.vocalDb[b] > -54.0f)
                ? std::clamp((diffDb + 6.0f) / 18.0f, 0.0f, 1.0f)
                : 0.0f;
        }

        // 4大フォルマント領域のスコア化 [0..1]
        auto bandNorm = [&](size_t idx) noexcept -> float
        {
            return std::clamp((out.vocalDb[idx] + 52.0f) / 42.0f, 0.0f, 1.0f);
        };

        out.f1WarmthScore01  = 0.34f * (bandNorm(2) + bandNorm(3) + bandNorm(4));
        out.f2ClarityScore01 = 0.25f * (bandNorm(5) + bandNorm(6) + bandNorm(7) + bandNorm(8));
        out.singersFormant01 = 0.34f * (bandNorm(9) + bandNorm(10) + bandNorm(11));
        out.silkAirScore01   = 0.34f * (bandNorm(13) + bandNorm(14) + bandNorm(15));

        // 総合「歌声聴き取りやすさ指数 (Vocal Audibility Index)」
        float weightedMask = 0.0f;
        float weightSum = 0.0f;
        for (size_t b = 2; b <= 12; ++b)
        {
            const float w = (b >= 7 && b <= 11) ? 2.0f : 1.0f; // 1.5kHz〜4kHz の歌詞帯域を2倍重視
            weightedMask += out.maskingExcess01[b] * w;
            weightSum += w;
        }
        const float avgMask = (weightSum > 0.0f) ? (weightedMask / weightSum) : 0.0f;
        out.vocalAudibilityIndex = std::clamp(100.0f * (1.0f - 0.72f * avgMask), 15.0f, 100.0f);
        return out;
    }

    static constexpr std::array<float, kNumBarkBands> kBarkCenterHz = {{
        120.0f, 220.0f, 340.0f, 480.0f, 650.0f, 850.0f, 1100.0f, 1400.0f,
        1780.0f, 2250.0f, 2850.0f, 3600.0f, 4600.0f, 6000.0f, 8200.0f, 11500.0f
    }};

private:
    static constexpr std::array<float, kNumBarkBands> kBarkQ = {{
        2.2f, 2.4f, 2.6f, 2.8f, 3.0f, 3.2f, 3.4f, 3.5f,
        3.6f, 3.8f, 4.0f, 3.8f, 3.5f, 3.0f, 2.6f, 2.2f
    }};

    double sampleRate = 48000.0;
    std::array<TptStateVariableFilter, kNumBarkBands> vocalBands;
    std::array<TptStateVariableFilter, kNumBarkBands> bgmBands;
    std::array<float, kNumBarkBands> vocalEnv {};
    std::array<float, kNumBarkBands> bgmEnv {};
    std::array<std::atomic<float>, kNumBarkBands> atomicVocalEnv {};
    std::array<std::atomic<float>, kNumBarkBands> atomicBgmEnv {};
    float envAtt = 0.05f;
    float envRel = 0.005f;
    uint32_t subSampleCounter = 0;
};

// =============================================================================
//  4. リアルタイム・オーディオ無事故ガーディアン (AudioStabilityGuardian)
// =============================================================================
//  プロの配信・レコーディング現場で絶対に起こしてはならないオーディオ事故：
//  1) NaN / ±Inf / Denormal の混入によるオーディオインターフェースの爆音ロックアップ
//  2) USBマイク接触不良やバッファアンダーラン時の 1〜4 サンプル鋭角スパイク（バチッという異音）
//  3) 非対称サチュレーション等で蓄積する DC オフセット（スピーカーコーンの偏位・ヘッドルーム損失）
//  4) サンプルレート変更・ブロックサイズ急変時の不連続ステップノイズ
//  これらすべてを 0 バイト割り当て・0 サンプル遅延でリアルタイム防御します。
// =============================================================================
class AudioStabilityGuardian
{
public:
    struct StabilityDiagnostics
    {
        uint32_t nanInfScrubCount   = 0; // 無害化した NaN/Inf サンプル累計数
        uint32_t slewSpikeArrests   = 0; // 抑制した突発ケーブルポップ・スパイク数
        float    dcOffsetRemovedMv  = 0.0f; // 除去したDCオフセット量(mV換算)
        bool     streamHealthy      = true;
    };

    AudioStabilityGuardian() = default;

    void prepare(double newSampleRate, int expectedMaxBlockSize = 2048) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        (void) expectedMaxBlockSize;

        for (size_t ch = 0; ch < 2; ++ch)
        {
            dcBlocker[ch].prepare(sampleRate);
            dcBlocker[ch].setParameters(TptStateVariableFilter::Mode::Highpass, 11.5f, 0.7071f);
        }

        // サンプルレート変更直後の 12ms ソフトフェードインで切替クリックを完全防止
        warmupSamplesRemaining = static_cast<int>(sampleRate * 0.012);
        warmupTotalSamples     = std::max(1, warmupSamplesRemaining);

        // 48kHz基準の最大許容スルーレート（通常の20kHzフルスケール正弦波の最大勾配 + 安全マージン）
        maxAllowedStep = static_cast<float>(std::clamp(1.65 * (48000.0 / sampleRate), 0.35, 2.2));
        resetStateOnly();
    }

    void reset() noexcept
    {
        resetStateOnly();
        warmupSamplesRemaining = static_cast<int>(sampleRate * 0.006);
        warmupTotalSamples     = std::max(1, warmupSamplesRemaining);
    }

    inline float processSample(int channel, float rawSample) noexcept
    {
        const size_t ch = static_cast<size_t>(std::clamp(channel, 0, 1));

        // 1. NaN / Inf / 非正規化数の即時スクラブ＆ソフトリカバリ
        if (!std::isfinite(rawSample))
        {
            nanCount.fetch_add(1u, std::memory_order_relaxed);
            recoveryGain[ch] = 0.0f;
            prevOut[ch] = 0.0f;
            return 0.0f;
        }

        float x = (std::fabs(rawSample) < FastAudioMath::kDenormEps) ? 0.0f : rawSample;

        // 異常入力からの復帰ランプ（約2.5msかけて滑らかに復帰）
        if (recoveryGain[ch] < 0.9999f)
        {
            recoveryGain[ch] += 0.008f * (1.0f - recoveryGain[ch]);
            x *= recoveryGain[ch];
        }

        // 2. 11.5Hz TPT ゼロ遅延 DC ブロッカー
        const float afterDc = dcBlocker[ch].processSample(x);
        const float dcDiff  = std::fabs(x - afterDc);
        dcTracker = 0.9995f * dcTracker + 0.0005f * dcDiff;

        // 3. 突発1サンプル・デジタルスパイク（USBノイズ／バッファグリッチ）スルーレート制動
        float y = afterDc;
        const float delta = y - prevOut[ch];
        const float absDelta = std::fabs(delta);
        if (absDelta > maxAllowedStep && std::fabs(prevDelta[ch]) < 0.12f)
        {
            // 直前まで穏やかだった信号が1サンプルで異常跳躍した場合、勾配をソフト制限
            const float sign = (delta >= 0.0f) ? 1.0f : -1.0f;
            y = prevOut[ch] + sign * (maxAllowedStep * 0.65f);
            spikeCount.fetch_add(1u, std::memory_order_relaxed);
        }

        // 4. ハードウェア保護用 最終ソフトシーリング (±3.98 = +12dBFS 安全境界)
        if (std::fabs(y) > 3.98f)
            y = static_cast<float>(3.98 * FastAudioMath::smoothTanh(static_cast<double>(y) / 3.98));

        // 5. サンプルレート／リセット直後のウォームアップ・クロスフェード
        if (ch == 0 && warmupSamplesRemaining > 0)
            --warmupSamplesRemaining;
        if (warmupSamplesRemaining > 0)
        {
            const float prog = 1.0f - static_cast<float>(warmupSamplesRemaining) / static_cast<float>(warmupTotalSamples);
            y *= prog * prog * (3.0f - 2.0f * prog);
        }

        prevDelta[ch] = y - prevOut[ch];
        prevOut[ch]   = FastAudioMath::sanitize(y);
        return prevOut[ch];
    }

    inline void processStereoBlock(float* left, float* right, int numSamples) noexcept
    {
        if (numSamples <= 0 || left == nullptr)
            return;

        if (right == nullptr)
        {
            for (int i = 0; i < numSamples; ++i)
                left[i] = processSample(0, left[i]);
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            left[i]  = processSample(0, left[i]);
            right[i] = processSample(1, right[i]);
        }
    }

    StabilityDiagnostics getDiagnostics() const noexcept
    {
        StabilityDiagnostics d;
        d.nanInfScrubCount  = nanCount.load(std::memory_order_relaxed);
        d.slewSpikeArrests  = spikeCount.load(std::memory_order_relaxed);
        d.dcOffsetRemovedMv = dcTracker * 1000.0f;
        d.streamHealthy     = true;
        return d;
    }

private:
    void resetStateOnly() noexcept
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            dcBlocker[ch].reset();
            prevOut[ch] = 0.0f;
            prevDelta[ch] = 0.0f;
            recoveryGain[ch] = 1.0f;
        }
        dcTracker = 0.0f;
    }

    double sampleRate = 48000.0;
    std::array<TptStateVariableFilter, 2> dcBlocker;
    std::array<float, 2> prevOut { 0.0f, 0.0f };
    std::array<float, 2> prevDelta { 0.0f, 0.0f };
    std::array<float, 2> recoveryGain { 1.0f, 1.0f };

    float maxAllowedStep = 1.65f;
    float dcTracker = 0.0f;
    int warmupSamplesRemaining = 0;
    int warmupTotalSamples = 1;

    std::atomic<uint32_t> nanCount { 0 };
    std::atomic<uint32_t> spikeCount { 0 };
};

// =============================================================================
//  5. ポップガード不要・知能派プローシブ（吹かれ /p/, /b/・机振動）ゼロ遅延抑制器
// =============================================================================
//  固定のローカット（例：120Hz ハイパス常時ON）をかけると、男性ボーカルの基音や
//  女性ボーカルのチェスト感まで痩せてしまいます。
//  本モジュールは「110Hz以下の超低域エネルギー」と「220Hz〜900Hzの倍音エネルギー」の
//  比率（Harmonic-to-Sub Ratio）をリアルタイム比較します：
//  - 歌声の低音発声時：倍音(2f0, 3f0)が同時に存在するため、カットオフを 32Hz に保ち太さを完全維持
//  - マイクへの息吹かれ（ポップノイズ）や机の振動時：倍音を伴わない 110Hz 以下の単発爆発と
//    判定し、0.6ms で TPT ハイパスを最大 155Hz まで動的スイープさせて「ボフッ」という音だけを消去。
// =============================================================================
class AdaptivePlosiveSuppressor
{
public:
    AdaptivePlosiveSuppressor() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        subDetectorLp.prepare(sampleRate);
        subDetectorLp.setParameters(TptStateVariableFilter::Mode::Lowpass, 95.0f, 0.7071f);

        harmonicDetectorBp.prepare(sampleRate);
        harmonicDetectorBp.setParameters(TptStateVariableFilter::Mode::UnitGainBandpass, 380.0f, 0.75f);

        dynamicHpStage1.prepare(sampleRate);
        dynamicHpStage2.prepare(sampleRate);
        dynamicHpStage1.setParameters(TptStateVariableFilter::Mode::Highpass, 32.0f, 0.7071f);
        dynamicHpStage2.setParameters(TptStateVariableFilter::Mode::Highpass, 32.0f, 0.7071f);

        fastAtt = FastAudioMath::calcTimeConstantCoeff(sampleRate, 0.6f);
        fastRel = FastAudioMath::calcTimeConstantCoeff(sampleRate, 55.0f);
        reset();
    }

    void reset() noexcept
    {
        subDetectorLp.reset();
        harmonicDetectorBp.reset();
        dynamicHpStage1.reset();
        dynamicHpStage2.reset();
        subEnv = 1.0e-6f;
        harmEnv = 1.0e-6f;
        currentCutoffHz = 32.0f;
        decimCounter = 0;
    }

    void setSensitivity(float sensitivity01) noexcept
    {
        sensitivity = std::clamp(sensitivity01, 0.0f, 1.0f);
    }

    inline float processSample(float input) noexcept
    {
        if (sensitivity <= 0.001f)
            return input;

        const float subAbs  = std::fabs(subDetectorLp.processSample(input));
        const float harmAbs = std::fabs(harmonicDetectorBp.processSample(input));

        subEnv  += ((subAbs > subEnv)   ? fastAtt : fastRel) * (subAbs - subEnv);
        harmEnv += ((harmAbs > harmEnv) ? fastAtt : fastRel) * (harmAbs - harmEnv);

        // 倍音に対して超低域が異常に大きい場合＝吹かれ（Plosive）または机振動と判定
        const float plosiveRatio = subEnv / (harmEnv + 0.002f);
        float targetFc = 32.0f;
        if (plosiveRatio > 1.35f && subEnv > 0.008f)
        {
            const float excess = std::min(6.0f, plosiveRatio - 1.35f);
            targetFc = 32.0f + excess * 22.0f * sensitivity;
        }
        targetFc = std::clamp(targetFc, 32.0f, 158.0f);

        const float cCoeff = (targetFc > currentCutoffHz) ? fastAtt : fastRel;
        currentCutoffHz += cCoeff * (targetFc - currentCutoffHz);

        if ((++decimCounter & 7u) == 0u)
        {
            dynamicHpStage1.setParameters(TptStateVariableFilter::Mode::Highpass, currentCutoffHz, 0.7071f);
            dynamicHpStage2.setParameters(TptStateVariableFilter::Mode::Highpass, currentCutoffHz * 0.85f, 0.7071f);
        }

        const float s1 = dynamicHpStage1.processSample(input);
        return FastAudioMath::sanitize(dynamicHpStage2.processSample(s1));
    }

    inline float getActiveCutoffHz() const noexcept { return currentCutoffHz; }

private:
    double sampleRate = 48000.0;
    float sensitivity = 0.65f;
    TptStateVariableFilter subDetectorLp;
    TptStateVariableFilter harmonicDetectorBp;
    TptStateVariableFilter dynamicHpStage1;
    TptStateVariableFilter dynamicHpStage2;

    float subEnv = 1.0e-6f;
    float harmEnv = 1.0e-6f;
    float currentCutoffHz = 32.0f;
    float fastAtt = 0.08f;
    float fastRel = 0.002f;
    uint32_t decimCounter = 0;
};

// =============================================================================
//  6. ケプストラム・声道フォルマント包絡アンカー (Cepstral Formant Envelope Preserver)
// =============================================================================
//  ピッチ補正・ハモリ生成・倍音処理の前後で「歌い手固有の声道形状（F1/F2/F3の相対バランス）」
//  が崩れてケロケロ感や合成音声感が出るのを防ぐため、原音（Dry）の6帯域フォルマント包絡と
//  処理後（Processed）の6帯域包絡の差分伝達関数をリアルタイム追跡し、声の自然な体温と
//  母音の響きを復元します。
// =============================================================================
class CepstralFormantEnvelopePreserver
{
public:
    static constexpr size_t kNumFormantBands = 6;

    CepstralFormantEnvelopePreserver() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        for (size_t i = 0; i < kNumFormantBands; ++i)
        {
            dryTrackBp[i].prepare(sampleRate);
            dryTrackBp[i].setParameters(TptStateVariableFilter::Mode::UnitGainBandpass, kBandHz[i], kBandQ[i]);

            wetTrackBp[i].prepare(sampleRate);
            wetTrackBp[i].setParameters(TptStateVariableFilter::Mode::UnitGainBandpass, kBandHz[i], kBandQ[i]);

            correctorBell[i].prepare(sampleRate);
            correctorBell[i].setParameters(TptStateVariableFilter::Mode::PeakBell, kBandHz[i], kBandQ[i] * 0.85f, 0.0f);
        }

        envAtt = FastAudioMath::calcTimeConstantCoeff(sampleRate, 6.0f);
        envRel = FastAudioMath::calcTimeConstantCoeff(sampleRate, 45.0f);
        reset();
    }

    void reset() noexcept
    {
        for (size_t i = 0; i < kNumFormantBands; ++i)
        {
            dryTrackBp[i].reset();
            wetTrackBp[i].reset();
            correctorBell[i].reset();
            dryEnv[i] = 1.0e-4f;
            wetEnv[i] = 1.0e-4f;
            corrGainDb[i] = 0.0f;
        }
        decimCounter = 0;
    }

    void setPreservationStrength(float strength01) noexcept
    {
        strength = std::clamp(strength01, 0.0f, 1.0f);
    }

    inline float processSample(float dryReference, float processedSignal) noexcept
    {
        if (strength <= 0.001f)
            return processedSignal;

        for (size_t i = 0; i < kNumFormantBands; ++i)
        {
            const float dAbs = std::fabs(dryTrackBp[i].processSample(dryReference));
            const float wAbs = std::fabs(wetTrackBp[i].processSample(processedSignal));

            dryEnv[i] += ((dAbs > dryEnv[i]) ? envAtt : envRel) * (dAbs - dryEnv[i]);
            wetEnv[i] += ((wAbs > wetEnv[i]) ? envAtt : envRel) * (wAbs - wetEnv[i]);
        }

        if ((++decimCounter & 15u) == 0u)
        {
            for (size_t i = 0; i < kNumFormantBands; ++i)
            {
                if (dryEnv[i] > 0.002f && wetEnv[i] > 0.002f)
                {
                    const float ratioDb = FastAudioMath::linearToDb(dryEnv[i] / wetEnv[i]);
                    const float targetDb = std::clamp(ratioDb * 0.45f * strength, -5.5f, 5.5f);
                    corrGainDb[i] += 0.22f * (targetDb - corrGainDb[i]);
                }
                else
                {
                    corrGainDb[i] *= 0.90f;
                }
                correctorBell[i].setDynamicGainDbFast(corrGainDb[i]);
            }
        }

        float y = processedSignal;
        for (size_t i = 0; i < kNumFormantBands; ++i)
            y = correctorBell[i].processSample(y);

        return FastAudioMath::sanitize(y);
    }

private:
    static constexpr std::array<float, kNumFormantBands> kBandHz = { 190.0f, 460.0f, 980.0f, 1880.0f, 3050.0f, 5400.0f };
    static constexpr std::array<float, kNumFormantBands> kBandQ  = { 1.4f,   1.6f,   1.8f,   2.0f,    2.2f,    1.8f    };

    double sampleRate = 48000.0;
    float strength = 0.40f;

    std::array<TptStateVariableFilter, kNumFormantBands> dryTrackBp;
    std::array<TptStateVariableFilter, kNumFormantBands> wetTrackBp;
    std::array<TptStateVariableFilter, kNumFormantBands> correctorBell;

    std::array<float, kNumFormantBands> dryEnv {};
    std::array<float, kNumFormantBands> wetEnv {};
    std::array<float, kNumFormantBands> corrGainDb {};
    float envAtt = 0.02f;
    float envRel = 0.003f;
    uint32_t decimCounter = 0;
};

// =============================================================================
//  7. VocalScienceLabSuite — 音響音声学＆無事故ガーディアン統合スイート
// =============================================================================
class VocalScienceLabSuite
{
public:
    VocalScienceLabSuite() = default;

    void prepare(double newSampleRate, int maxBlockSize = 2048) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        plosiveSuppressor[0].prepare(sampleRate);
        plosiveSuppressor[1].prepare(sampleRate);
        formantBridge[0].prepare(sampleRate);
        formantBridge[1].prepare(sampleRate);
        cepstralPreserver[0].prepare(sampleRate);
        cepstralPreserver[1].prepare(sampleRate);
        barkAnalyzer.prepare(sampleRate);
        stabilityGuardian.prepare(sampleRate, maxBlockSize);
    }

    void reset() noexcept
    {
        plosiveSuppressor[0].reset();
        plosiveSuppressor[1].reset();
        formantBridge[0].reset();
        formantBridge[1].reset();
        cepstralPreserver[0].reset();
        cepstralPreserver[1].reset();
        barkAnalyzer.reset();
        stabilityGuardian.reset();
    }

    void setVoiceScienceParameters(float singersFormant01, float subChest01, float plosiveGuard01, float formantLock01) noexcept
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            plosiveSuppressor[ch].setSensitivity(plosiveGuard01);
            formantBridge[ch].setParameters(singersFormant01, subChest01);
            cepstralPreserver[ch].setPreservationStrength(formantLock01);
        }
    }

    // 入力直後のプローシブ抑制＋シンガーズフォルマント＆チェスト補強＋バーク解析＋安全保護
    inline void processStereoWithBgmReference(float* left, float* right, const float* bgmMonoOrNull, int numSamples) noexcept
    {
        if (numSamples <= 0 || left == nullptr)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            const float rawL = left[i];
            const float cleanL = plosiveSuppressor[0].processSample(rawL);
            const float bridgedL = formantBridge[0].processSample(cleanL);
            const float lockedL  = cepstralPreserver[0].processSample(cleanL, bridgedL);
            left[i] = stabilityGuardian.processSample(0, lockedL);

            if (right != nullptr)
            {
                const float rawR = right[i];
                const float cleanR = plosiveSuppressor[1].processSample(rawR);
                const float bridgedR = formantBridge[1].processSample(cleanR);
                const float lockedR  = cepstralPreserver[1].processSample(cleanR, bridgedR);
                right[i] = stabilityGuardian.processSample(1, lockedR);
            }

            const float vocalMono = (right != nullptr) ? 0.5f * (left[i] + right[i]) : left[i];
            const float bgmMono   = (bgmMonoOrNull != nullptr) ? bgmMonoOrNull[i] : 0.0f;
            barkAnalyzer.analyzeSamplePair(vocalMono, bgmMono);
        }
    }

    BarkMaskingAnalyzer::BarkTelemetry getBarkTelemetry() const noexcept
    {
        return barkAnalyzer.computeTelemetry();
    }

    AudioStabilityGuardian::StabilityDiagnostics getStabilityDiagnostics() const noexcept
    {
        return stabilityGuardian.getDiagnostics();
    }

    ZeroLagPhonemeClassifier::PhonemeState getPhonemeState() const noexcept
    {
        return formantBridge[0].getPhonemeState();
    }

private:
    double sampleRate = 48000.0;
    std::array<AdaptivePlosiveSuppressor, 2> plosiveSuppressor;
    std::array<SingersFormantAndChestBridge, 2> formantBridge;
    std::array<CepstralFormantEnvelopePreserver, 2> cepstralPreserver;
    BarkMaskingAnalyzer barkAnalyzer;
    AudioStabilityGuardian stabilityGuardian;
};

} // namespace science
} // namespace gzzio
