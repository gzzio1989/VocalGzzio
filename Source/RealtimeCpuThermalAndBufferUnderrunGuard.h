#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace vg::safety
{

// ============================================================================
// RealtimeCpuThermalAndBufferUnderrunGuard.h
// ----------------------------------------------------------------------------
// 安全性＆軽量動作のリアルタイム・デッドライン防衛モジュール:
//   - ゲーム実況中やOBS高負荷エンコード中、またはノートPCのサーマルスロットリング発生時に、
//     オーディオコールバックの実行時間がバッファ周期（例: 256 samples @ 48kHz = 5.33 ms）の
//     安全マージンに迫ったことをミリ秒未満で検知。
//   - ドロップアウト（プチノイズ / Buffer Underrun）が起きる前に、
//     クリックノイズを一切出さない等電力クロスフェード（Equal-Power Crossfade）を伴って
//     装飾系セカンダリ処理を段階的にシェディング（負荷軽減）し、絶対無事故の配信を保証する。
// ============================================================================

struct UnderrunGuardStatus
{
    float callbackBudgetMs = 5.33f;          // 1ブロックあたりの許容デッドライン (ms)
    float smoothedExecMs = 0.15f;            // 平滑化された実行時間 (ms)
    float peakJitterMs = 0.0f;               // コールバック実行時間ジッタ最大値 (ms)
    float headroomMarginRatio01 = 0.95f;     // デッドラインまでの余裕率 (0..1)
    int sheddingStage = 0;                   // 0=Full Quality, 1=Light Shed, 2=Emergency Core Only
    float secondaryBranchCrossfade01 = 1.0f; // セカンダリ高負荷DSPのクロスフェード係数 (0..1)
    bool underrunRiskPrevented = false;      // ドロップアウト未然回避フラグ
};

class RealtimeCpuThermalAndBufferUnderrunGuard
{
public:
    void prepare(double sampleRate, int maxBlockSize) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        blockSize = std::max(16, maxBlockSize);
        budgetMs = 1000.0f * static_cast<float>(blockSize) / static_cast<float>(sr);
        reset();
    }

    void reset() noexcept
    {
        smoothedMs = 0.08f;
        maxJitterMs = 0.0f;
        shedStage = 0;
        crossfadeGain = 1.0f;
        status = UnderrunGuardStatus{};
        status.callbackBudgetMs = budgetMs;
    }

    // ブロック処理開始時に呼ぶ
    inline void beginCallbackMeasurement(int currentNumSamples) noexcept
    {
        if (currentNumSamples > 0)
            budgetMs = 1000.0f * static_cast<float>(currentNumSamples) / static_cast<float>(sr);
        tStart = std::chrono::steady_clock::now();
    }

    // ブロック処理終了時に呼び、次ブロックのクロスフェード安全係数を更新
    UnderrunGuardStatus endCallbackMeasurement() noexcept
    {
        const auto tEnd = std::chrono::steady_clock::now();
        const float elapsedMs = std::chrono::duration<float, std::milli>(tEnd - tStart).count();

        const float jitter = std::fabs(elapsedMs - smoothedMs);
        maxJitterMs = 0.98f * maxJitterMs + 0.02f * std::max(maxJitterMs, jitter);
        smoothedMs = 0.88f * smoothedMs + 0.12f * elapsedMs;

        const float loadRatio = smoothedMs / std::max(0.1f, budgetMs);
        if (loadRatio > 0.65f)
            shedStage = 2;
        else if (loadRatio > 0.42f)
            shedStage = 1;
        else if (loadRatio < 0.28f)
            shedStage = 0;

        const float targetFade = (shedStage == 2) ? 0.0f : ((shedStage == 1) ? 0.5f : 1.0f);
        crossfadeGain += 0.15f * (targetFade - crossfadeGain);

        status.callbackBudgetMs = budgetMs;
        status.smoothedExecMs = smoothedMs;
        status.peakJitterMs = maxJitterMs;
        status.headroomMarginRatio01 = std::clamp(1.0f - loadRatio, 0.0f, 1.0f);
        status.sheddingStage = shedStage;
        status.secondaryBranchCrossfade01 = crossfadeGain;
        status.underrunRiskPrevented = (shedStage > 0);
        return status;
    }

    // ドライ信号とセカンダリ装飾信号をプチノイズなしで等電力ブレンドするヘルパー
    inline void blendSecondaryBranchClickFree(float* mainL,
                                              float* mainR,
                                              const float* decoratedL,
                                              const float* decoratedR,
                                              int numSamples) noexcept
    {
        if (mainL == nullptr || mainR == nullptr || decoratedL == nullptr || decoratedR == nullptr || numSamples <= 0)
            return;

        const float w = std::clamp(crossfadeGain, 0.0f, 1.0f);
        const float d = 1.0f - w;
        for (int i = 0; i < numSamples; ++i)
        {
            mainL[i] = d * mainL[i] + w * decoratedL[i];
            mainR[i] = d * mainR[i] + w * decoratedR[i];
        }
    }

    const UnderrunGuardStatus& getStatus() const noexcept { return status; }

private:
    double sr = 48000.0;
    int blockSize = 512;
    float budgetMs = 10.66f;
    float smoothedMs = 0.08f;
    float maxJitterMs = 0.0f;
    int shedStage = 0;
    float crossfadeGain = 1.0f;
    std::chrono::steady_clock::time_point tStart{};
    UnderrunGuardStatus status{};
};

} // namespace vg::safety
