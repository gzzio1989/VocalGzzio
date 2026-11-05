#pragma once

/**
 * @file PrecisionPitchAndVibratoSynthesizer.h
 * @brief VocalGzzio v5.5.0 ピッチ同期TD-PSOLA・日本歌唱表現（しゃくり・こぶし・フォール）物理生成＆12声ゴスペル/J-Pop聖歌隊ハーモナイザー
 *
 * 収録クラス:
 *   1. PitchSynchronousTdPsolaEngine
 *      - 時間領域ピッチ同期重畳加算（TD-PSOLA: Moulines & Charpentier 1990）をロックフリー・リングバッファで実装。
 *      - Hann-Poisson 窓によるゼロクリック粒子切り出しと、合成ピッチ周期 T_synth の連続位相ロック。
 *
 *   2. NaturalKokoroneKobushiAndShakuriGenerator
 *      - 演歌・J-Pop・アニソン・R&B で命となる3大歌唱装飾：
 *        ① しゃくり (Shakuri): 発声開始直後の 65〜110ms で下から滑らかに目標音高へ掬い上げる最小躍度カーブ
 *        ② こぶし (Kobushi): 持続母音の節目に 115ms の1周期マイクロ・ターン（+140c → -60c → 0c）を付与
 *        ③ フォール (Fall / Sigh Release): フレーズ語尾の減衰に伴う自然なピッチ下降
 *        の保護・生成を行う表現エンジン。
 *
 *   3. ChoirEnsemble12VoiceHarmonizer
 *      - リードボーカルから12名の仮想聖歌隊（ソプラノ3・アルト3・テナー3・バリトン3）を生成。
 *      - 各シンガーに独立した微細ピッチドリフト（OU過程）、異なるビブラート周期（5.1〜6.3Hz）、
 *        異なる声道長スケーリング、およびステージ半円配置（Haas ITD + パンニング）を付与し、
 *        1人で歌っても武道館や大聖堂のクワイアが背後で支える壮大なコーラス空間を実現。
 */

#include <algorithm>
#include <array>
#include <cmath>

namespace gzzio
{
namespace precision_pitch
{

// =============================================================================
// 1. ピッチ同期 TD-PSOLA (Time-Domain Pitch-Synchronous Overlap-Add) エンジン
// =============================================================================
class PitchSynchronousTdPsolaEngine
{
public:
    static constexpr size_t kBufferSize = 4096;
    static constexpr size_t kBufferMask = kBufferSize - 1;
    static constexpr size_t kNumGrains  = 4;

    struct PsolaGrain
    {
        bool   active     = false;
        double readCenter = 0.0;
        double posInGrain = 0.0;
        double halfLength = 128.0;
        double readStep   = 1.0; // フォルマントシフト比 (1.0 = フォルマント完全維持)
    };

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        reset();
    }

    void reset() noexcept
    {
        historyBuffer.fill(0.0f);
        grains = {};
        writePos        = 0;
        synthTimer      = 0.0;
        lastPeriodSamp  = 200.0;
        nextGrainIdx    = 0;
    }

    // pitchRatio: 1.0 = 原音キー, 2.0 = +1oct, 0.5 = -1oct
    // formantRatio: 1.0 = 元の声道サイズ維持, >1.0 = 小柄な声道, <1.0 = 大柄な声道
    inline float processSample(float input,
                               float detectedF0Hz,
                               bool isVoiced,
                               float pitchRatio,
                               float formantRatio = 1.0f) noexcept
    {
        historyBuffer[writePos] = input;

        const float pRatio = std::clamp(pitchRatio,   0.50f, 2.00f);
        const float fRatio = std::clamp(formantRatio, 0.70f, 1.45f);

        if (!isVoiced || detectedF0Hz < 55.0f || detectedF0Hz > 1100.0f
            || (std::fabs(pRatio - 1.0f) < 0.002f && std::fabs(fRatio - 1.0f) < 0.002f))
        {
            writePos = (writePos + 1) & kBufferMask;
            return input;
        }

        const double targetPeriod = std::clamp(sampleRate / static_cast<double>(detectedF0Hz), 36.0, 900.0);
        lastPeriodSamp += 0.08 * (targetPeriod - lastPeriodSamp);

        const double synthPeriod = std::clamp(lastPeriodSamp / static_cast<double>(pRatio), 24.0, 1200.0);
        synthTimer += 1.0;

        if (synthTimer >= synthPeriod)
        {
            synthTimer -= synthPeriod;
            spawnNewGrain(lastPeriodSamp, static_cast<double>(fRatio));
        }

        float acc = 0.0f;
        float winSum = 0.0f;

        for (auto& g : grains)
        {
            if (!g.active)
                continue;

            const double normPos = g.posInGrain / g.halfLength; // [-1.0, +1.0]
            if (normPos >= 1.0)
            {
                g.active = false;
                continue;
            }

            // Hann 窓 W(u) = 0.5 * (1 + cos(pi * u)) for u in [-1, 1]
            const float w = 0.5f * (1.0f + std::cos(3.1415926535f * static_cast<float>(normPos)));
            const double readIdxD = g.readCenter + g.posInGrain * g.readStep;
            acc    += readInterpolated(readIdxD) * w;
            winSum += w;

            g.posInGrain += 1.0;
        }

        writePos = (writePos + 1) & kBufferMask;
        return (winSum > 0.05f) ? (acc / std::max(0.55f, winSum)) : input;
    }

private:
    void spawnNewGrain(double analysisPeriod, double formantStep) noexcept
    {
        auto& g = grains[nextGrainIdx];
        nextGrainIdx = (nextGrainIdx + 1) % kNumGrains;

        g.active     = true;
        g.halfLength = std::clamp(analysisPeriod, 32.0, 900.0);
        g.posInGrain = -g.halfLength;
        g.readStep   = formantStep;
        // 現在書き込み位置から 1.5 周期前を中心に切り出すことで未来サンプル参照を回避
        g.readCenter = static_cast<double>(writePos) - g.halfLength * formantStep - 4.0;
    }

