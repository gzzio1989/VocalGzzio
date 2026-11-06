#pragma once
// ==============================================================================
//  ChorusClimaxAndArrangementLiftEngine.h
//  VocalGzzio — 楽曲展開（Aメロ静寂 → Bメロ高揚 → サビ絶唱クライマックス → 落ちサビ吐息）自動演出 ＆ ステレオM/S翼展開エンジン
//
//  【人間の心を揺さぶるドラマツルギー：一流ミキシングエンジニアの「手書きオートメーション」を自動化】
//   グラミー賞や日本のトップチャートを手掛けるミキシングエンジニアは、1曲を通して同じ設定のまま放置しない。
//   - **Aメロ（Verse）**: センター定位をタイトに絞り、親密な距離感で語りかけるように聴かせる。
//   - **Bメロ（Pre-Chorus）**: 徐々に高域の抜けと奥行きを開き、サビへの期待感（Anticipation）を高める。
//   - **サビ（Chorus Climax）**: 声量と音域が跳ね上がった瞬間、ボーカルの存在感を +0.85dB リフトし、
//     ステレオSideの空気感を +18% 広げて「視界が一気に開ける感動」を演出する。
//   - **落ちサビ（Drop-Chorus Whisper）**: サビの直後にふっと声量を落とした瞬間、耳元へ一気に引き寄せて
//     聴き手の心臓を鷲掴みにする。
// ==============================================================================

#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>

namespace vg::human
{

enum class SongDramaticSection : int
{
    IntimateVerse       = 0, // Aメロ・静かな語りかけ
    RisingPreChorus     = 1, // Bメロ・サビへ向けたビルドアップ
    SoaringChorusClimax = 2, // サビ・最高潮の開花（ステレオ翼展開＋ゴールデンリフト）
    DropChorusWhisper   = 3  // 落ちサビ・急激な静寂への引き込み（耳元フォーカス）
};

// ==============================================================================
//  1. サビ突入時の高域ステレオ・マイクロ・アンサンブル翼（Chorus Stereo Halo Wing）生成器
// ==============================================================================
class ChorusStereoWingExpander
{
public:
    void prepare (double sampleRate) noexcept
    {
        const double safeFs = std::max (8000.0, sampleRate);
        // 300Hz 以下はモノラル芯を死守し、300Hz 以上のみをステレオ翼として広げるためのハイパス
        sideHp300Coeff = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * 300.0 / safeFs));
        tapL = std::clamp (static_cast<int> (0.0068 * safeFs), 1, 500);
        tapR = std::clamp (static_cast<int> (0.0094 * safeFs), 1, 500);
        reset();
    }

    void reset() noexcept
    {
        wingBufL.fill (0.0f);
        wingBufR.fill (0.0f);
        writePos = 0;
        hpPrev = 0.0f;
        hpState = 0.0f;
    }

    inline float synthesizeChorusSideWing (float midSignal) noexcept
    {
        hpState = sideHp300Coeff * (hpState + midSignal - hpPrev);
        hpPrev  = midSignal;

        wingBufL[static_cast<size_t> (writePos)] = hpState;
        const float d1 = wingBufL[static_cast<size_t> ((writePos - tapL) & kMask)];
        const float d2 = wingBufL[static_cast<size_t> ((writePos - tapR) & kMask)];
        writePos = (writePos + 1) & kMask;

        // LとRに逆相で加算してもモノラル時に完全キャンセル（モノラル互換性100%）のHaas差分信号
        return 0.5f * (d1 - d2);
    }

private:
    static constexpr int kMask = 511;
    std::array<float, 512> wingBufL {};
    std::array<float, 512> wingBufR {};
    int writePos = 0;
    int tapL = 326, tapR = 451;
    float sideHp300Coeff = 0.96f;
    float hpPrev = 0.0f, hpState = 0.0f;
};

// ==============================================================================
//  2. 楽曲展開（Aメロ→Bメロ→サビ→落ちサビ）自動演出統合エンジン
// ==============================================================================
struct ChorusClimaxTelemetry
{
    SongDramaticSection detectedSection        = SongDramaticSection::IntimateVerse;
    float               climaxLiftDb           = 0.0f;
    float               stereoWingExpansionPct = 0.0f;
    float               dramaticArcIntensity01 = 0.0f;
};

