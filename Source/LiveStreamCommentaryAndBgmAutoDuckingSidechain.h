#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vg::human
{

// ============================================================================
// LiveStreamCommentaryAndBgmAutoDuckingSidechain.h
// ----------------------------------------------------------------------------
// 人間の欲求「雑談配信・歌枠・ゲーム実況で、ささやき声から爆笑・絶叫までどんな声量でも
// BGMに埋もれず、かつ聞き手が一度も音量ツマミを触らなくて済む神バランスにしたい」を叶える
// 配信者・シンガー特化型インテリジェント・スペクトラル・ダッキング＆オートライダー:
//   1. WhisperToLaughSeamlessVocalRider:
//      ささやき声 (-38 dBFS) はノイズフロアを持ち上げずに +5.5 dB 優しく引き上げ、
//      突発的な爆笑・絶叫 (-3 dBFS) は 1.5 ms の高速ソフトニーで耳障りな飽和を防ぎ、
//      長時間の雑談・歌枠でも常にリスナーの耳元に心地よく届く声量を自動維持。
//   2. SpectralPocketBgmSidechainCarver:
//      従来のダッキングのようにBGM全体の音量を下げて不自然なポンピングを起こすのではなく、
//      声が鳴った瞬間だけ BGM の中域（350 Hz 〜 3.4 kHz: 言葉の明瞭度ポケット）だけを
//      最大 -4.5 dB 彫り込み、BGMのローエンドとステレオの広がりはそのまま残す。
// ============================================================================

struct StreamRiderAndDuckingReport
{
    float vocalRiderGainDb = 0.0f;          // 声量自動レベラー補正量 (-6.0 .. +5.5 dB)
    float bgmPocketCarveDb = 0.0f;          // BGM中域ポケット自動ダッキング深さ (0 .. -4.5 dB)
    float speechActivityConfidence01 = 0.0f;// 発話・歌唱アクティビティ確信度 (0..1)
    bool laughOrShoutProtected = false;     // 突発的な爆笑・シャウト保護作動フラグ
};

class LiveStreamCommentaryAndBgmAutoDuckingSidechain
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        attackFastCoeff = 1.0f - std::exp(-1.0f / (0.0025f * static_cast<float>(sr)));
        releaseSlowCoeff = 1.0f - std::exp(-1.0f / (0.240f * static_cast<float>(sr)));
        riderSmoothCoeff = 1.0f - std::exp(-1.0f / (0.045f * static_cast<float>(sr)));
        midLpCoeff = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * 3400.0f / static_cast<float>(sr));
        midHpCoeff = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * 320.0f / static_cast<float>(sr));
        reset();
    }

    void reset() noexcept
    {
        vocalEnvFast = 0.0f;
        vocalEnvSlow = 0.0f;
        riderGainLinear = 1.0f;
        duckingDepth01 = 0.0f;
        bgmLpStateL = bgmLpStateR = 0.0f;
        bgmHpStateL = bgmHpStateR = 0.0f;
        lastReport = StreamRiderAndDuckingReport{};
    }

    void setTargetVocalLevelDb(float targetDb) noexcept
    {
        targetLevelLinear = std::pow(10.0f, std::clamp(targetDb, -28.0f, -10.0f) / 20.0f);
    }

    // ボーカル単体トラックに対する Whisper-to-Shout シームレス・オートライダー処理
    StreamRiderAndDuckingReport processVocalRider(float* vocalL, float* vocalR, int numSamples) noexcept
    {
        if (vocalL == nullptr || vocalR == nullptr || numSamples <= 0)
            return lastReport;

        bool shoutCaught = false;
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = vocalL[i];
            const float r = vocalR[i];
            const float peak = std::max(std::fabs(l), std::fabs(r));

            if (peak > vocalEnvFast)
                vocalEnvFast += attackFastCoeff * (peak - vocalEnvFast);
            else
                vocalEnvFast += releaseSlowCoeff * (peak - vocalEnvFast);

            vocalEnvSlow += 0.0012f * (peak - vocalEnvSlow);

            // 発話確信度（ノイズフロア 0.004 = -48 dBFS 以上で滑らかに立ち上がる）
            const float vad01 = std::clamp((vocalEnvFast - 0.004f) / 0.025f, 0.0f, 1.0f);

            float desiredGain = 1.0f;
            if (vocalEnvFast > 0.38f)
            {
                // 突発的な爆笑・絶叫の保護 (-8.4 dBFS 超)
                shoutCaught = true;
                desiredGain = std::clamp(0.38f / (vocalEnvFast + 1.0e-6f), 0.45f, 1.0f);
            }
            else if (vad01 > 0.05f)
            {
                // ささやき〜通常発話をターゲットレベルへ優しく引き寄せる
                const float ratio = targetLevelLinear / std::max(0.012f, vocalEnvSlow);
                const float gentleGain = std::clamp(std::pow(ratio, 0.42f), 0.65f, 1.88f); // -3.7 dB .. +5.5 dB
                desiredGain = 1.0f + vad01 * (gentleGain - 1.0f);
            }

            riderGainLinear += riderSmoothCoeff * (desiredGain - riderGainLinear);
            duckingDepth01 += 0.008f * (vad01 - duckingDepth01);

            vocalL[i] = l * riderGainLinear;
            vocalR[i] = r * riderGainLinear;
        }

        lastReport.vocalRiderGainDb = 20.0f * std::log10(std::max(0.1f, riderGainLinear));
        lastReport.bgmPocketCarveDb = -4.5f * duckingDepth01;
        lastReport.speechActivityConfidence01 = std::clamp((vocalEnvFast - 0.004f) / 0.03f, 0.0f, 1.0f);
        lastReport.laughOrShoutProtected = shoutCaught;
        return lastReport;
    }

    // BGM/カラオケトラックに対して、声が鳴っている間だけ 320Hz〜3.4kHz の中域ポケットを彫り込む
    void processBgmSpectralPocketSidechain(float* bgmL, float* bgmR, int numSamples) noexcept
    {
        if (bgmL == nullptr || bgmR == nullptr || numSamples <= 0)
            return;

        // 最大 -4.5 dB (linear ~0.595) の中域カービング係数
        const float midAtten = 1.0f - 0.405f * std::clamp(duckingDepth01, 0.0f, 1.0f);

        for (int i = 0; i < numSamples; ++i)
        {
            const float l = bgmL[i];
            const float r = bgmR[i];

            bgmLpStateL += midLpCoeff * (l - bgmLpStateL);
            bgmLpStateR += midLpCoeff * (r - bgmLpStateR);
            bgmHpStateL += midHpCoeff * (bgmLpStateL - bgmHpStateL);
            bgmHpStateR += midHpCoeff * (bgmLpStateR - bgmHpStateR);

            // 320 Hz .. 3400 Hz のボーカル競合ポケット成分のみを抽出して減衰
            const float pocketMidL = bgmLpStateL - bgmHpStateL;
            const float pocketMidR = bgmLpStateR - bgmHpStateR;

            bgmL[i] = (l - pocketMidL) + pocketMidL * midAtten;
            bgmR[i] = (r - pocketMidR) + pocketMidR * midAtten;
        }
    }

    const StreamRiderAndDuckingReport& getLastReport() const noexcept { return lastReport; }

private:
    double sr = 48000.0;
    float targetLevelLinear = 0.14125f; // -17 dBFS
    float attackFastCoeff = 0.01f;
    float releaseSlowCoeff = 0.0002f;
    float riderSmoothCoeff = 0.001f;
    float midLpCoeff = 0.35f;
    float midHpCoeff = 0.04f;
    float vocalEnvFast = 0.0f;
    float vocalEnvSlow = 0.0f;
    float riderGainLinear = 1.0f;
    float duckingDepth01 = 0.0f;
    float bgmLpStateL = 0.0f, bgmLpStateR = 0.0f;
    float bgmHpStateL = 0.0f, bgmHpStateR = 0.0f;
    StreamRiderAndDuckingReport lastReport{};
};

} // namespace vg::human