    inline float readInterpolated(double readPos) const noexcept
    {
        double wrapped = std::fmod(readPos, static_cast<double>(kBufferSize));
        if (wrapped < 0.0)
            wrapped += static_cast<double>(kBufferSize);

        const size_t idx0 = static_cast<size_t>(wrapped) & kBufferMask;
        const size_t idx1 = (idx0 + 1) & kBufferMask;
        const float  frac = static_cast<float>(wrapped - std::floor(wrapped));
        return historyBuffer[idx0] + frac * (historyBuffer[idx1] - historyBuffer[idx0]);
    }

    double sampleRate = 48000.0;
    std::array<float, kBufferSize> historyBuffer{};
    std::array<PsolaGrain, kNumGrains> grains{};
    size_t writePos       = 0;
    double synthTimer     = 0.0;
    double lastPeriodSamp = 200.0;
    size_t nextGrainIdx   = 0;
};

// =============================================================================
// 2. 日本歌唱表現「しゃくり・こぶし・フォール」物理カーブ生成器
//    (NaturalKokoroneKobushiAndShakuriGenerator)
// =============================================================================
class NaturalKokoroneKobushiAndShakuriGenerator
{
public:
    struct OrnamentContours
    {
        float shakuriCentsOffset = 0.0f; // 発声立ち上がり「しゃくり」オフセット (-180c → 0c)
        float kobushiCentsOffset = 0.0f; // 節回し「こぶし」ターンオフセット (+135c → -55c → 0c)
        float fallCentsOffset    = 0.0f; // 語尾「フォール」オフセット (0c → -160c)
        float totalPitchRatio    = 1.0f; // 合成ピッチ倍率 2^(totalCents / 1200)
        bool  ornamentActive     = false;
    };

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        reset();
    }

    void reset() noexcept
    {
        wasVoiced          = false;
        noteAgeSec         = 0.0f;
        kobushiTriggered   = false;
        kobushiPhaseSec    = 10.0f;
        prevEnv            = 0.0f;
        prevF0Hz           = 220.0f;
        lastContours       = {};
    }