class ChorusClimaxAndArrangementLiftEngine
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRateHz = std::max (8000.0, sampleRate);
        wingExpander.prepare (sampleRateHz);

        // 短中期（250ms）と長期（3.5秒）のエネルギー窓で楽曲セクションの盛り上がりを追跡
        phraseEnvCoeff  = static_cast<float> (std::exp (-1.0 / (0.250 * sampleRateHz)));
        sectionEnvCoeff = static_cast<float> (std::exp (-1.0 / (3.500 * sampleRateHz)));
        smoothLiftCoeff = static_cast<float> (std::exp (-1.0 / (0.120 * sampleRateHz)));

        // 高音域歌唱（サビのハイノート）検出用 1.4kHz ハイパス
        const double fcHighNote = 1400.0;
        highNoteHpCoeff = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * fcHighNote / sampleRateHz));

        // 11kHz ゴールデン・エアー・シェルフ用ハイパス
        const double fcAir = std::min (11000.0, sampleRateHz * 0.40);
        airHpCoeff = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * fcAir / sampleRateHz));

        reset();
    }

    void reset() noexcept
    {
        wingExpander.reset();
        phraseEnv  = 0.005f;
        sectionEnv = 0.005f;
        highNoteEnv = 0.001f;
        smoothedClimax01 = 0.0f;
        prevMono = 0.0f;
        hpState = 0.0f;
        airPrevL = 0.0f; airPrevR = 0.0f;
        airStateL = 0.0f; airStateR = 0.0f;
        currentSection = SongDramaticSection::IntimateVerse;
        wasInChorusRecently = false;
        chorusMemoryCounter = 0;
    }

    inline void processStereo (float& left, float& right, float dramaticAutoLift01 = 0.50f) noexcept
    {
        if (dramaticAutoLift01 <= 0.001f)
            return;

        const float mono = 0.5f * (left + right);
        const float absMono = std::fabs (mono);

        phraseEnv  = phraseEnvCoeff  * phraseEnv  + (1.0f - phraseEnvCoeff)  * absMono;
        sectionEnv = sectionEnvCoeff * sectionEnv + (1.0f - sectionEnvCoeff) * absMono;

        // 高音倍音密度トラッキング（サビでハイノートを張った時に急増）
        hpState = highNoteHpCoeff * (hpState + mono - prevMono);
        prevMono = mono;
        highNoteEnv = phraseEnvCoeff * highNoteEnv + (1.0f - phraseEnvCoeff) * std::fabs (hpState);

        const float highRatio   = highNoteEnv / std::max (0.001f, phraseEnv);
        const float energyRatio = phraseEnv   / std::max (0.002f, sectionEnv);

        // クライマックス指標 (0..1)
        const float rawClimax = std::clamp ((energyRatio - 0.95f) * 0.75f + (highRatio - 0.25f) * 0.85f, 0.0f, 1.0f);
        smoothedClimax01 = smoothLiftCoeff * smoothedClimax01 + (1.0f - smoothLiftCoeff) * rawClimax;

        // セクション判定
        if (smoothedClimax01 > 0.58f && phraseEnv > 0.025f)
        {
            currentSection = SongDramaticSection::SoaringChorusClimax;
            wasInChorusRecently = true;
            chorusMemoryCounter = static_cast<int> (4.0 * sampleRateHz); // サビ後4秒間の落ちサビ監視窓
        }
        else if (wasInChorusRecently && phraseEnv < sectionEnv * 0.55f && phraseEnv > 0.002f)
        {
            currentSection = SongDramaticSection::DropChorusWhisper;
            if (chorusMemoryCounter > 0) --chorusMemoryCounter;
            else wasInChorusRecently = false;
        }
        else if (smoothedClimax01 > 0.30f)
        {
            currentSection = SongDramaticSection::RisingPreChorus;
        }
        else
        {
            currentSection = SongDramaticSection::IntimateVerse;
        }

        // 11kHz ゴールデンエアー抽出
        airStateL = airHpCoeff * (airStateL + left  - airPrevL);
        airPrevL  = left;
        airStateR = airHpCoeff * (airStateR + right - airPrevR);
        airPrevR  = right;

        // Mid/Side 変換による空間制御
        float mid  = 0.5f * (left + right);
        float side = 0.5f * (left - right);
        const float synthWing = wingExpander.synthesizeChorusSideWing (mid);

        const float liftScale = std::clamp (dramaticAutoLift01, 0.0f, 1.0f);
        if (currentSection == SongDramaticSection::DropChorusWhisper)
        {
            // 落ちサビ：あえてSideを少し絞ってセンター耳元へ引き寄せ、息づかいを強調
            mid  *= (1.0f + 0.06f * liftScale);
            side *= (1.0f - 0.12f * liftScale);
        }
        else
        {
            // Bメロ〜サビ：クライマックス度に応じて Mid の芯を最大 +0.85dB (約+10%) リフトし、Sideの翼を展開
            const float gainLift = 1.0f + 0.10f * liftScale * smoothedClimax01;
            const float sideWing = 1.0f + 0.18f * liftScale * smoothedClimax01;
            mid  *= gainLift;
            side  = side * sideWing + (0.09f * liftScale * smoothedClimax01) * synthWing;
        }

        const float airBoost = 0.18f * liftScale * smoothedClimax01;
        left  = (mid + side) + airBoost * airStateL;
        right = (mid - side) + airBoost * airStateR;
    }

    ChorusClimaxTelemetry getTelemetry (float dramaticAutoLift01 = 0.50f) const noexcept
    {
        ChorusClimaxTelemetry t;
        t.detectedSection        = currentSection;
        t.climaxLiftDb           = dramaticAutoLift01 * smoothedClimax01 * 0.85f;
        t.stereoWingExpansionPct = dramaticAutoLift01 * smoothedClimax01 * 18.0f;
        t.dramaticArcIntensity01 = smoothedClimax01;
        return t;
    }

private:
    double sampleRateHz = 48000.0;
    ChorusStereoWingExpander wingExpander;
    float phraseEnvCoeff  = 0.9999f;
    float sectionEnvCoeff = 0.99999f;
    float smoothLiftCoeff = 0.9998f;
    float highNoteHpCoeff = 0.82f;
    float airHpCoeff      = 0.28f;

    float phraseEnv = 0.005f;
    float sectionEnv = 0.005f;
    float highNoteEnv = 0.001f;
    float smoothedClimax01 = 0.0f;

    float prevMono = 0.0f, hpState = 0.0f;
    float airPrevL = 0.0f, airPrevR = 0.0f;
    float airStateL = 0.0f, airStateR = 0.0f;

    SongDramaticSection currentSection = SongDramaticSection::IntimateVerse;
    bool wasInChorusRecently = false;
    int chorusMemoryCounter = 0;
};

} // namespace vg::human
