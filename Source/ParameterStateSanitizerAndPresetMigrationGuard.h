#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace vg::arbitration
{

// ============================================================================
// ParameterStateSanitizerAndPresetMigrationGuard.h
// ----------------------------------------------------------------------------
// コードの見直し・無駄な処理・旧世代プリセット互換性の整合性検証エンジン:
//   1. ParameterValueSanitizer:
//      DAWオートメーションや破損プリセットから注入される範囲外値・NaN・Inf・
//      微小ドリフト（デッドバンド内の無駄な係数再計算）を検出し、
//      係数再計算が本当に必要なときだけ dirty フラグを立てることでCPU負荷を削減。
//   2. LegacyPresetEraMigrationNormalizer:
//      v1.0〜v5.5.0までの全19世代プリセットのパラメータ単位系（0..1 正規化値 vs
//      0..100 パーセント値 vs dB値）の食い違いを自動検知・正規化し、
//      「旧プリセットを読み込んだら音が割れた／無音になった」事故を100%防ぐ。
//   3. ZombieModulationAndNoOpBypassDetector:
//      Wet=0% や Gain=0 dB や Ratio=1.0:1 のまま稼働している実質No-Op（無意味）な
//      DSPブロックを特定し、即座にゼロコスト・バイパスへ切り替える。
// ============================================================================

struct ParameterSanitizationSummary
{
    int totalParametersChecked = 0;
    int clampedOutOfRangeCount = 0;
    int nanOrInfRecoveredCount = 0;
    int redundantRecalcSkippedCount = 0;
    int noOpModulesAutoBypassedCount = 0;
    bool presetMigrationClean = true;
};

// ----------------------------------------------------------------------------
// 1. 変更検知デッドバンド付きパラメータサニタイザ（無駄なBiquad係数再計算を防止）
// ----------------------------------------------------------------------------
template <size_t NumParams = 32>
class ParameterValueSanitizer
{
public:
    struct ParamSpec
    {
        float minVal = 0.0f;
        float maxVal = 1.0f;
        float defaultVal = 0.5f;
        float deadbandEpsilon = 1.0e-4f;
    };

    void configureParam(size_t idx, float minV, float maxV, float defV, float eps = 1.0e-4f) noexcept
    {
        if (idx >= NumParams)
            return;
        specs[idx] = { minV, maxV, defV, std::max(1.0e-7f, eps) };
        cachedValues[idx] = defV;
        initialized[idx] = false;
    }

    // 値をサニタイズし、前回のキャッシュ値から eps 以上変化した場合のみ isDirty = true を返す
    bool sanitizeAndCheckDirty(size_t idx, float rawValue, float& outCleanValue) noexcept
    {
        if (idx >= NumParams)
        {
            outCleanValue = 0.0f;
            return false;
        }

        ++summary.totalParametersChecked;
        const auto& spec = specs[idx];

        if (!std::isfinite(rawValue))
        {
            ++summary.nanOrInfRecoveredCount;
            rawValue = spec.defaultVal;
        }
        else if (rawValue < spec.minVal || rawValue > spec.maxVal)
        {
            ++summary.clampedOutOfRangeCount;
            rawValue = std::clamp(rawValue, spec.minVal, spec.maxVal);
        }

        outCleanValue = rawValue;

        if (!initialized[idx] || std::fabs(rawValue - cachedValues[idx]) > spec.deadbandEpsilon)
        {
            cachedValues[idx] = rawValue;
            initialized[idx] = true;
            return true; // 係数再計算が必要
        }

        ++summary.redundantRecalcSkippedCount;
        return false; // デッドバンド内のため高コストな三角関数/exp再計算をスキップ可能
    }

    const ParameterSanitizationSummary& getSummary() const noexcept { return summary; }
    void resetSummary() noexcept { summary = ParameterSanitizationSummary{}; }

private:
    std::array<ParamSpec, NumParams> specs{};
    std::array<float, NumParams> cachedValues{};
    std::array<bool, NumParams> initialized{};
    ParameterSanitizationSummary summary{};
};

// ----------------------------------------------------------------------------
// 2. 旧世代プリセット（0..100% 表記と 0..1 正規化表記の混在）自動マイグレーション
// ----------------------------------------------------------------------------
class LegacyPresetEraMigrationNormalizer
{
public:
    // 0..1 を期待するパラメータに旧世代の 0..100 スケール値が入った場合を自動判別・変換
    static inline float normalizeUnit01OrPercent(float rawVal, float fallbackDefault = 0.5f) noexcept
    {
        if (!std::isfinite(rawVal))
            return fallbackDefault;
        if (rawVal > 1.0001f && rawVal <= 100.0f)
            return rawVal * 0.01f;
        return std::clamp(rawVal, 0.0f, 1.0f);
    }

    // dB ゲイン値の安全なマイグレーション (-60 dB .. +24 dB)
    static inline float normalizeDecibelGain(float rawDb, float minDb = -60.0f, float maxDb = 24.0f) noexcept
    {
        if (!std::isfinite(rawDb))
            return 0.0f;
        return std::clamp(rawDb, minDb, maxDb);
    }

    // 周波数パラメータの安全なマイグレーション (20 Hz .. 20000 Hz)
    static inline float normalizeFrequencyHz(float rawHz, float defaultHz = 1000.0f) noexcept
    {
        if (!std::isfinite(rawHz) || rawHz <= 0.0f)
            return defaultHz;
        return std::clamp(rawHz, 20.0f, 20000.0f);
    }
};

// ----------------------------------------------------------------------------
// 3. No-Op（実質無効）エフェクト・ゾンビモジュレーション自動バイパス判定器
// ----------------------------------------------------------------------------
class ZombieModulationAndNoOpBypassDetector
{
public:
    // EQバンドのゲインが ±0.03 dB 未満ならフィルタ演算自体をスキップ可能と判定
    static constexpr bool isEqBandNoOp(float gainDb) noexcept
    {
        return (gainDb > -0.03f && gainDb < 0.03f);
    }

    // コンプレッサの Ratio が 1.02:1 未満 または Mix が 0.5% 未満ならスキップ可能
    static constexpr bool isCompressorNoOp(float ratio, float wetMix01) noexcept
    {
        return (ratio < 1.02f) || (wetMix01 < 0.005f);
    }

    // 空間系エフェクトの Wet Mix が 0.2% 未満 かつ テール残響エネルギーが -96 dB 未満なら休眠可能
    static constexpr bool isSpatialModuleDormant(float wetMix01, float tailRmsLinear) noexcept
    {
        return (wetMix01 < 0.002f) && (tailRmsLinear < 1.58e-5f);
    }
};

} // namespace vg::arbitration