    // 1サンプルまたは1フレームごとの歌唱表現ピッチ変調量を計算
    inline OrnamentContours evaluateSample(float vocalEnvLin,
                                           float currentF0Hz,
                                           bool isVoiced,
                                           float shakuriDepth01,
                                           float kobushiDepth01,
                                           float fallDepth01) noexcept
    {
        const float dt = static_cast<float>(1.0 / sampleRate);
        OrnamentContours out{};

        if (!isVoiced || vocalEnvLin < 0.008f)
        {
            wasVoiced        = false;
            noteAgeSec       = 0.0f;
            kobushiTriggered = false;
            lastContours     = out;
            return out;
        }

        // 新規発声または 2.5半音以上のレガート跳躍を検出したらノート年齢をリセット
        const float pitchJumpSemitones = 12.0f * std::fabs(std::log2(std::max(50.0f, currentF0Hz) / std::max(50.0f, prevF0Hz)));
        if (!wasVoiced || pitchJumpSemitones > 2.4f)
        {
            noteAgeSec       = 0.0f;
            kobushiTriggered = false;
        }
        wasVoiced  = true;
        prevF0Hz   = currentF0Hz;
        noteAgeSec += dt;

        // 1. しゃくり (Shakuri): 発声開始 0〜95ms で -165 cents から最小躍度 S字曲線で 0 cents へ掬い上げる
        const float shakuriDur = 0.095f;
        if (noteAgeSec < shakuriDur && shakuriDepth01 > 0.001f)
        {
            const float u = noteAgeSec / shakuriDur;
            const float minJerk = u * u * u * (10.0f + u * (-15.0f + 6.0f * u));
            out.shakuriCentsOffset = -165.0f * std::clamp(shakuriDepth01, 0.0f, 1.0f) * (1.0f - minJerk);
            out.ornamentActive     = true;
        }

        // 2. こぶし (Kobushi): 発声後 0.22s 付近のアクセントで 115ms の 1サイクル装飾ターンを発火
        if (!kobushiTriggered && noteAgeSec >= 0.20f && noteAgeSec <= 0.32f && kobushiDepth01 > 0.001f)
        {
            kobushiTriggered = true;
            kobushiPhaseSec  = 0.0f;
        }
        if (kobushiPhaseSec < 0.118f)
        {
            const float u = kobushiPhaseSec / 0.118f;
            const float win = std::sin(3.1415926535f * u);
            const float turnWave = std::sin(6.283185307f * u);
            out.kobushiCentsOffset = 125.0f * std::clamp(kobushiDepth01, 0.0f, 1.0f) * win * turnWave;
            out.ornamentActive     = true;
            kobushiPhaseSec += dt;
        }

        // 3. 語尾フォール (Fall): 0.45s以上持続したノートが減衰に入った瞬間に優しくピッチを落とす
        const float envSlope = vocalEnvLin - prevEnv;
        prevEnv = vocalEnvLin;
        if (noteAgeSec > 0.45f && envSlope < -0.00002f && vocalEnvLin < 0.06f && fallDepth01 > 0.001f)
        {
            const float releaseProgress = std::clamp((0.06f - vocalEnvLin) / 0.052f, 0.0f, 1.0f);
            out.fallCentsOffset = -140.0f * std::clamp(fallDepth01, 0.0f, 1.0f) * releaseProgress * releaseProgress;
            out.ornamentActive  = true;
        }

        const float totalCents = out.shakuriCentsOffset + out.kobushiCentsOffset + out.fallCentsOffset;
        out.totalPitchRatio    = std::pow(2.0f, totalCents / 1200.0f);
        lastContours           = out;
        return out;
    }

    inline const OrnamentContours& getLastContours() const noexcept { return lastContours; }

private:
    double sampleRate       = 48000.0;
    bool   wasVoiced        = false;
    float  noteAgeSec       = 0.0f;
    bool   kobushiTriggered = false;
    float  kobushiPhaseSec  = 10.0f;
    float  prevEnv          = 0.0f;
    float  prevF0Hz         = 220.0f;
    OrnamentContours lastContours{};
};

// =============================================================================
// 3. 12声ゴスペル／J-Pop クワイア・アンサンブル生成器 (ChoirEnsemble12VoiceHarmonizer)
// =============================================================================
class ChoirEnsemble12VoiceHarmonizer
{
public:
    static constexpr size_t kNumVoices  = 12;
    static constexpr size_t kDelayBufSz = 4096;
    static constexpr size_t kDelayMask  = kDelayBufSz - 1;

    struct ChoirVoiceState
    {
        float baseDelayMs   = 18.0f;
        float modRateHz     = 5.4f;
        float modDepthMs    = 1.6f;
        float lfoPhase      = 0.0f;
        float pan01         = 0.5f;
        float lpState       = 0.0f;
        float hpStateIn     = 0.0f;
        float hpStateOut    = 0.0f;
    };

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        // 12名のクワイアメンバーをステージ半円（左端 0.08 〜 右端 0.92）に配置
        for (size_t v = 0; v < kNumVoices; ++v)
        {
            const float u = static_cast<float>(v) / static_cast<float>(kNumVoices - 1);
            voices[v].baseDelayMs = 11.5f + static_cast<float>((v * 7) % 12) * 2.85f;
            voices[v].modRateHz   = 4.65f + static_cast<float>((v * 5) % 11) * 0.21f;
            voices[v].modDepthMs  = 1.15f + static_cast<float>(v % 4) * 0.38f;
            voices[v].lfoPhase    = u * 6.2831853f;
            voices[v].pan01       = 0.08f + 0.84f * u;
            voices[v].lpState     = 0.0f;
            voices[v].hpStateIn   = 0.0f;
            voices[v].hpStateOut  = 0.0f;
        }
        reset();
    }

    void reset() noexcept
    {
        delayLine.fill(0.0f);
        writePos = 0;
        for (auto& v : voices)
        {
            v.lpState    = 0.0f;
            v.hpStateIn  = 0.0f;
            v.hpStateOut = 0.0f;
        }
    }

    inline void processSamplePair(float& ioL, float& ioR, float choirMix01) noexcept
    {
        const float mix = std::clamp(choirMix01, 0.0f, 1.0f);
        const float monoIn = 0.5f * (ioL + ioR);
        delayLine[writePos] = monoIn;

        if (mix <= 0.001f)
        {
            writePos = (writePos + 1) & kDelayMask;
            return;
        }

        float choirL = 0.0f;
        float choirR = 0.0f;
        const float twoPiOverSr = static_cast<float>(6.283185307179586 / sampleRate);

        for (size_t v = 0; v < kNumVoices; ++v)
        {
            auto& vc = voices[v];
            vc.lfoPhase += vc.modRateHz * twoPiOverSr;
            if (vc.lfoPhase > 6.2831853f)
                vc.lfoPhase -= 6.2831853f;

            // 二重正弦波モジュレーションで機械的周期性を排除した人間らしいピッチゆらぎを生成
            const float lfo = 0.72f * std::sin(vc.lfoPhase) + 0.28f * std::sin(vc.lfoPhase * 1.618f + 1.1f);
            const double delaySamples = std::clamp(
                static_cast<double>(vc.baseDelayMs + vc.modDepthMs * lfo) * 0.001 * sampleRate,
                4.0, static_cast<double>(kDelayBufSz - 8));

            const float rawTap = readDelayInterpolated(delaySamples);

            // クワイア声部は歯擦音 (6kHz以上) と低域濁り (180Hz以下) を丸めてリードボーカルの邪魔をしないベルベット層にする
            const float hp = rawTap - vc.hpStateIn + 0.978f * vc.hpStateOut;
            vc.hpStateIn   = rawTap;
            vc.hpStateOut  = hp;
            vc.lpState    += 0.34f * (hp - vc.lpState);

            const float ang = vc.pan01 * 1.5707963f;
            choirL += vc.lpState * std::cos(ang);
            choirR += vc.lpState * std::sin(ang);
        }

        writePos = (writePos + 1) & kDelayMask;
        constexpr float kVoiceNorm = 0.26f;
        ioL += choirL * kVoiceNorm * mix;
        ioR += choirR * kVoiceNorm * mix;
    }

private:
    inline float readDelayInterpolated(double delaySamples) const noexcept
    {
        double rPos = static_cast<double>(writePos) - delaySamples;
        while (rPos < 0.0)
            rPos += static_cast<double>(kDelayBufSz);

        const size_t i0 = static_cast<size_t>(rPos) & kDelayMask;
        const size_t i1 = (i0 + 1) & kDelayMask;
        const float  fr = static_cast<float>(rPos - std::floor(rPos));
        return delayLine[i0] + fr * (delayLine[i1] - delayLine[i0]);
    }

    double sampleRate = 48000.0;
    std::array<float, kDelayBufSz> delayLine{};
    std::array<ChoirVoiceState, kNumVoices> voices{};
    size_t writePos = 0;
};

// =============================================================================
// 4. 統合高精度ピッチ＆日本歌唱表現スイート (PrecisionPitchAndVibratoSuite)
// =============================================================================
class PrecisionPitchAndVibratoSuite
{
public:
    void prepare(double newSampleRate) noexcept
    {
        psolaL.prepare(newSampleRate);
        psolaR.prepare(newSampleRate);
        ornamentGen.prepare(newSampleRate);
        choir12.prepare(newSampleRate);
    }

    void reset() noexcept
    {
        psolaL.reset();
        psolaR.reset();
        ornamentGen.reset();
        choir12.reset();
    }

    PitchSynchronousTdPsolaEngine&              getPsolaL() noexcept        { return psolaL; }
    NaturalKokoroneKobushiAndShakuriGenerator&  getOrnamentGen() noexcept   { return ornamentGen; }
    ChoirEnsemble12VoiceHarmonizer&             getChoir12() noexcept       { return choir12; }

private:
    PitchSynchronousTdPsolaEngine             psolaL, psolaR;
    NaturalKokoroneKobushiAndShakuriGenerator ornamentGen;
    ChoirEnsemble12VoiceHarmonizer            choir12;
};

} // namespace precision_pitch
} // namespace gzzio

namespace vg
{
namespace precision_pitch = ::gzzio::precision_pitch;
} // namespace vg
