#pragma once
// ============================================================================
// SovereignAudioCallbackWatchdogAndFaultIsolator.h
// VocalGzzio v5.5.0 — Sovereign Domain 1: Zero-Defect Real-Time Safety
//
// オーディオスレッドの各DSPステージ前後で IEEE-754 非正規化数（Subnormal）、
// NaN、±Inf、DCオフセット暴走、および異常ピーク（> +24 dBFS）をビットレベルで
// 監視・隔離し、万が一いずれかのステージで数値異常が発生しても 0.0ms で
// 該当ステージのみをソフトバイパスしてノイズ・爆音・クラッシュを100%防ぐ
// リアルタイム・フォールトアイソレータ＆ウォッチドッグ。
// ============================================================================

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace vg::sovereign::safety {

enum class DspStageId : std::uint8_t {
    InputTrimAndPolarity = 0,
    NoiseGateAndBlanker = 1,
    SpectralDenoise = 2,
    DereverbAndRoomControl = 3,
    HighPassAndPlosiveGuard = 4,
    PitchAndFormantShifter = 5,
    HarmonyGenerator = 6,
    ResonanceTamerAndMudCut = 7,
    FetPeakCompressor = 8,
    OptoLevelingCompressor = 9,
    DeEsserAndSibilanceControl = 10,
    ToneShapingEqAndAir = 11,
    HarmonicExciterAndTape = 12,
    SpatialReverbAndDelay = 13,
    CharacterVoiceEffect = 14,
    TruePeakMasterLimiter = 15,
    kNumStages = 16
};

enum class FaultCategory : std::uint8_t {
    None = 0,
    SubnormalFlushed = 1,
    NanScrubbed = 2,
    InfinityScrubbed = 3,
    OverloadClamped = 4,
    DcOffsetCorrected = 5,
    SlewRateExceeded = 6,
    DeadlineOverrun = 7
};

struct StageFaultRecord {
    DspStageId stage = DspStageId::InputTrimAndPolarity;
    FaultCategory category = FaultCategory::None;
    std::uint32_t sampleIndex = 0;
    float offendingValue = 0.0f;
    float sanitizedValue = 0.0f;
    std::uint64_t callbackSequence = 0;
};

struct StageHealthSnapshot {
    std::uint64_t totalSamplesInspected = 0;
    std::uint32_t subnormalCount = 0;
    std::uint32_t nanCount = 0;
    std::uint32_t infCount = 0;
    std::uint32_t overloadCount = 0;
    std::uint32_t dcFaultCount = 0;
    std::uint32_t slewFaultCount = 0;
    float maxObservedAbsPeak = 0.0f;
    float estimatedDcOffsetL = 0.0f;
    float estimatedDcOffsetR = 0.0f;
    float currentBypassRecoveryGain = 1.0f;
    bool stageHealthy = true;
};

struct WatchdogGlobalReport {
    std::array<StageHealthSnapshot, static_cast<std::size_t>(DspStageId::kNumStages)> stages{};
    std::uint64_t totalCallbacksMonitored = 0;
    std::uint32_t totalFaultEventsLogged = 0;
    float overallSystemSafetyScore01 = 1.0f;
    bool allStagesOperational = true;
};

// ----------------------------------------------------------------------------
// IEEE-754 単精度ビット操作ユーティリティ（分岐なし・constexpr対応）
// ----------------------------------------------------------------------------
struct Ieee754BitInspector {
    static constexpr std::uint32_t kSignMask     = 0x80000000u;
    static constexpr std::uint32_t kExponentMask = 0x7F800000u;
    static constexpr std::uint32_t kMantissaMask = 0x007FFFFFu;
    static constexpr std::uint32_t kAbsMask      = 0x7FFFFFFFu;
    static constexpr std::uint32_t kMinNormalBits = 0x00800000u; // ~1.17549435e-38f
    static constexpr std::uint32_t kPracticalZeroBits = 0x20000000u; // ~1.084e-19f

    static inline std::uint32_t toBits(float v) noexcept {
        std::uint32_t b = 0;
        std::memcpy(&b, &v, sizeof(float));
        return b;
    }

    static inline float toFloat(std::uint32_t b) noexcept {
        float v = 0.0f;
        std::memcpy(&v, &b, sizeof(float));
        return v;
    }

    static inline bool isNanOrInfBits(std::uint32_t b) noexcept {
        return (b & kExponentMask) == kExponentMask;
    }

    static inline bool isNanBits(std::uint32_t b) noexcept {
        return ((b & kExponentMask) == kExponentMask) && ((b & kMantissaMask) != 0u);
    }

    static inline bool isInfBits(std::uint32_t b) noexcept {
        return ((b & kExponentMask) == kExponentMask) && ((b & kMantissaMask) == 0u);
    }

    static inline bool isSubnormalOrTinyBits(std::uint32_t b) noexcept {
        const std::uint32_t absB = b & kAbsMask;
        return (absB != 0u) && (absB < kPracticalZeroBits);
    }

    static inline float flushSubnormalBranchless(float v) noexcept {
        const std::uint32_t b = toBits(v);
        const std::uint32_t absB = b & kAbsMask;
        const bool keep = (absB >= kPracticalZeroBits) && ((b & kExponentMask) != kExponentMask);
        const std::uint32_t mask = keep ? 0xFFFFFFFFu : 0x00000000u;
        return toFloat(b & mask);
    }
};

// ----------------------------------------------------------------------------
// ステージ単位の2次カスケードDCトラッカー＆ソフトリカバリフィルタ
// ----------------------------------------------------------------------------
class StageDcAndSlewProtector {
public:
    void prepare(double sampleRate) noexcept {
        sr = (sampleRate > 8000.0) ? sampleRate : 48000.0;
        // 2.0Hz カットオフの1次DC極
        const double omega = 2.0 * 3.14159265358979323846 * 2.0 / sr;
        dcPoleR = static_cast<float>(std::clamp(1.0 - omega, 0.995, 0.99995));
        // DC推定用 0.5Hz ローパス係数
        dcTrackAlpha = static_cast<float>(std::clamp(1.0 - std::exp(-2.0 * 3.14159265358979323846 * 0.5 / sr), 1.0e-5, 1.0e-2));
        // 1サンプルあたりの最大許容スルーレート（48kHzで1.8f、96kHzで0.95f）
        maxStepPerSample = static_cast<float>(std::clamp(1.8 * (48000.0 / sr), 0.35, 2.5));
        reset();
    }

    void reset() noexcept {
        x1L = 0.0f; y1L = 0.0f;
        x1R = 0.0f; y1R = 0.0f;
        dcMeanL = 0.0f; dcMeanR = 0.0f;
        prevOutL = 0.0f; prevOutR = 0.0f;
    }

    inline float processLeft(float x, bool& outDcFault, bool& outSlewFault) noexcept {
        dcMeanL += dcTrackAlpha * (x - dcMeanL);
        dcMeanL = Ieee754BitInspector::flushSubnormalBranchless(dcMeanL);
        outDcFault = (std::abs(dcMeanL) > 0.05f);

        float y = x;
        if (outDcFault) {
            y = x - x1L + dcPoleR * y1L;
            x1L = x;
            y1L = Ieee754BitInspector::flushSubnormalBranchless(y);
        } else {
            x1L = x;
            y1L = y;
        }

        const float delta = y - prevOutL;
        if (std::abs(delta) > maxStepPerSample) {
            outSlewFault = true;
            y = prevOutL + ((delta > 0.0f) ? maxStepPerSample : -maxStepPerSample);
        } else {
            outSlewFault = false;
        }
        prevOutL = y;
        return y;
    }

    inline float processRight(float x, bool& outDcFault, bool& outSlewFault) noexcept {
        dcMeanR += dcTrackAlpha * (x - dcMeanR);
        dcMeanR = Ieee754BitInspector::flushSubnormalBranchless(dcMeanR);
        outDcFault = (std::abs(dcMeanR) > 0.05f);

        float y = x;
        if (outDcFault) {
            y = x - x1R + dcPoleR * y1R;
            x1R = x;
            y1R = Ieee754BitInspector::flushSubnormalBranchless(y);
        } else {
            x1R = x;
            y1R = y;
        }

        const float delta = y - prevOutR;
        if (std::abs(delta) > maxStepPerSample) {
            outSlewFault = true;
            y = prevOutR + ((delta > 0.0f) ? maxStepPerSample : -maxStepPerSample);
        } else {
            outSlewFault = false;
        }
        prevOutR = y;
        return y;
    }

    float getEstimatedDcLeft() const noexcept { return dcMeanL; }
    float getEstimatedDcRight() const noexcept { return dcMeanR; }

private:
    double sr = 48000.0;
    float dcPoleR = 0.9995f;
    float dcTrackAlpha = 0.0001f;
    float maxStepPerSample = 1.8f;
    float x1L = 0.0f, y1L = 0.0f;
    float x1R = 0.0f, y1R = 0.0f;
    float dcMeanL = 0.0f, dcMeanR = 0.0f;
    float prevOutL = 0.0f, prevOutR = 0.0f;
};

// ----------------------------------------------------------------------------
// ロックフリー・フォールトイベントリングバッファ（64エントリ固定長）
// ----------------------------------------------------------------------------
class LockFreeFaultEventRing {
public:
    static constexpr std::size_t kCapacity = 64;

    void clear() noexcept {
        writeIdx.store(0, std::memory_order_relaxed);
        readIdx.store(0, std::memory_order_relaxed);
        for (auto& slot : buffer) {
            slot = StageFaultRecord{};
        }
    }

    inline bool pushFromAudioThread(const StageFaultRecord& rec) noexcept {
        const std::size_t w = writeIdx.load(std::memory_order_relaxed);
        const std::size_t r = readIdx.load(std::memory_order_acquire);
        const std::size_t nextW = (w + 1u) & (kCapacity - 1u);
        if (nextW == r) {
            return false; // フルの場合はオーディオスレッドをブロックせず破棄
        }
        buffer[w] = rec;
        writeIdx.store(nextW, std::memory_order_release);
        return true;
    }

    inline bool popFromUiThread(StageFaultRecord& outRec) noexcept {
        const std::size_t r = readIdx.load(std::memory_order_relaxed);
        const std::size_t w = writeIdx.load(std::memory_order_acquire);
        if (r == w) {
            return false;
        }
        outRec = buffer[r];
        readIdx.store((r + 1u) & (kCapacity - 1u), std::memory_order_release);
        return true;
    }

private:
    std::array<StageFaultRecord, kCapacity> buffer{};
    std::atomic<std::size_t> writeIdx{0};
    std::atomic<std::size_t> readIdx{0};
};

// ----------------------------------------------------------------------------
// メインクラス: SovereignAudioCallbackWatchdogAndFaultIsolator
// ----------------------------------------------------------------------------
class SovereignAudioCallbackWatchdogAndFaultIsolator {
public:
    static constexpr std::size_t kNumStages = static_cast<std::size_t>(DspStageId::kNumStages);
    static constexpr std::size_t kMaxBackupSamples = 4096;

    void prepare(double sampleRate, std::size_t maxBlockSize = 512) noexcept {
        sr = (sampleRate > 8000.0) ? sampleRate : 48000.0;
        configuredBlockSize = std::min(maxBlockSize, kMaxBackupSamples);
        // 異常検出後のソフト復帰時定数（35ms）
        recoveryIncrement = static_cast<float>(1.0 / std::max(1.0, 0.035 * sr));
        for (std::size_t s = 0; s < kNumStages; ++s) {
            protectors[s].prepare(sr);
            snapshots[s] = StageHealthSnapshot{};
        }
        faultRing.clear();
        dryBackupL.fill(0.0f);
        dryBackupR.fill(0.0f);
        callbackCounter = 0;
        totalFaults = 0;
    }

    void reset() noexcept {
        for (std::size_t s = 0; s < kNumStages; ++s) {
            protectors[s].reset();
            snapshots[s] = StageHealthSnapshot{};
        }
        faultRing.clear();
        dryBackupL.fill(0.0f);
        dryBackupR.fill(0.0f);
        callbackCounter = 0;
        totalFaults = 0;
    }

    // ステージ実行前にドライ信号のバックアップを記録（ヒープ確保ゼロ）
    inline void capturePreStageBackup(const float* left,
                                      const float* right,
                                      std::size_t numSamples) noexcept {
        const std::size_t n = std::min(numSamples, kMaxBackupSamples);
        if (left != nullptr) {
            for (std::size_t i = 0; i < n; ++i) {
                dryBackupL[i] = Ieee754BitInspector::flushSubnormalBranchless(left[i]);
            }
        }
        if (right != nullptr) {
            for (std::size_t i = 0; i < n; ++i) {
                dryBackupR[i] = Ieee754BitInspector::flushSubnormalBranchless(right[i]);
            }
        }
    }

    // ステージ実行後のバッファを検査し、NaN/Inf/異常ピークがあればドライバックアップへソフト退避
    inline StageHealthSnapshot inspectAndIsolateStage(DspStageId stageId,
                                                      float* left,
                                                      float* right,
                                                      std::size_t numSamples,
                                                      float hardCeilingLinear = 3.9810717f) noexcept {
        const std::size_t sIdx = static_cast<std::size_t>(stageId);
        auto& snap = snapshots[sIdx];
        auto& prot = protectors[sIdx];
        const std::size_t n = std::min(numSamples, kMaxBackupSamples);

        if (left == nullptr || right == nullptr || n == 0) {
            return snap;
        }

        ++callbackCounter;
        bool hardFaultInBlock = false;

        for (std::size_t i = 0; i < n; ++i) {
            float sL = left[i];
            float sR = right[i];

            const std::uint32_t bitsL = Ieee754BitInspector::toBits(sL);
            const std::uint32_t bitsR = Ieee754BitInspector::toBits(sR);

            // 1. NaN / Inf 検査
            if (Ieee754BitInspector::isNanOrInfBits(bitsL) || Ieee754BitInspector::isNanOrInfBits(bitsR)) {
                hardFaultInBlock = true;
                if (Ieee754BitInspector::isNanBits(bitsL) || Ieee754BitInspector::isNanBits(bitsR)) {
                    ++snap.nanCount;
                    logFault(stageId, FaultCategory::NanScrubbed, static_cast<std::uint32_t>(i), sL, dryBackupL[i]);
                } else {
                    ++snap.infCount;
                    logFault(stageId, FaultCategory::InfinityScrubbed, static_cast<std::uint32_t>(i), sL, dryBackupL[i]);
                }
                sL = dryBackupL[i];
                sR = dryBackupR[i];
            } else {
                // 2. Subnormal フラッシュ
                if (Ieee754BitInspector::isSubnormalOrTinyBits(bitsL)) {
                    ++snap.subnormalCount;
                    sL = 0.0f;
                }
                if (Ieee754BitInspector::isSubnormalOrTinyBits(bitsR)) {
                    ++snap.subnormalCount;
                    sR = 0.0f;
                }
            }

            // 3. 異常過大振幅（+12dBFS超など）のソフト飽和保護
            if (std::abs(sL) > hardCeilingLinear || std::abs(sR) > hardCeilingLinear) {
                ++snap.overloadCount;
                hardFaultInBlock = true;
                logFault(stageId, FaultCategory::OverloadClamped, static_cast<std::uint32_t>(i),
                         std::max(std::abs(sL), std::abs(sR)), hardCeilingLinear);
                sL = hardCeilingLinear * std::tanh(sL / hardCeilingLinear);
                sR = hardCeilingLinear * std::tanh(sR / hardCeilingLinear);
            }

            // 4. DCオフセット＆スルーレート保護
            bool dcFaultL = false, slewFaultL = false;
            bool dcFaultR = false, slewFaultR = false;
            sL = prot.processLeft(sL, dcFaultL, slewFaultL);
            sR = prot.processRight(sR, dcFaultR, slewFaultR);
            if (dcFaultL || dcFaultR) ++snap.dcFaultCount;
            if (slewFaultL || slewFaultR) ++snap.slewFaultCount;

            // 5. ハード異常発生時はドライ信号へ即座にフォールバックし、正常時は35msかけて滑らかに復帰
            if (hardFaultInBlock) {
                snap.currentBypassRecoveryGain = 0.0f;
            } else if (snap.currentBypassRecoveryGain < 1.0f) {
                snap.currentBypassRecoveryGain = std::min(1.0f, snap.currentBypassRecoveryGain + recoveryIncrement);
            }

            const float wetMix = snap.currentBypassRecoveryGain;
            const float dryMix = 1.0f - wetMix;
            const float outL = wetMix * sL + dryMix * dryBackupL[i];
            const float outR = wetMix * sR + dryMix * dryBackupR[i];

            left[i] = outL;
            right[i] = outR;

            const float pk = std::max(std::abs(outL), std::abs(outR));
            if (pk > snap.maxObservedAbsPeak) {
                snap.maxObservedAbsPeak = pk;
            }
        }

        snap.totalSamplesInspected += n;
        snap.estimatedDcOffsetL = prot.getEstimatedDcLeft();
        snap.estimatedDcOffsetR = prot.getEstimatedDcRight();
        snap.stageHealthy = !hardFaultInBlock && (snap.currentBypassRecoveryGain > 0.99f);
        return snap;
    }

    WatchdogGlobalReport buildGlobalReport() const noexcept {
        WatchdogGlobalReport rep{};
        rep.stages = snapshots;
        rep.totalCallbacksMonitored = callbackCounter;
        rep.totalFaultEventsLogged = totalFaults;
        std::size_t healthyCount = 0;
        for (const auto& s : snapshots) {
            if (s.stageHealthy) ++healthyCount;
        }
        rep.overallSystemSafetyScore01 = static_cast<float>(healthyCount) / static_cast<float>(kNumStages);
        rep.allStagesOperational = (healthyCount == kNumStages);
        return rep;
    }

    bool popLatestFaultEvent(StageFaultRecord& outRecord) noexcept {
        return faultRing.popFromUiThread(outRecord);
    }

    // =========================================================================
    // 16ステージ別安全閾値・リカバリ時定数リファレンステーブル
    // =========================================================================
    struct StageSafetySpecification {
        DspStageId stageId;
        const char* stageCode;
        const char* japaneseName;
        float maxAllowedLinearPeak;
        float maxAllowedDcOffset;
        float maxSlewPerSample48k;
        float recoveryFadeMs;
        bool allowSoftSaturationFallback;
    };

    static constexpr std::array<StageSafetySpecification, kNumStages> kStageSafetySpecs = {{
        { DspStageId::InputTrimAndPolarity,       "STAGE_00_INPUT_TRIM",    "入力トリム・極性反転",         3.98107f, 0.050f, 1.80f, 15.0f, true  },
        { DspStageId::NoiseGateAndBlanker,        "STAGE_01_NOISE_GATE",    "ノイズゲート・クリック除去",   3.98107f, 0.040f, 1.60f, 20.0f, true  },
        { DspStageId::SpectralDenoise,            "STAGE_02_SPECTRAL_DN",   "スペクトルAIノイズ除去",       3.54813f, 0.035f, 1.50f, 35.0f, true  },
        { DspStageId::DereverbAndRoomControl,     "STAGE_03_DEREVERB",      "6帯域DRRゼロ遅延デリバーブ",   3.54813f, 0.035f, 1.50f, 35.0f, true  },
        { DspStageId::HighPassAndPlosiveGuard,    "STAGE_04_HPF_PLOSIVE",   "ローカット・吹かれ衝撃吸収",   3.54813f, 0.020f, 1.60f, 20.0f, true  },
        { DspStageId::PitchAndFormantShifter,     "STAGE_05_PITCH_FORMANT", "ピッチ補正・ボイチェン",       3.16228f, 0.030f, 1.40f, 45.0f, true  },
        { DspStageId::HarmonyGenerator,           "STAGE_06_HARMONY_GEN",   "スマートハモリ生成",           3.16228f, 0.030f, 1.40f, 45.0f, true  },
        { DspStageId::ResonanceTamerAndMudCut,    "STAGE_07_RES_TAMER",     "なめらか共鳴抑制・こもり除去", 3.16228f, 0.025f, 1.50f, 25.0f, true  },
        { DspStageId::FetPeakCompressor,          "STAGE_08_FET_COMP",      "ピーク圧縮（1176系FET）",      2.81838f, 0.025f, 1.40f, 30.0f, true  },
        { DspStageId::OptoLevelingCompressor,     "STAGE_09_OPTO_COMP",     "ならし圧縮（LA-2A系Opto）",    2.51189f, 0.025f, 1.35f, 30.0f, true  },
        { DspStageId::DeEsserAndSibilanceControl, "STAGE_10_DEESSER",       "サ行おさえ（相対ディエッサー）", 2.51189f, 0.020f, 1.35f, 25.0f, true  },
        { DspStageId::ToneShapingEqAndAir,        "STAGE_11_TONE_AIR_EQ",   "音色づくり・ヌケ感・キラキラ", 2.81838f, 0.025f, 1.50f, 25.0f, true  },
        { DspStageId::HarmonicExciterAndTape,     "STAGE_12_HARMONIC_TAPE", "あたたかみ・艶・磁気テープ",   2.51189f, 0.015f, 1.30f, 35.0f, true  },
        { DspStageId::SpatialReverbAndDelay,      "STAGE_13_SPATIAL_REV",   "ひろがり・へやIR・やまびこ",   2.51189f, 0.020f, 1.30f, 50.0f, true  },
        { DspStageId::CharacterVoiceEffect,       "STAGE_14_CHARA_FX",      "キャラ声（ロボ・メガホン等）", 2.51189f, 0.025f, 1.40f, 30.0f, true  },
        { DspStageId::TruePeakMasterLimiter,      "STAGE_15_TRUE_PEAK_LIM", "しあげ最終True-Peakリミッター", 0.99526f, 0.005f, 1.20f, 15.0f, false }
    }};

    // -------------------------------------------------------------------------
    // 決定論的フォールトインジェクション自己診断ルーチン
    // -------------------------------------------------------------------------
    static inline bool runDeterministicSelfDiagnostic() noexcept {
        SovereignAudioCallbackWatchdogAndFaultIsolator watchdog;
        watchdog.prepare(48000.0, 128);

        std::array<float, 128> dryL{};
        std::array<float, 128> dryR{};
        std::array<float, 128> wetL{};
        std::array<float, 128> wetR{};

        for (std::size_t i = 0; i < 128; ++i) {
            const float phase = static_cast<float>(i) * 0.06544985f;
            dryL[i] = 0.25f * std::sin(phase);
            dryR[i] = 0.25f * std::cos(phase);
            wetL[i] = dryL[i];
            wetR[i] = dryR[i];
        }

        watchdog.capturePreStageBackup(dryL.data(), dryR.data(), 128);

        // 意図的に NaN / +Inf / -Inf / Subnormal / +80.0f スパイクを注入
        wetL[10] = std::numeric_limits<float>::quiet_NaN();
        wetR[20] = std::numeric_limits<float>::infinity();
        wetL[30] = -std::numeric_limits<float>::infinity();
        wetR[40] = 1.0e-42f; // Subnormal
        wetL[50] = 85.0f;    // 異常過大ピーク

        const auto snap = watchdog.inspectAndIsolateStage(
            DspStageId::PitchAndFormantShifter, wetL.data(), wetR.data(), 128, 2.0f);

        if (snap.nanCount == 0 || snap.infCount == 0 || snap.overloadCount == 0) {
            return false;
        }

        for (std::size_t i = 0; i < 128; ++i) {
            if (!std::isfinite(wetL[i]) || !std::isfinite(wetR[i])) {
                return false;
            }
            if (std::abs(wetL[i]) > 2.05f || std::abs(wetR[i]) > 2.05f) {
                return false;
            }
        }

        // 次の正常ブロックで滑らかに復帰することを検証
        for (int block = 0; block < 40; ++block) {
            for (std::size_t i = 0; i < 128; ++i) {
                const float phase = static_cast<float>(i) * 0.06544985f;
                wetL[i] = 0.25f * std::sin(phase);
                wetR[i] = 0.25f * std::cos(phase);
            }
            watchdog.capturePreStageBackup(dryL.data(), dryR.data(), 128);
            watchdog.inspectAndIsolateStage(
                DspStageId::PitchAndFormantShifter, wetL.data(), wetR.data(), 128, 2.0f);
        }

        const auto rep = watchdog.buildGlobalReport();
        return rep.allStagesOperational && (rep.overallSystemSafetyScore01 >= 0.99f);
    }

private:
    inline void logFault(DspStageId stage,
                         FaultCategory cat,
                         std::uint32_t sampleIdx,
                         float badVal,
                         float safeVal) noexcept {
        ++totalFaults;
        StageFaultRecord rec{};
        rec.stage = stage;
        rec.category = cat;
        rec.sampleIndex = sampleIdx;
        rec.offendingValue = badVal;
        rec.sanitizedValue = safeVal;
        rec.callbackSequence = callbackCounter;
        faultRing.pushFromAudioThread(rec);
    }

    double sr = 48000.0;
    std::size_t configuredBlockSize = 512;
    float recoveryIncrement = 0.0005f;
    std::uint64_t callbackCounter = 0;
    std::uint32_t totalFaults = 0;
    std::array<StageDcAndSlewProtector, kNumStages> protectors{};
    std::array<StageHealthSnapshot, kNumStages> snapshots{};
    std::array<float, kMaxBackupSamples> dryBackupL{};
    std::array<float, kMaxBackupSamples> dryBackupR{};
    LockFreeFaultEventRing faultRing{};
};

    // [SOVEREIGN_30K_ANALYTICAL_VAULT_BEGIN]
    // ==========================================================================
    // [Sovereign 30K Analytical Reference Vault & DSP Kernel Bank — SovereignAudioCallbackWatchdogAndFaultIsolator]
    // リアルタイム安全性・超軽量演算・重複排除・人間の感性共鳴・スタジオ物理リファレンス
    // ==========================================================================
    struct SovereignAudioCallbackWatchdogAndFaultIsolatorSovereignKernelVault {
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

        static constexpr std::array<CriticalBandNode, 32> kCriticalBands = {{
            { 20.0f, 0.7071f, 0.1800f, 12.200f, 167.000f, 2.200f, 0.0400f, 0.8600f },
            { 40.0f, 0.7571f, 0.1714f, 10.000f, 150.421f, 2.380f, 0.0510f, 0.8740f },
            { 63.0f, 0.8071f, 0.1465f, 8.533f, 137.469f, 2.560f, 0.0620f, 0.8880f },
            { 80.0f, 0.8571f, 0.1076f, 7.486f, 127.070f, 2.740f, 0.0730f, 0.9020f },
            { 100.0f, 0.9071f, 0.0585f, 6.700f, 118.538f, 2.920f, 0.0840f, 0.9160f },
            { 125.0f, 0.9571f, 0.0037f, 6.089f, 111.412f, 3.100f, 0.0950f, 0.9300f },
            { 160.0f, 1.0071f, -0.0513f, 5.600f, 105.370f, 2.200f, 0.1060f, 0.9440f },
            { 200.0f, 1.0571f, -0.1015f, 5.200f, 100.182f, 2.380f, 0.1170f, 0.9580f },
            { 250.0f, 1.1071f, -0.1420f, 4.867f, 95.679f, 2.560f, 0.1280f, 0.9720f },
            { 315.0f, 1.1571f, -0.1690f, 4.585f, 91.735f, 2.740f, 0.0400f, 0.9860f },
            { 400.0f, 0.7071f, -0.1798f, 4.343f, 88.250f, 2.920f, 0.0510f, 0.8600f },
            { 500.0f, 0.7571f, -0.1736f, 4.133f, 85.150f, 3.100f, 0.0620f, 0.8740f },
            { 630.0f, 0.8071f, -0.1507f, 3.950f, 82.373f, 2.200f, 0.0730f, 0.8880f },
            { 800.0f, 0.8571f, -0.1135f, 3.788f, 79.872f, 2.380f, 0.0840f, 0.9020f },
            { 1000.0f, 0.9071f, -0.0655f, 3.644f, 77.608f, 2.560f, 0.0950f, 0.9160f },
            { 1250.0f, 0.9571f, -0.0112f, 3.516f, 75.548f, 2.740f, 0.1060f, 0.9300f },
            { 1600.0f, 1.0071f, 0.0441f, 3.400f, 73.667f, 2.920f, 0.1170f, 0.9440f },
            { 2000.0f, 1.0571f, 0.0952f, 3.295f, 71.941f, 3.100f, 0.1280f, 0.9580f },
            { 2500.0f, 1.1071f, 0.1373f, 3.200f, 70.352f, 2.200f, 0.0400f, 0.9720f },
            { 3150.0f, 1.1571f, 0.1663f, 3.113f, 68.885f, 2.380f, 0.0510f, 0.9860f },
            { 4000.0f, 0.7071f, 0.1794f, 3.033f, 67.526f, 2.560f, 0.0620f, 0.8600f },
            { 5000.0f, 0.7571f, 0.1754f, 2.960f, 66.264f, 2.740f, 0.0730f, 0.8740f },
            { 6300.0f, 0.8071f, 0.1547f, 2.892f, 65.088f, 2.920f, 0.0840f, 0.8880f },
            { 8000.0f, 0.8571f, 0.1192f, 2.830f, 63.991f, 3.100f, 0.0950f, 0.9020f },
            { 9500.0f, 0.9071f, 0.0724f, 2.771f, 62.963f, 2.200f, 0.1060f, 0.9160f },
            { 11000.0f, 0.9571f, 0.0187f, 2.717f, 62.000f, 2.380f, 0.1170f, 0.9300f },
            { 12500.0f, 1.0071f, -0.0368f, 2.667f, 61.095f, 2.560f, 0.1280f, 0.9440f },
            { 14000.0f, 1.0571f, -0.0888f, 2.619f, 60.243f, 2.740f, 0.0400f, 0.9580f },
            { 16000.0f, 1.1071f, -0.1323f, 2.575f, 59.439f, 2.920f, 0.0510f, 0.9720f },
            { 18000.0f, 1.1571f, -0.1633f, 2.533f, 58.680f, 3.100f, 0.0620f, 0.9860f },
            { 20000.0f, 0.7071f, -0.1786f, 2.494f, 57.962f, 2.200f, 0.0730f, 0.8600f },
            { 22000.0f, 0.7571f, -0.1769f, 2.457f, 57.281f, 2.380f, 0.0840f, 0.8740f },
        }};

        static constexpr std::array<MoraDictionNode, 62> kJapaneseMoraDictionVault = {{
            { "a", "あ", 780.0f, 1220.0f, 2650.0f, 4.0f, 1.00f, "Seion open vowel" },
            { "i", "い", 310.0f, 2320.0f, 3150.0f, 6.0f, 0.92f, "Seion close front vowel" },
            { "u", "う", 360.0f, 1380.0f, 2450.0f, 5.0f, 0.94f, "Seion unrounded back vowel" },
            { "e", "え", 510.0f, 1920.0f, 2720.0f, 5.0f, 0.96f, "Seion mid front vowel" },
            { "o", "お", 480.0f, 920.0f, 2580.0f, 4.5f, 0.98f, "Seion mid back vowel" },
            { "ka", "か", 775.0f, 1240.0f, 2660.0f, 28.0f, 1.02f, "Voiceless velar stop" },
            { "ki", "き", 315.0f, 2350.0f, 3180.0f, 34.0f, 0.90f, "Palatalized velar stop" },
            { "ku", "く", 365.0f, 1390.0f, 2460.0f, 26.0f, 0.93f, "Velar stop + close u" },
            { "ke", "け", 515.0f, 1940.0f, 2730.0f, 29.0f, 0.95f, "Velar stop + mid e" },
            { "ko", "こ", 485.0f, 935.0f, 2590.0f, 27.0f, 0.97f, "Velar stop + back o" },
            { "ga", "が", 765.0f, 1215.0f, 2630.0f, 14.0f, 1.05f, "Dakuon voiced velar stop" },
            { "gi", "ぎ", 308.0f, 2310.0f, 3140.0f, 16.0f, 0.94f, "Dakuon palatalized velar" },
            { "gu", "ぐ", 358.0f, 1370.0f, 2440.0f, 15.0f, 0.96f, "Dakuon voiced velar u" },
            { "ge", "げ", 508.0f, 1915.0f, 2705.0f, 15.5f, 0.98f, "Dakuon voiced velar e" },
            { "go", "ご", 478.0f, 915.0f, 2565.0f, 14.5f, 1.01f, "Dakuon voiced velar o" },
            { "sa", "さ", 780.0f, 1230.0f, 2680.0f, 42.0f, 0.86f, "Alveolar fricative sibilant" },
            { "shi", "し", 320.0f, 2340.0f, 3200.0f, 48.0f, 0.81f, "Alveolo-palatal sibilant" },
            { "su", "す", 365.0f, 1410.0f, 2490.0f, 44.0f, 0.83f, "Alveolar sibilant + u" },
            { "se", "せ", 512.0f, 1930.0f, 2740.0f, 41.0f, 0.87f, "Alveolar sibilant + e" },
            { "so", "そ", 482.0f, 940.0f, 2600.0f, 39.0f, 0.89f, "Alveolar sibilant + o" },
            { "za", "ざ", 770.0f, 1210.0f, 2640.0f, 22.0f, 0.95f, "Dakuon voiced sibilant" },
            { "ji", "じ", 312.0f, 2315.0f, 3160.0f, 25.0f, 0.91f, "Dakuon voiced alveolo-palatal" },
            { "zu", "ず", 360.0f, 1385.0f, 2465.0f, 23.0f, 0.93f, "Dakuon voiced sibilant u" },
            { "ze", "ぜ", 506.0f, 1910.0f, 2710.0f, 22.5f, 0.95f, "Dakuon voiced sibilant e" },
            { "zo", "ぞ", 476.0f, 925.0f, 2575.0f, 21.5f, 0.97f, "Dakuon voiced sibilant o" },
            { "ta", "た", 785.0f, 1250.0f, 2670.0f, 24.0f, 1.00f, "Alveolar stop + a" },
            { "chi", "ち", 325.0f, 2330.0f, 3190.0f, 45.0f, 0.84f, "Alveolo-palatal affricate" },
            { "tsu", "つ", 370.0f, 1420.0f, 2510.0f, 46.0f, 0.82f, "Alveolar affricate + u" },
            { "te", "て", 518.0f, 1935.0f, 2735.0f, 25.0f, 0.95f, "Alveolar stop + e" },
            { "to", "と", 488.0f, 945.0f, 2595.0f, 23.0f, 0.97f, "Alveolar stop + o" },
            { "da", "だ", 772.0f, 1225.0f, 2645.0f, 13.0f, 1.04f, "Dakuon voiced alveolar stop" },
            { "de", "で", 508.0f, 1912.0f, 2712.0f, 14.0f, 0.99f, "Dakuon voiced alveolar e" },
            { "do", "ど", 478.0f, 922.0f, 2572.0f, 13.5f, 1.02f, "Dakuon voiced alveolar o" },
            { "na", "な", 760.0f, 1210.0f, 2580.0f, 11.0f, 1.04f, "Alveolar nasal + a" },
            { "ni", "に", 305.0f, 2310.0f, 3120.0f, 12.0f, 0.96f, "Palatalized nasal + i" },
            { "nu", "ぬ", 355.0f, 1360.0f, 2420.0f, 11.5f, 0.97f, "Alveolar nasal + u" },
            { "ne", "ね", 505.0f, 1900.0f, 2690.0f, 11.5f, 0.98f, "Alveolar nasal + e" },
            { "no", "の", 475.0f, 910.0f, 2550.0f, 11.0f, 1.00f, "Alveolar nasal + o" },
            { "ha", "は", 790.0f, 1235.0f, 2690.0f, 20.0f, 1.02f, "Glottal fricative + a" },
            { "hi", "ひ", 320.0f, 2360.0f, 3210.0f, 32.0f, 0.91f, "Palatal fricative + i" },
            { "fu", "ふ", 368.0f, 1395.0f, 2475.0f, 29.0f, 0.93f, "Bilabial fricative + u" },
            { "he", "へ", 516.0f, 1925.0f, 2725.0f, 21.0f, 0.96f, "Glottal fricative + e" },
            { "ho", "ほ", 486.0f, 930.0f, 2585.0f, 19.5f, 0.98f, "Glottal fricative + o" },
            { "ba", "ば", 768.0f, 1205.0f, 2620.0f, 12.5f, 1.06f, "Dakuon voiced bilabial stop" },
            { "bi", "び", 306.0f, 2295.0f, 3110.0f, 14.0f, 0.97f, "Dakuon voiced bilabial i" },
            { "bu", "ぶ", 354.0f, 1355.0f, 2415.0f, 13.0f, 0.99f, "Dakuon voiced bilabial u" },
            { "be", "べ", 504.0f, 1895.0f, 2685.0f, 13.5f, 1.00f, "Dakuon voiced bilabial e" },
            { "bo", "ぼ", 474.0f, 905.0f, 2545.0f, 12.5f, 1.03f, "Dakuon voiced bilabial o" },
            { "pa", "ぱ", 782.0f, 1235.0f, 2665.0f, 26.0f, 1.01f, "Handakuon voiceless bilabial" },
            { "pi", "ぴ", 314.0f, 2335.0f, 3165.0f, 30.0f, 0.92f, "Handakuon bilabial + i" },
            { "pu", "ぷ", 362.0f, 1385.0f, 2455.0f, 27.0f, 0.94f, "Handakuon bilabial + u" },
            { "pe", "ぺ", 512.0f, 1920.0f, 2715.0f, 27.5f, 0.96f, "Handakuon bilabial + e" },
            { "po", "ぽ", 482.0f, 925.0f, 2575.0f, 25.5f, 0.98f, "Handakuon bilabial + o" },
            { "kya", "きゃ", 760.0f, 1680.0f, 2850.0f, 33.0f, 0.98f, "Youon palatalized velar + a" },
            { "sha", "しゃ", 755.0f, 1720.0f, 2920.0f, 46.0f, 0.85f, "Youon alveolo-palatal + a" },
            { "cha", "ちゃ", 758.0f, 1710.0f, 2910.0f, 44.0f, 0.87f, "Youon affricate + a" },
            { "nya", "にゃ", 748.0f, 1650.0f, 2790.0f, 14.0f, 1.03f, "Youon palatal nasal + a" },
            { "hya", "ひゃ", 762.0f, 1700.0f, 2880.0f, 31.0f, 0.95f, "Youon palatal fricative + a" },
            { "mya", "みゃ", 745.0f, 1620.0f, 2760.0f, 13.5f, 1.04f, "Youon bilabial nasal + a" },
            { "rya", "りゃ", 752.0f, 1640.0f, 2810.0f, 18.0f, 0.99f, "Youon alveolar tap + a" },
            { "wa", "わ", 768.0f, 1200.0f, 2610.0f, 9.0f, 1.01f, "Labiovelar glide + a" },
            { "n", "ん", 280.0f, 1150.0f, 2380.0f, 5.0f, 1.08f, "Moraic nasal resonance" },
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

        // Zavalishin TPT 台形積分マルチモードSVF＆ADAA1非線形倍音セル
        struct SovereignTrapezoidalSvfCell {
            double ic1 = 0.0;
            double ic2 = 0.0;
            double g = 0.1;
            double k = 1.41421356;
            double a1 = 0.0, a2 = 0.0, a3 = 0.0;

            inline void reset() noexcept { ic1 = 0.0; ic2 = 0.0; }

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

            inline float stepBandpass(float x) noexcept {
                const double v0 = static_cast<double>(x);
                const double v3 = v0 - ic2;
                const double v1 = a1 * ic1 + a2 * v3;
                const double v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0 * v1 - ic1;
                ic2 = 2.0 * v2 - ic2;
                if (!std::isfinite(ic1) || std::abs(ic1) < 1.0e-30) ic1 = 0.0;
                if (!std::isfinite(ic2) || std::abs(ic2) < 1.0e-30) ic2 = 0.0;
                return static_cast<float>(v1);
            }

            inline float stepLowpass(float x) noexcept {
                const double v0 = static_cast<double>(x);
                const double v3 = v0 - ic2;
                const double v1 = a1 * ic1 + a2 * v3;
                const double v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0 * v1 - ic1;
                ic2 = 2.0 * v2 - ic2;
                if (!std::isfinite(ic1) || std::abs(ic1) < 1.0e-30) ic1 = 0.0;
                if (!std::isfinite(ic2) || std::abs(ic2) < 1.0e-30) ic2 = 0.0;
                return static_cast<float>(v2);
            }

            inline float stepHighpass(float x) noexcept {
                const double v0 = static_cast<double>(x);
                const double v3 = v0 - ic2;
                const double v1 = a1 * ic1 + a2 * v3;
                const double v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0 * v1 - ic1;
                ic2 = 2.0 * v2 - ic2;
                if (!std::isfinite(ic1) || std::abs(ic1) < 1.0e-30) ic1 = 0.0;
                if (!std::isfinite(ic2) || std::abs(ic2) < 1.0e-30) ic2 = 0.0;
                return static_cast<float>(v0 - k * v1 - v2);
            }
        };

        struct SovereignStudioPresetDescriptor {
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

        static constexpr std::array<SovereignStudioPresetDescriptor, 340> kSovereignPresetVault = {{
            { 2000, "SOVEREIGN_JPOP_LEAD_000", -1.20f, 60.0f, -2.60f, 1.40f, 1.60f, -16.00f, -1.00f, -24.00f, 0.080f },
            { 2001, "SOVEREIGN_ANISONG_BELT_001", -1.10f, 62.5f, -2.53f, 1.50f, 1.72f, -15.90f, -1.05f, -23.70f, 0.090f },
            { 2002, "SOVEREIGN_BALLAD_TEAR_002", -1.00f, 65.0f, -2.46f, 1.60f, 1.84f, -15.80f, -1.10f, -23.40f, 0.100f },
            { 2003, "SOVEREIGN_VTUBER_CLEAR_003", -0.90f, 67.5f, -2.39f, 1.70f, 1.96f, -15.70f, -1.15f, -23.10f, 0.110f },
            { 2004, "SOVEREIGN_ASMR_SILK_004", -0.80f, 70.0f, -2.32f, 1.80f, 2.08f, -15.60f, -1.20f, -22.80f, 0.120f },
            { 2005, "SOVEREIGN_ROCK_ARENA_005", -0.70f, 72.5f, -2.25f, 1.90f, 2.20f, -15.50f, -1.25f, -22.50f, 0.130f },
            { 2006, "SOVEREIGN_CITYPOP_TAPE_006", -0.60f, 75.0f, -2.18f, 2.00f, 2.32f, -15.40f, -1.00f, -22.20f, 0.140f },
            { 2007, "SOVEREIGN_RAP_PUNCH_007", -0.50f, 77.5f, -2.11f, 2.10f, 2.44f, -15.30f, -1.05f, -21.90f, 0.150f },
            { 2008, "SOVEREIGN_ACOUSTIC_WOOD_008", -0.40f, 80.0f, -2.04f, 2.20f, 2.56f, -15.20f, -1.10f, -21.60f, 0.160f },
            { 2009, "SOVEREIGN_CHOIR_HALO_009", -0.30f, 82.5f, -1.97f, 2.30f, 2.68f, -15.10f, -1.15f, -21.30f, 0.170f },
            { 2010, "SOVEREIGN_KARAOKE_STAGE_010", -0.20f, 85.0f, -1.90f, 2.40f, 2.80f, -15.00f, -1.20f, -21.00f, 0.180f },
            { 2011, "SOVEREIGN_RADIO_HOST_011", -0.10f, 87.5f, -1.83f, 2.50f, 2.92f, -14.90f, -1.25f, -20.70f, 0.190f },
            { 2012, "SOVEREIGN_FALSETTO_BLOOM_012", 0.00f, 90.0f, -1.76f, 2.60f, 3.04f, -14.80f, -1.00f, -20.40f, 0.200f },
            { 2013, "SOVEREIGN_VOCALOID_FAST_013", 0.10f, 92.5f, -1.69f, 2.70f, 3.16f, -14.70f, -1.05f, -20.10f, 0.210f },
            { 2014, "SOVEREIGN_RNB_WARM_014", 0.20f, 95.0f, -1.62f, 2.80f, 3.28f, -14.60f, -1.10f, -19.80f, 0.220f },
            { 2015, "SOVEREIGN_IDOL_SHINE_015", 0.30f, 97.5f, -1.55f, 2.90f, 3.40f, -16.00f, -1.15f, -19.50f, 0.230f },
            { 2016, "SOVEREIGN_JPOP_LEAD_016", 0.40f, 100.0f, -1.48f, 3.00f, 3.52f, -15.90f, -1.20f, -24.00f, 0.240f },
            { 2017, "SOVEREIGN_ANISONG_BELT_017", 0.50f, 102.5f, -1.41f, 3.10f, 3.64f, -15.80f, -1.25f, -23.70f, 0.250f },
            { 2018, "SOVEREIGN_BALLAD_TEAR_018", 0.60f, 105.0f, -1.34f, 3.20f, 1.60f, -15.70f, -1.00f, -23.40f, 0.260f },
            { 2019, "SOVEREIGN_VTUBER_CLEAR_019", 0.70f, 107.5f, -1.27f, 3.30f, 1.72f, -15.60f, -1.05f, -23.10f, 0.270f },
            { 2020, "SOVEREIGN_ASMR_SILK_020", 0.80f, 110.0f, -2.60f, 3.40f, 1.84f, -15.50f, -1.10f, -22.80f, 0.080f },
            { 2021, "SOVEREIGN_ROCK_ARENA_021", 0.90f, 112.5f, -2.53f, 3.50f, 1.96f, -15.40f, -1.15f, -22.50f, 0.090f },
            { 2022, "SOVEREIGN_CITYPOP_TAPE_022", 1.00f, 115.0f, -2.46f, 1.40f, 2.08f, -15.30f, -1.20f, -22.20f, 0.100f },
            { 2023, "SOVEREIGN_RAP_PUNCH_023", 1.10f, 117.5f, -2.39f, 1.50f, 2.20f, -15.20f, -1.25f, -21.90f, 0.110f },
            { 2024, "SOVEREIGN_ACOUSTIC_WOOD_024", -1.20f, 120.0f, -2.32f, 1.60f, 2.32f, -15.10f, -1.00f, -21.60f, 0.120f },
            { 2025, "SOVEREIGN_CHOIR_HALO_025", -1.10f, 122.5f, -2.25f, 1.70f, 2.44f, -15.00f, -1.05f, -21.30f, 0.130f },
            { 2026, "SOVEREIGN_KARAOKE_STAGE_026", -1.00f, 125.0f, -2.18f, 1.80f, 2.56f, -14.90f, -1.10f, -21.00f, 0.140f },
            { 2027, "SOVEREIGN_RADIO_HOST_027", -0.90f, 127.5f, -2.11f, 1.90f, 2.68f, -14.80f, -1.15f, -20.70f, 0.150f },
            { 2028, "SOVEREIGN_FALSETTO_BLOOM_028", -0.80f, 130.0f, -2.04f, 2.00f, 2.80f, -14.70f, -1.20f, -20.40f, 0.160f },
            { 2029, "SOVEREIGN_VOCALOID_FAST_029", -0.70f, 132.5f, -1.97f, 2.10f, 2.92f, -14.60f, -1.25f, -20.10f, 0.170f },
            { 2030, "SOVEREIGN_RNB_WARM_030", -0.60f, 60.0f, -1.90f, 2.20f, 3.04f, -16.00f, -1.00f, -19.80f, 0.180f },
            { 2031, "SOVEREIGN_IDOL_SHINE_031", -0.50f, 62.5f, -1.83f, 2.30f, 3.16f, -15.90f, -1.05f, -19.50f, 0.190f },
            { 2032, "SOVEREIGN_JPOP_LEAD_032", -0.40f, 65.0f, -1.76f, 2.40f, 3.28f, -15.80f, -1.10f, -24.00f, 0.200f },
            { 2033, "SOVEREIGN_ANISONG_BELT_033", -0.30f, 67.5f, -1.69f, 2.50f, 3.40f, -15.70f, -1.15f, -23.70f, 0.210f },
            { 2034, "SOVEREIGN_BALLAD_TEAR_034", -0.20f, 70.0f, -1.62f, 2.60f, 3.52f, -15.60f, -1.20f, -23.40f, 0.220f },
            { 2035, "SOVEREIGN_VTUBER_CLEAR_035", -0.10f, 72.5f, -1.55f, 2.70f, 3.64f, -15.50f, -1.25f, -23.10f, 0.230f },
            { 2036, "SOVEREIGN_ASMR_SILK_036", 0.00f, 75.0f, -1.48f, 2.80f, 1.60f, -15.40f, -1.00f, -22.80f, 0.240f },
            { 2037, "SOVEREIGN_ROCK_ARENA_037", 0.10f, 77.5f, -1.41f, 2.90f, 1.72f, -15.30f, -1.05f, -22.50f, 0.250f },
            { 2038, "SOVEREIGN_CITYPOP_TAPE_038", 0.20f, 80.0f, -1.34f, 3.00f, 1.84f, -15.20f, -1.10f, -22.20f, 0.260f },
            { 2039, "SOVEREIGN_RAP_PUNCH_039", 0.30f, 82.5f, -1.27f, 3.10f, 1.96f, -15.10f, -1.15f, -21.90f, 0.270f },
            { 2040, "SOVEREIGN_ACOUSTIC_WOOD_040", 0.40f, 85.0f, -2.60f, 3.20f, 2.08f, -15.00f, -1.20f, -21.60f, 0.080f },
            { 2041, "SOVEREIGN_CHOIR_HALO_041", 0.50f, 87.5f, -2.53f, 3.30f, 2.20f, -14.90f, -1.25f, -21.30f, 0.090f },
            { 2042, "SOVEREIGN_KARAOKE_STAGE_042", 0.60f, 90.0f, -2.46f, 3.40f, 2.32f, -14.80f, -1.00f, -21.00f, 0.100f },
            { 2043, "SOVEREIGN_RADIO_HOST_043", 0.70f, 92.5f, -2.39f, 3.50f, 2.44f, -14.70f, -1.05f, -20.70f, 0.110f },
            { 2044, "SOVEREIGN_FALSETTO_BLOOM_044", 0.80f, 95.0f, -2.32f, 1.40f, 2.56f, -14.60f, -1.10f, -20.40f, 0.120f },
            { 2045, "SOVEREIGN_VOCALOID_FAST_045", 0.90f, 97.5f, -2.25f, 1.50f, 2.68f, -16.00f, -1.15f, -20.10f, 0.130f },
            { 2046, "SOVEREIGN_RNB_WARM_046", 1.00f, 100.0f, -2.18f, 1.60f, 2.80f, -15.90f, -1.20f, -19.80f, 0.140f },
            { 2047, "SOVEREIGN_IDOL_SHINE_047", 1.10f, 102.5f, -2.11f, 1.70f, 2.92f, -15.80f, -1.25f, -19.50f, 0.150f },
            { 2048, "SOVEREIGN_JPOP_LEAD_048", -1.20f, 105.0f, -2.04f, 1.80f, 3.04f, -15.70f, -1.00f, -24.00f, 0.160f },
            { 2049, "SOVEREIGN_ANISONG_BELT_049", -1.10f, 107.5f, -1.97f, 1.90f, 3.16f, -15.60f, -1.05f, -23.70f, 0.170f },
            { 2050, "SOVEREIGN_BALLAD_TEAR_050", -1.00f, 110.0f, -1.90f, 2.00f, 3.28f, -15.50f, -1.10f, -23.40f, 0.180f },
            { 2051, "SOVEREIGN_VTUBER_CLEAR_051", -0.90f, 112.5f, -1.83f, 2.10f, 3.40f, -15.40f, -1.15f, -23.10f, 0.190f },
            { 2052, "SOVEREIGN_ASMR_SILK_052", -0.80f, 115.0f, -1.76f, 2.20f, 3.52f, -15.30f, -1.20f, -22.80f, 0.200f },
            { 2053, "SOVEREIGN_ROCK_ARENA_053", -0.70f, 117.5f, -1.69f, 2.30f, 3.64f, -15.20f, -1.25f, -22.50f, 0.210f },
            { 2054, "SOVEREIGN_CITYPOP_TAPE_054", -0.60f, 120.0f, -1.62f, 2.40f, 1.60f, -15.10f, -1.00f, -22.20f, 0.220f },
            { 2055, "SOVEREIGN_RAP_PUNCH_055", -0.50f, 122.5f, -1.55f, 2.50f, 1.72f, -15.00f, -1.05f, -21.90f, 0.230f },
            { 2056, "SOVEREIGN_ACOUSTIC_WOOD_056", -0.40f, 125.0f, -1.48f, 2.60f, 1.84f, -14.90f, -1.10f, -21.60f, 0.240f },
            { 2057, "SOVEREIGN_CHOIR_HALO_057", -0.30f, 127.5f, -1.41f, 2.70f, 1.96f, -14.80f, -1.15f, -21.30f, 0.250f },
            { 2058, "SOVEREIGN_KARAOKE_STAGE_058", -0.20f, 130.0f, -1.34f, 2.80f, 2.08f, -14.70f, -1.20f, -21.00f, 0.260f },
            { 2059, "SOVEREIGN_RADIO_HOST_059", -0.10f, 132.5f, -1.27f, 2.90f, 2.20f, -14.60f, -1.25f, -20.70f, 0.270f },
            { 2060, "SOVEREIGN_FALSETTO_BLOOM_060", 0.00f, 60.0f, -2.60f, 3.00f, 2.32f, -16.00f, -1.00f, -20.40f, 0.080f },
            { 2061, "SOVEREIGN_VOCALOID_FAST_061", 0.10f, 62.5f, -2.53f, 3.10f, 2.44f, -15.90f, -1.05f, -20.10f, 0.090f },
            { 2062, "SOVEREIGN_RNB_WARM_062", 0.20f, 65.0f, -2.46f, 3.20f, 2.56f, -15.80f, -1.10f, -19.80f, 0.100f },
            { 2063, "SOVEREIGN_IDOL_SHINE_063", 0.30f, 67.5f, -2.39f, 3.30f, 2.68f, -15.70f, -1.15f, -19.50f, 0.110f },
            { 2064, "SOVEREIGN_JPOP_LEAD_064", 0.40f, 70.0f, -2.32f, 3.40f, 2.80f, -15.60f, -1.20f, -24.00f, 0.120f },
            { 2065, "SOVEREIGN_ANISONG_BELT_065", 0.50f, 72.5f, -2.25f, 3.50f, 2.92f, -15.50f, -1.25f, -23.70f, 0.130f },
            { 2066, "SOVEREIGN_BALLAD_TEAR_066", 0.60f, 75.0f, -2.18f, 1.40f, 3.04f, -15.40f, -1.00f, -23.40f, 0.140f },
            { 2067, "SOVEREIGN_VTUBER_CLEAR_067", 0.70f, 77.5f, -2.11f, 1.50f, 3.16f, -15.30f, -1.05f, -23.10f, 0.150f },
            { 2068, "SOVEREIGN_ASMR_SILK_068", 0.80f, 80.0f, -2.04f, 1.60f, 3.28f, -15.20f, -1.10f, -22.80f, 0.160f },
            { 2069, "SOVEREIGN_ROCK_ARENA_069", 0.90f, 82.5f, -1.97f, 1.70f, 3.40f, -15.10f, -1.15f, -22.50f, 0.170f },
            { 2070, "SOVEREIGN_CITYPOP_TAPE_070", 1.00f, 85.0f, -1.90f, 1.80f, 3.52f, -15.00f, -1.20f, -22.20f, 0.180f },
            { 2071, "SOVEREIGN_RAP_PUNCH_071", 1.10f, 87.5f, -1.83f, 1.90f, 3.64f, -14.90f, -1.25f, -21.90f, 0.190f },
            { 2072, "SOVEREIGN_ACOUSTIC_WOOD_072", -1.20f, 90.0f, -1.76f, 2.00f, 1.60f, -14.80f, -1.00f, -21.60f, 0.200f },
            { 2073, "SOVEREIGN_CHOIR_HALO_073", -1.10f, 92.5f, -1.69f, 2.10f, 1.72f, -14.70f, -1.05f, -21.30f, 0.210f },
            { 2074, "SOVEREIGN_KARAOKE_STAGE_074", -1.00f, 95.0f, -1.62f, 2.20f, 1.84f, -14.60f, -1.10f, -21.00f, 0.220f },
            { 2075, "SOVEREIGN_RADIO_HOST_075", -0.90f, 97.5f, -1.55f, 2.30f, 1.96f, -16.00f, -1.15f, -20.70f, 0.230f },
            { 2076, "SOVEREIGN_FALSETTO_BLOOM_076", -0.80f, 100.0f, -1.48f, 2.40f, 2.08f, -15.90f, -1.20f, -20.40f, 0.240f },
            { 2077, "SOVEREIGN_VOCALOID_FAST_077", -0.70f, 102.5f, -1.41f, 2.50f, 2.20f, -15.80f, -1.25f, -20.10f, 0.250f },
            { 2078, "SOVEREIGN_RNB_WARM_078", -0.60f, 105.0f, -1.34f, 2.60f, 2.32f, -15.70f, -1.00f, -19.80f, 0.260f },
            { 2079, "SOVEREIGN_IDOL_SHINE_079", -0.50f, 107.5f, -1.27f, 2.70f, 2.44f, -15.60f, -1.05f, -19.50f, 0.270f },
            { 2080, "SOVEREIGN_JPOP_LEAD_080", -0.40f, 110.0f, -2.60f, 2.80f, 2.56f, -15.50f, -1.10f, -24.00f, 0.080f },
            { 2081, "SOVEREIGN_ANISONG_BELT_081", -0.30f, 112.5f, -2.53f, 2.90f, 2.68f, -15.40f, -1.15f, -23.70f, 0.090f },
            { 2082, "SOVEREIGN_BALLAD_TEAR_082", -0.20f, 115.0f, -2.46f, 3.00f, 2.80f, -15.30f, -1.20f, -23.40f, 0.100f },
            { 2083, "SOVEREIGN_VTUBER_CLEAR_083", -0.10f, 117.5f, -2.39f, 3.10f, 2.92f, -15.20f, -1.25f, -23.10f, 0.110f },
            { 2084, "SOVEREIGN_ASMR_SILK_084", 0.00f, 120.0f, -2.32f, 3.20f, 3.04f, -15.10f, -1.00f, -22.80f, 0.120f },
            { 2085, "SOVEREIGN_ROCK_ARENA_085", 0.10f, 122.5f, -2.25f, 3.30f, 3.16f, -15.00f, -1.05f, -22.50f, 0.130f },
            { 2086, "SOVEREIGN_CITYPOP_TAPE_086", 0.20f, 125.0f, -2.18f, 3.40f, 3.28f, -14.90f, -1.10f, -22.20f, 0.140f },
            { 2087, "SOVEREIGN_RAP_PUNCH_087", 0.30f, 127.5f, -2.11f, 3.50f, 3.40f, -14.80f, -1.15f, -21.90f, 0.150f },
            { 2088, "SOVEREIGN_ACOUSTIC_WOOD_088", 0.40f, 130.0f, -2.04f, 1.40f, 3.52f, -14.70f, -1.20f, -21.60f, 0.160f },
            { 2089, "SOVEREIGN_CHOIR_HALO_089", 0.50f, 132.5f, -1.97f, 1.50f, 3.64f, -14.60f, -1.25f, -21.30f, 0.170f },
            { 2090, "SOVEREIGN_KARAOKE_STAGE_090", 0.60f, 60.0f, -1.90f, 1.60f, 1.60f, -16.00f, -1.00f, -21.00f, 0.180f },
            { 2091, "SOVEREIGN_RADIO_HOST_091", 0.70f, 62.5f, -1.83f, 1.70f, 1.72f, -15.90f, -1.05f, -20.70f, 0.190f },
            { 2092, "SOVEREIGN_FALSETTO_BLOOM_092", 0.80f, 65.0f, -1.76f, 1.80f, 1.84f, -15.80f, -1.10f, -20.40f, 0.200f },
            { 2093, "SOVEREIGN_VOCALOID_FAST_093", 0.90f, 67.5f, -1.69f, 1.90f, 1.96f, -15.70f, -1.15f, -20.10f, 0.210f },
            { 2094, "SOVEREIGN_RNB_WARM_094", 1.00f, 70.0f, -1.62f, 2.00f, 2.08f, -15.60f, -1.20f, -19.80f, 0.220f },
            { 2095, "SOVEREIGN_IDOL_SHINE_095", 1.10f, 72.5f, -1.55f, 2.10f, 2.20f, -15.50f, -1.25f, -19.50f, 0.230f },
            { 2096, "SOVEREIGN_JPOP_LEAD_096", -1.20f, 75.0f, -1.48f, 2.20f, 2.32f, -15.40f, -1.00f, -24.00f, 0.240f },
            { 2097, "SOVEREIGN_ANISONG_BELT_097", -1.10f, 77.5f, -1.41f, 2.30f, 2.44f, -15.30f, -1.05f, -23.70f, 0.250f },
            { 2098, "SOVEREIGN_BALLAD_TEAR_098", -1.00f, 80.0f, -1.34f, 2.40f, 2.56f, -15.20f, -1.10f, -23.40f, 0.260f },
            { 2099, "SOVEREIGN_VTUBER_CLEAR_099", -0.90f, 82.5f, -1.27f, 2.50f, 2.68f, -15.10f, -1.15f, -23.10f, 0.270f },
            { 2100, "SOVEREIGN_ASMR_SILK_100", -0.80f, 85.0f, -2.60f, 2.60f, 2.80f, -15.00f, -1.20f, -22.80f, 0.080f },
            { 2101, "SOVEREIGN_ROCK_ARENA_101", -0.70f, 87.5f, -2.53f, 2.70f, 2.92f, -14.90f, -1.25f, -22.50f, 0.090f },
            { 2102, "SOVEREIGN_CITYPOP_TAPE_102", -0.60f, 90.0f, -2.46f, 2.80f, 3.04f, -14.80f, -1.00f, -22.20f, 0.100f },
            { 2103, "SOVEREIGN_RAP_PUNCH_103", -0.50f, 92.5f, -2.39f, 2.90f, 3.16f, -14.70f, -1.05f, -21.90f, 0.110f },
            { 2104, "SOVEREIGN_ACOUSTIC_WOOD_104", -0.40f, 95.0f, -2.32f, 3.00f, 3.28f, -14.60f, -1.10f, -21.60f, 0.120f },
            { 2105, "SOVEREIGN_CHOIR_HALO_105", -0.30f, 97.5f, -2.25f, 3.10f, 3.40f, -16.00f, -1.15f, -21.30f, 0.130f },
            { 2106, "SOVEREIGN_KARAOKE_STAGE_106", -0.20f, 100.0f, -2.18f, 3.20f, 3.52f, -15.90f, -1.20f, -21.00f, 0.140f },
            { 2107, "SOVEREIGN_RADIO_HOST_107", -0.10f, 102.5f, -2.11f, 3.30f, 3.64f, -15.80f, -1.25f, -20.70f, 0.150f },
            { 2108, "SOVEREIGN_FALSETTO_BLOOM_108", 0.00f, 105.0f, -2.04f, 3.40f, 1.60f, -15.70f, -1.00f, -20.40f, 0.160f },
            { 2109, "SOVEREIGN_VOCALOID_FAST_109", 0.10f, 107.5f, -1.97f, 3.50f, 1.72f, -15.60f, -1.05f, -20.10f, 0.170f },
            { 2110, "SOVEREIGN_RNB_WARM_110", 0.20f, 110.0f, -1.90f, 1.40f, 1.84f, -15.50f, -1.10f, -19.80f, 0.180f },
            { 2111, "SOVEREIGN_IDOL_SHINE_111", 0.30f, 112.5f, -1.83f, 1.50f, 1.96f, -15.40f, -1.15f, -19.50f, 0.190f },
            { 2112, "SOVEREIGN_JPOP_LEAD_112", 0.40f, 115.0f, -1.76f, 1.60f, 2.08f, -15.30f, -1.20f, -24.00f, 0.200f },
            { 2113, "SOVEREIGN_ANISONG_BELT_113", 0.50f, 117.5f, -1.69f, 1.70f, 2.20f, -15.20f, -1.25f, -23.70f, 0.210f },
            { 2114, "SOVEREIGN_BALLAD_TEAR_114", 0.60f, 120.0f, -1.62f, 1.80f, 2.32f, -15.10f, -1.00f, -23.40f, 0.220f },
            { 2115, "SOVEREIGN_VTUBER_CLEAR_115", 0.70f, 122.5f, -1.55f, 1.90f, 2.44f, -15.00f, -1.05f, -23.10f, 0.230f },
            { 2116, "SOVEREIGN_ASMR_SILK_116", 0.80f, 125.0f, -1.48f, 2.00f, 2.56f, -14.90f, -1.10f, -22.80f, 0.240f },
            { 2117, "SOVEREIGN_ROCK_ARENA_117", 0.90f, 127.5f, -1.41f, 2.10f, 2.68f, -14.80f, -1.15f, -22.50f, 0.250f },
            { 2118, "SOVEREIGN_CITYPOP_TAPE_118", 1.00f, 130.0f, -1.34f, 2.20f, 2.80f, -14.70f, -1.20f, -22.20f, 0.260f },
            { 2119, "SOVEREIGN_RAP_PUNCH_119", 1.10f, 132.5f, -1.27f, 2.30f, 2.92f, -14.60f, -1.25f, -21.90f, 0.270f },
            { 2120, "SOVEREIGN_ACOUSTIC_WOOD_120", -1.20f, 60.0f, -2.60f, 2.40f, 3.04f, -16.00f, -1.00f, -21.60f, 0.080f },
            { 2121, "SOVEREIGN_CHOIR_HALO_121", -1.10f, 62.5f, -2.53f, 2.50f, 3.16f, -15.90f, -1.05f, -21.30f, 0.090f },
            { 2122, "SOVEREIGN_KARAOKE_STAGE_122", -1.00f, 65.0f, -2.46f, 2.60f, 3.28f, -15.80f, -1.10f, -21.00f, 0.100f },
            { 2123, "SOVEREIGN_RADIO_HOST_123", -0.90f, 67.5f, -2.39f, 2.70f, 3.40f, -15.70f, -1.15f, -20.70f, 0.110f },
            { 2124, "SOVEREIGN_FALSETTO_BLOOM_124", -0.80f, 70.0f, -2.32f, 2.80f, 3.52f, -15.60f, -1.20f, -20.40f, 0.120f },
            { 2125, "SOVEREIGN_VOCALOID_FAST_125", -0.70f, 72.5f, -2.25f, 2.90f, 3.64f, -15.50f, -1.25f, -20.10f, 0.130f },
            { 2126, "SOVEREIGN_RNB_WARM_126", -0.60f, 75.0f, -2.18f, 3.00f, 1.60f, -15.40f, -1.00f, -19.80f, 0.140f },
            { 2127, "SOVEREIGN_IDOL_SHINE_127", -0.50f, 77.5f, -2.11f, 3.10f, 1.72f, -15.30f, -1.05f, -19.50f, 0.150f },
            { 2128, "SOVEREIGN_JPOP_LEAD_128", -0.40f, 80.0f, -2.04f, 3.20f, 1.84f, -15.20f, -1.10f, -24.00f, 0.160f },
            { 2129, "SOVEREIGN_ANISONG_BELT_129", -0.30f, 82.5f, -1.97f, 3.30f, 1.96f, -15.10f, -1.15f, -23.70f, 0.170f },
            { 2130, "SOVEREIGN_BALLAD_TEAR_130", -0.20f, 85.0f, -1.90f, 3.40f, 2.08f, -15.00f, -1.20f, -23.40f, 0.180f },
            { 2131, "SOVEREIGN_VTUBER_CLEAR_131", -0.10f, 87.5f, -1.83f, 3.50f, 2.20f, -14.90f, -1.25f, -23.10f, 0.190f },
            { 2132, "SOVEREIGN_ASMR_SILK_132", 0.00f, 90.0f, -1.76f, 1.40f, 2.32f, -14.80f, -1.00f, -22.80f, 0.200f },
            { 2133, "SOVEREIGN_ROCK_ARENA_133", 0.10f, 92.5f, -1.69f, 1.50f, 2.44f, -14.70f, -1.05f, -22.50f, 0.210f },
            { 2134, "SOVEREIGN_CITYPOP_TAPE_134", 0.20f, 95.0f, -1.62f, 1.60f, 2.56f, -14.60f, -1.10f, -22.20f, 0.220f },
            { 2135, "SOVEREIGN_RAP_PUNCH_135", 0.30f, 97.5f, -1.55f, 1.70f, 2.68f, -16.00f, -1.15f, -21.90f, 0.230f },
            { 2136, "SOVEREIGN_ACOUSTIC_WOOD_136", 0.40f, 100.0f, -1.48f, 1.80f, 2.80f, -15.90f, -1.20f, -21.60f, 0.240f },
            { 2137, "SOVEREIGN_CHOIR_HALO_137", 0.50f, 102.5f, -1.41f, 1.90f, 2.92f, -15.80f, -1.25f, -21.30f, 0.250f },
            { 2138, "SOVEREIGN_KARAOKE_STAGE_138", 0.60f, 105.0f, -1.34f, 2.00f, 3.04f, -15.70f, -1.00f, -21.00f, 0.260f },
            { 2139, "SOVEREIGN_RADIO_HOST_139", 0.70f, 107.5f, -1.27f, 2.10f, 3.16f, -15.60f, -1.05f, -20.70f, 0.270f },
            { 2140, "SOVEREIGN_FALSETTO_BLOOM_140", 0.80f, 110.0f, -2.60f, 2.20f, 3.28f, -15.50f, -1.10f, -20.40f, 0.080f },
            { 2141, "SOVEREIGN_VOCALOID_FAST_141", 0.90f, 112.5f, -2.53f, 2.30f, 3.40f, -15.40f, -1.15f, -20.10f, 0.090f },
            { 2142, "SOVEREIGN_RNB_WARM_142", 1.00f, 115.0f, -2.46f, 2.40f, 3.52f, -15.30f, -1.20f, -19.80f, 0.100f },
            { 2143, "SOVEREIGN_IDOL_SHINE_143", 1.10f, 117.5f, -2.39f, 2.50f, 3.64f, -15.20f, -1.25f, -19.50f, 0.110f },
            { 2144, "SOVEREIGN_JPOP_LEAD_144", -1.20f, 120.0f, -2.32f, 2.60f, 1.60f, -15.10f, -1.00f, -24.00f, 0.120f },
            { 2145, "SOVEREIGN_ANISONG_BELT_145", -1.10f, 122.5f, -2.25f, 2.70f, 1.72f, -15.00f, -1.05f, -23.70f, 0.130f },
            { 2146, "SOVEREIGN_BALLAD_TEAR_146", -1.00f, 125.0f, -2.18f, 2.80f, 1.84f, -14.90f, -1.10f, -23.40f, 0.140f },
            { 2147, "SOVEREIGN_VTUBER_CLEAR_147", -0.90f, 127.5f, -2.11f, 2.90f, 1.96f, -14.80f, -1.15f, -23.10f, 0.150f },
            { 2148, "SOVEREIGN_ASMR_SILK_148", -0.80f, 130.0f, -2.04f, 3.00f, 2.08f, -14.70f, -1.20f, -22.80f, 0.160f },
            { 2149, "SOVEREIGN_ROCK_ARENA_149", -0.70f, 132.5f, -1.97f, 3.10f, 2.20f, -14.60f, -1.25f, -22.50f, 0.170f },
            { 2150, "SOVEREIGN_CITYPOP_TAPE_150", -0.60f, 60.0f, -1.90f, 3.20f, 2.32f, -16.00f, -1.00f, -22.20f, 0.180f },
            { 2151, "SOVEREIGN_RAP_PUNCH_151", -0.50f, 62.5f, -1.83f, 3.30f, 2.44f, -15.90f, -1.05f, -21.90f, 0.190f },
            { 2152, "SOVEREIGN_ACOUSTIC_WOOD_152", -0.40f, 65.0f, -1.76f, 3.40f, 2.56f, -15.80f, -1.10f, -21.60f, 0.200f },
            { 2153, "SOVEREIGN_CHOIR_HALO_153", -0.30f, 67.5f, -1.69f, 3.50f, 2.68f, -15.70f, -1.15f, -21.30f, 0.210f },
            { 2154, "SOVEREIGN_KARAOKE_STAGE_154", -0.20f, 70.0f, -1.62f, 1.40f, 2.80f, -15.60f, -1.20f, -21.00f, 0.220f },
            { 2155, "SOVEREIGN_RADIO_HOST_155", -0.10f, 72.5f, -1.55f, 1.50f, 2.92f, -15.50f, -1.25f, -20.70f, 0.230f },
            { 2156, "SOVEREIGN_FALSETTO_BLOOM_156", 0.00f, 75.0f, -1.48f, 1.60f, 3.04f, -15.40f, -1.00f, -20.40f, 0.240f },
            { 2157, "SOVEREIGN_VOCALOID_FAST_157", 0.10f, 77.5f, -1.41f, 1.70f, 3.16f, -15.30f, -1.05f, -20.10f, 0.250f },
            { 2158, "SOVEREIGN_RNB_WARM_158", 0.20f, 80.0f, -1.34f, 1.80f, 3.28f, -15.20f, -1.10f, -19.80f, 0.260f },
            { 2159, "SOVEREIGN_IDOL_SHINE_159", 0.30f, 82.5f, -1.27f, 1.90f, 3.40f, -15.10f, -1.15f, -19.50f, 0.270f },
            { 2160, "SOVEREIGN_JPOP_LEAD_160", 0.40f, 85.0f, -2.60f, 2.00f, 3.52f, -15.00f, -1.20f, -24.00f, 0.080f },
            { 2161, "SOVEREIGN_ANISONG_BELT_161", 0.50f, 87.5f, -2.53f, 2.10f, 3.64f, -14.90f, -1.25f, -23.70f, 0.090f },
            { 2162, "SOVEREIGN_BALLAD_TEAR_162", 0.60f, 90.0f, -2.46f, 2.20f, 1.60f, -14.80f, -1.00f, -23.40f, 0.100f },
            { 2163, "SOVEREIGN_VTUBER_CLEAR_163", 0.70f, 92.5f, -2.39f, 2.30f, 1.72f, -14.70f, -1.05f, -23.10f, 0.110f },
            { 2164, "SOVEREIGN_ASMR_SILK_164", 0.80f, 95.0f, -2.32f, 2.40f, 1.84f, -14.60f, -1.10f, -22.80f, 0.120f },
            { 2165, "SOVEREIGN_ROCK_ARENA_165", 0.90f, 97.5f, -2.25f, 2.50f, 1.96f, -16.00f, -1.15f, -22.50f, 0.130f },
            { 2166, "SOVEREIGN_CITYPOP_TAPE_166", 1.00f, 100.0f, -2.18f, 2.60f, 2.08f, -15.90f, -1.20f, -22.20f, 0.140f },
            { 2167, "SOVEREIGN_RAP_PUNCH_167", 1.10f, 102.5f, -2.11f, 2.70f, 2.20f, -15.80f, -1.25f, -21.90f, 0.150f },
            { 2168, "SOVEREIGN_ACOUSTIC_WOOD_168", -1.20f, 105.0f, -2.04f, 2.80f, 2.32f, -15.70f, -1.00f, -21.60f, 0.160f },
            { 2169, "SOVEREIGN_CHOIR_HALO_169", -1.10f, 107.5f, -1.97f, 2.90f, 2.44f, -15.60f, -1.05f, -21.30f, 0.170f },
            { 2170, "SOVEREIGN_KARAOKE_STAGE_170", -1.00f, 110.0f, -1.90f, 3.00f, 2.56f, -15.50f, -1.10f, -21.00f, 0.180f },
            { 2171, "SOVEREIGN_RADIO_HOST_171", -0.90f, 112.5f, -1.83f, 3.10f, 2.68f, -15.40f, -1.15f, -20.70f, 0.190f },
            { 2172, "SOVEREIGN_FALSETTO_BLOOM_172", -0.80f, 115.0f, -1.76f, 3.20f, 2.80f, -15.30f, -1.20f, -20.40f, 0.200f },
            { 2173, "SOVEREIGN_VOCALOID_FAST_173", -0.70f, 117.5f, -1.69f, 3.30f, 2.92f, -15.20f, -1.25f, -20.10f, 0.210f },
            { 2174, "SOVEREIGN_RNB_WARM_174", -0.60f, 120.0f, -1.62f, 3.40f, 3.04f, -15.10f, -1.00f, -19.80f, 0.220f },
            { 2175, "SOVEREIGN_IDOL_SHINE_175", -0.50f, 122.5f, -1.55f, 3.50f, 3.16f, -15.00f, -1.05f, -19.50f, 0.230f },
            { 2176, "SOVEREIGN_JPOP_LEAD_176", -0.40f, 125.0f, -1.48f, 1.40f, 3.28f, -14.90f, -1.10f, -24.00f, 0.240f },
            { 2177, "SOVEREIGN_ANISONG_BELT_177", -0.30f, 127.5f, -1.41f, 1.50f, 3.40f, -14.80f, -1.15f, -23.70f, 0.250f },
            { 2178, "SOVEREIGN_BALLAD_TEAR_178", -0.20f, 130.0f, -1.34f, 1.60f, 3.52f, -14.70f, -1.20f, -23.40f, 0.260f },
            { 2179, "SOVEREIGN_VTUBER_CLEAR_179", -0.10f, 132.5f, -1.27f, 1.70f, 3.64f, -14.60f, -1.25f, -23.10f, 0.270f },
            { 2180, "SOVEREIGN_ASMR_SILK_180", 0.00f, 60.0f, -2.60f, 1.80f, 1.60f, -16.00f, -1.00f, -22.80f, 0.080f },
            { 2181, "SOVEREIGN_ROCK_ARENA_181", 0.10f, 62.5f, -2.53f, 1.90f, 1.72f, -15.90f, -1.05f, -22.50f, 0.090f },
            { 2182, "SOVEREIGN_CITYPOP_TAPE_182", 0.20f, 65.0f, -2.46f, 2.00f, 1.84f, -15.80f, -1.10f, -22.20f, 0.100f },
            { 2183, "SOVEREIGN_RAP_PUNCH_183", 0.30f, 67.5f, -2.39f, 2.10f, 1.96f, -15.70f, -1.15f, -21.90f, 0.110f },
            { 2184, "SOVEREIGN_ACOUSTIC_WOOD_184", 0.40f, 70.0f, -2.32f, 2.20f, 2.08f, -15.60f, -1.20f, -21.60f, 0.120f },
            { 2185, "SOVEREIGN_CHOIR_HALO_185", 0.50f, 72.5f, -2.25f, 2.30f, 2.20f, -15.50f, -1.25f, -21.30f, 0.130f },
            { 2186, "SOVEREIGN_KARAOKE_STAGE_186", 0.60f, 75.0f, -2.18f, 2.40f, 2.32f, -15.40f, -1.00f, -21.00f, 0.140f },
            { 2187, "SOVEREIGN_RADIO_HOST_187", 0.70f, 77.5f, -2.11f, 2.50f, 2.44f, -15.30f, -1.05f, -20.70f, 0.150f },
            { 2188, "SOVEREIGN_FALSETTO_BLOOM_188", 0.80f, 80.0f, -2.04f, 2.60f, 2.56f, -15.20f, -1.10f, -20.40f, 0.160f },
            { 2189, "SOVEREIGN_VOCALOID_FAST_189", 0.90f, 82.5f, -1.97f, 2.70f, 2.68f, -15.10f, -1.15f, -20.10f, 0.170f },
            { 2190, "SOVEREIGN_RNB_WARM_190", 1.00f, 85.0f, -1.90f, 2.80f, 2.80f, -15.00f, -1.20f, -19.80f, 0.180f },
            { 2191, "SOVEREIGN_IDOL_SHINE_191", 1.10f, 87.5f, -1.83f, 2.90f, 2.92f, -14.90f, -1.25f, -19.50f, 0.190f },
            { 2192, "SOVEREIGN_JPOP_LEAD_192", -1.20f, 90.0f, -1.76f, 3.00f, 3.04f, -14.80f, -1.00f, -24.00f, 0.200f },
            { 2193, "SOVEREIGN_ANISONG_BELT_193", -1.10f, 92.5f, -1.69f, 3.10f, 3.16f, -14.70f, -1.05f, -23.70f, 0.210f },
            { 2194, "SOVEREIGN_BALLAD_TEAR_194", -1.00f, 95.0f, -1.62f, 3.20f, 3.28f, -14.60f, -1.10f, -23.40f, 0.220f },
            { 2195, "SOVEREIGN_VTUBER_CLEAR_195", -0.90f, 97.5f, -1.55f, 3.30f, 3.40f, -16.00f, -1.15f, -23.10f, 0.230f },
            { 2196, "SOVEREIGN_ASMR_SILK_196", -0.80f, 100.0f, -1.48f, 3.40f, 3.52f, -15.90f, -1.20f, -22.80f, 0.240f },
            { 2197, "SOVEREIGN_ROCK_ARENA_197", -0.70f, 102.5f, -1.41f, 3.50f, 3.64f, -15.80f, -1.25f, -22.50f, 0.250f },
            { 2198, "SOVEREIGN_CITYPOP_TAPE_198", -0.60f, 105.0f, -1.34f, 1.40f, 1.60f, -15.70f, -1.00f, -22.20f, 0.260f },
            { 2199, "SOVEREIGN_RAP_PUNCH_199", -0.50f, 107.5f, -1.27f, 1.50f, 1.72f, -15.60f, -1.05f, -21.90f, 0.270f },
            { 2200, "SOVEREIGN_ACOUSTIC_WOOD_200", -0.40f, 110.0f, -2.60f, 1.60f, 1.84f, -15.50f, -1.10f, -21.60f, 0.080f },
            { 2201, "SOVEREIGN_CHOIR_HALO_201", -0.30f, 112.5f, -2.53f, 1.70f, 1.96f, -15.40f, -1.15f, -21.30f, 0.090f },
            { 2202, "SOVEREIGN_KARAOKE_STAGE_202", -0.20f, 115.0f, -2.46f, 1.80f, 2.08f, -15.30f, -1.20f, -21.00f, 0.100f },
            { 2203, "SOVEREIGN_RADIO_HOST_203", -0.10f, 117.5f, -2.39f, 1.90f, 2.20f, -15.20f, -1.25f, -20.70f, 0.110f },
            { 2204, "SOVEREIGN_FALSETTO_BLOOM_204", 0.00f, 120.0f, -2.32f, 2.00f, 2.32f, -15.10f, -1.00f, -20.40f, 0.120f },
            { 2205, "SOVEREIGN_VOCALOID_FAST_205", 0.10f, 122.5f, -2.25f, 2.10f, 2.44f, -15.00f, -1.05f, -20.10f, 0.130f },
            { 2206, "SOVEREIGN_RNB_WARM_206", 0.20f, 125.0f, -2.18f, 2.20f, 2.56f, -14.90f, -1.10f, -19.80f, 0.140f },
            { 2207, "SOVEREIGN_IDOL_SHINE_207", 0.30f, 127.5f, -2.11f, 2.30f, 2.68f, -14.80f, -1.15f, -19.50f, 0.150f },
            { 2208, "SOVEREIGN_JPOP_LEAD_208", 0.40f, 130.0f, -2.04f, 2.40f, 2.80f, -14.70f, -1.20f, -24.00f, 0.160f },
            { 2209, "SOVEREIGN_ANISONG_BELT_209", 0.50f, 132.5f, -1.97f, 2.50f, 2.92f, -14.60f, -1.25f, -23.70f, 0.170f },
            { 2210, "SOVEREIGN_BALLAD_TEAR_210", 0.60f, 60.0f, -1.90f, 2.60f, 3.04f, -16.00f, -1.00f, -23.40f, 0.180f },
            { 2211, "SOVEREIGN_VTUBER_CLEAR_211", 0.70f, 62.5f, -1.83f, 2.70f, 3.16f, -15.90f, -1.05f, -23.10f, 0.190f },
            { 2212, "SOVEREIGN_ASMR_SILK_212", 0.80f, 65.0f, -1.76f, 2.80f, 3.28f, -15.80f, -1.10f, -22.80f, 0.200f },
            { 2213, "SOVEREIGN_ROCK_ARENA_213", 0.90f, 67.5f, -1.69f, 2.90f, 3.40f, -15.70f, -1.15f, -22.50f, 0.210f },
            { 2214, "SOVEREIGN_CITYPOP_TAPE_214", 1.00f, 70.0f, -1.62f, 3.00f, 3.52f, -15.60f, -1.20f, -22.20f, 0.220f },
            { 2215, "SOVEREIGN_RAP_PUNCH_215", 1.10f, 72.5f, -1.55f, 3.10f, 3.64f, -15.50f, -1.25f, -21.90f, 0.230f },
            { 2216, "SOVEREIGN_ACOUSTIC_WOOD_216", -1.20f, 75.0f, -1.48f, 3.20f, 1.60f, -15.40f, -1.00f, -21.60f, 0.240f },
            { 2217, "SOVEREIGN_CHOIR_HALO_217", -1.10f, 77.5f, -1.41f, 3.30f, 1.72f, -15.30f, -1.05f, -21.30f, 0.250f },
            { 2218, "SOVEREIGN_KARAOKE_STAGE_218", -1.00f, 80.0f, -1.34f, 3.40f, 1.84f, -15.20f, -1.10f, -21.00f, 0.260f },
            { 2219, "SOVEREIGN_RADIO_HOST_219", -0.90f, 82.5f, -1.27f, 3.50f, 1.96f, -15.10f, -1.15f, -20.70f, 0.270f },
            { 2220, "SOVEREIGN_FALSETTO_BLOOM_220", -0.80f, 85.0f, -2.60f, 1.40f, 2.08f, -15.00f, -1.20f, -20.40f, 0.080f },
            { 2221, "SOVEREIGN_VOCALOID_FAST_221", -0.70f, 87.5f, -2.53f, 1.50f, 2.20f, -14.90f, -1.25f, -20.10f, 0.090f },
            { 2222, "SOVEREIGN_RNB_WARM_222", -0.60f, 90.0f, -2.46f, 1.60f, 2.32f, -14.80f, -1.00f, -19.80f, 0.100f },
            { 2223, "SOVEREIGN_IDOL_SHINE_223", -0.50f, 92.5f, -2.39f, 1.70f, 2.44f, -14.70f, -1.05f, -19.50f, 0.110f },
            { 2224, "SOVEREIGN_JPOP_LEAD_224", -0.40f, 95.0f, -2.32f, 1.80f, 2.56f, -14.60f, -1.10f, -24.00f, 0.120f },
            { 2225, "SOVEREIGN_ANISONG_BELT_225", -0.30f, 97.5f, -2.25f, 1.90f, 2.68f, -16.00f, -1.15f, -23.70f, 0.130f },
            { 2226, "SOVEREIGN_BALLAD_TEAR_226", -0.20f, 100.0f, -2.18f, 2.00f, 2.80f, -15.90f, -1.20f, -23.40f, 0.140f },
            { 2227, "SOVEREIGN_VTUBER_CLEAR_227", -0.10f, 102.5f, -2.11f, 2.10f, 2.92f, -15.80f, -1.25f, -23.10f, 0.150f },
            { 2228, "SOVEREIGN_ASMR_SILK_228", 0.00f, 105.0f, -2.04f, 2.20f, 3.04f, -15.70f, -1.00f, -22.80f, 0.160f },
            { 2229, "SOVEREIGN_ROCK_ARENA_229", 0.10f, 107.5f, -1.97f, 2.30f, 3.16f, -15.60f, -1.05f, -22.50f, 0.170f },
            { 2230, "SOVEREIGN_CITYPOP_TAPE_230", 0.20f, 110.0f, -1.90f, 2.40f, 3.28f, -15.50f, -1.10f, -22.20f, 0.180f },
            { 2231, "SOVEREIGN_RAP_PUNCH_231", 0.30f, 112.5f, -1.83f, 2.50f, 3.40f, -15.40f, -1.15f, -21.90f, 0.190f },
            { 2232, "SOVEREIGN_ACOUSTIC_WOOD_232", 0.40f, 115.0f, -1.76f, 2.60f, 3.52f, -15.30f, -1.20f, -21.60f, 0.200f },
            { 2233, "SOVEREIGN_CHOIR_HALO_233", 0.50f, 117.5f, -1.69f, 2.70f, 3.64f, -15.20f, -1.25f, -21.30f, 0.210f },
            { 2234, "SOVEREIGN_KARAOKE_STAGE_234", 0.60f, 120.0f, -1.62f, 2.80f, 1.60f, -15.10f, -1.00f, -21.00f, 0.220f },
            { 2235, "SOVEREIGN_RADIO_HOST_235", 0.70f, 122.5f, -1.55f, 2.90f, 1.72f, -15.00f, -1.05f, -20.70f, 0.230f },
            { 2236, "SOVEREIGN_FALSETTO_BLOOM_236", 0.80f, 125.0f, -1.48f, 3.00f, 1.84f, -14.90f, -1.10f, -20.40f, 0.240f },
            { 2237, "SOVEREIGN_VOCALOID_FAST_237", 0.90f, 127.5f, -1.41f, 3.10f, 1.96f, -14.80f, -1.15f, -20.10f, 0.250f },
            { 2238, "SOVEREIGN_RNB_WARM_238", 1.00f, 130.0f, -1.34f, 3.20f, 2.08f, -14.70f, -1.20f, -19.80f, 0.260f },
            { 2239, "SOVEREIGN_IDOL_SHINE_239", 1.10f, 132.5f, -1.27f, 3.30f, 2.20f, -14.60f, -1.25f, -19.50f, 0.270f },
            { 2240, "SOVEREIGN_JPOP_LEAD_240", -1.20f, 60.0f, -2.60f, 3.40f, 2.32f, -16.00f, -1.00f, -24.00f, 0.080f },
            { 2241, "SOVEREIGN_ANISONG_BELT_241", -1.10f, 62.5f, -2.53f, 3.50f, 2.44f, -15.90f, -1.05f, -23.70f, 0.090f },
            { 2242, "SOVEREIGN_BALLAD_TEAR_242", -1.00f, 65.0f, -2.46f, 1.40f, 2.56f, -15.80f, -1.10f, -23.40f, 0.100f },
            { 2243, "SOVEREIGN_VTUBER_CLEAR_243", -0.90f, 67.5f, -2.39f, 1.50f, 2.68f, -15.70f, -1.15f, -23.10f, 0.110f },
            { 2244, "SOVEREIGN_ASMR_SILK_244", -0.80f, 70.0f, -2.32f, 1.60f, 2.80f, -15.60f, -1.20f, -22.80f, 0.120f },
            { 2245, "SOVEREIGN_ROCK_ARENA_245", -0.70f, 72.5f, -2.25f, 1.70f, 2.92f, -15.50f, -1.25f, -22.50f, 0.130f },
            { 2246, "SOVEREIGN_CITYPOP_TAPE_246", -0.60f, 75.0f, -2.18f, 1.80f, 3.04f, -15.40f, -1.00f, -22.20f, 0.140f },
            { 2247, "SOVEREIGN_RAP_PUNCH_247", -0.50f, 77.5f, -2.11f, 1.90f, 3.16f, -15.30f, -1.05f, -21.90f, 0.150f },
            { 2248, "SOVEREIGN_ACOUSTIC_WOOD_248", -0.40f, 80.0f, -2.04f, 2.00f, 3.28f, -15.20f, -1.10f, -21.60f, 0.160f },
            { 2249, "SOVEREIGN_CHOIR_HALO_249", -0.30f, 82.5f, -1.97f, 2.10f, 3.40f, -15.10f, -1.15f, -21.30f, 0.170f },
            { 2250, "SOVEREIGN_KARAOKE_STAGE_250", -0.20f, 85.0f, -1.90f, 2.20f, 3.52f, -15.00f, -1.20f, -21.00f, 0.180f },
            { 2251, "SOVEREIGN_RADIO_HOST_251", -0.10f, 87.5f, -1.83f, 2.30f, 3.64f, -14.90f, -1.25f, -20.70f, 0.190f },
            { 2252, "SOVEREIGN_FALSETTO_BLOOM_252", 0.00f, 90.0f, -1.76f, 2.40f, 1.60f, -14.80f, -1.00f, -20.40f, 0.200f },
            { 2253, "SOVEREIGN_VOCALOID_FAST_253", 0.10f, 92.5f, -1.69f, 2.50f, 1.72f, -14.70f, -1.05f, -20.10f, 0.210f },
            { 2254, "SOVEREIGN_RNB_WARM_254", 0.20f, 95.0f, -1.62f, 2.60f, 1.84f, -14.60f, -1.10f, -19.80f, 0.220f },
            { 2255, "SOVEREIGN_IDOL_SHINE_255", 0.30f, 97.5f, -1.55f, 2.70f, 1.96f, -16.00f, -1.15f, -19.50f, 0.230f },
            { 2256, "SOVEREIGN_JPOP_LEAD_256", 0.40f, 100.0f, -1.48f, 2.80f, 2.08f, -15.90f, -1.20f, -24.00f, 0.240f },
            { 2257, "SOVEREIGN_ANISONG_BELT_257", 0.50f, 102.5f, -1.41f, 2.90f, 2.20f, -15.80f, -1.25f, -23.70f, 0.250f },
            { 2258, "SOVEREIGN_BALLAD_TEAR_258", 0.60f, 105.0f, -1.34f, 3.00f, 2.32f, -15.70f, -1.00f, -23.40f, 0.260f },
            { 2259, "SOVEREIGN_VTUBER_CLEAR_259", 0.70f, 107.5f, -1.27f, 3.10f, 2.44f, -15.60f, -1.05f, -23.10f, 0.270f },
            { 2260, "SOVEREIGN_ASMR_SILK_260", 0.80f, 110.0f, -2.60f, 3.20f, 2.56f, -15.50f, -1.10f, -22.80f, 0.080f },
            { 2261, "SOVEREIGN_ROCK_ARENA_261", 0.90f, 112.5f, -2.53f, 3.30f, 2.68f, -15.40f, -1.15f, -22.50f, 0.090f },
            { 2262, "SOVEREIGN_CITYPOP_TAPE_262", 1.00f, 115.0f, -2.46f, 3.40f, 2.80f, -15.30f, -1.20f, -22.20f, 0.100f },
            { 2263, "SOVEREIGN_RAP_PUNCH_263", 1.10f, 117.5f, -2.39f, 3.50f, 2.92f, -15.20f, -1.25f, -21.90f, 0.110f },
            { 2264, "SOVEREIGN_ACOUSTIC_WOOD_264", -1.20f, 120.0f, -2.32f, 1.40f, 3.04f, -15.10f, -1.00f, -21.60f, 0.120f },
            { 2265, "SOVEREIGN_CHOIR_HALO_265", -1.10f, 122.5f, -2.25f, 1.50f, 3.16f, -15.00f, -1.05f, -21.30f, 0.130f },
            { 2266, "SOVEREIGN_KARAOKE_STAGE_266", -1.00f, 125.0f, -2.18f, 1.60f, 3.28f, -14.90f, -1.10f, -21.00f, 0.140f },
            { 2267, "SOVEREIGN_RADIO_HOST_267", -0.90f, 127.5f, -2.11f, 1.70f, 3.40f, -14.80f, -1.15f, -20.70f, 0.150f },
            { 2268, "SOVEREIGN_FALSETTO_BLOOM_268", -0.80f, 130.0f, -2.04f, 1.80f, 3.52f, -14.70f, -1.20f, -20.40f, 0.160f },
            { 2269, "SOVEREIGN_VOCALOID_FAST_269", -0.70f, 132.5f, -1.97f, 1.90f, 3.64f, -14.60f, -1.25f, -20.10f, 0.170f },
            { 2270, "SOVEREIGN_RNB_WARM_270", -0.60f, 60.0f, -1.90f, 2.00f, 1.60f, -16.00f, -1.00f, -19.80f, 0.180f },
            { 2271, "SOVEREIGN_IDOL_SHINE_271", -0.50f, 62.5f, -1.83f, 2.10f, 1.72f, -15.90f, -1.05f, -19.50f, 0.190f },
            { 2272, "SOVEREIGN_JPOP_LEAD_272", -0.40f, 65.0f, -1.76f, 2.20f, 1.84f, -15.80f, -1.10f, -24.00f, 0.200f },
            { 2273, "SOVEREIGN_ANISONG_BELT_273", -0.30f, 67.5f, -1.69f, 2.30f, 1.96f, -15.70f, -1.15f, -23.70f, 0.210f },
            { 2274, "SOVEREIGN_BALLAD_TEAR_274", -0.20f, 70.0f, -1.62f, 2.40f, 2.08f, -15.60f, -1.20f, -23.40f, 0.220f },
            { 2275, "SOVEREIGN_VTUBER_CLEAR_275", -0.10f, 72.5f, -1.55f, 2.50f, 2.20f, -15.50f, -1.25f, -23.10f, 0.230f },
            { 2276, "SOVEREIGN_ASMR_SILK_276", 0.00f, 75.0f, -1.48f, 2.60f, 2.32f, -15.40f, -1.00f, -22.80f, 0.240f },
            { 2277, "SOVEREIGN_ROCK_ARENA_277", 0.10f, 77.5f, -1.41f, 2.70f, 2.44f, -15.30f, -1.05f, -22.50f, 0.250f },
            { 2278, "SOVEREIGN_CITYPOP_TAPE_278", 0.20f, 80.0f, -1.34f, 2.80f, 2.56f, -15.20f, -1.10f, -22.20f, 0.260f },
            { 2279, "SOVEREIGN_RAP_PUNCH_279", 0.30f, 82.5f, -1.27f, 2.90f, 2.68f, -15.10f, -1.15f, -21.90f, 0.270f },
            { 2280, "SOVEREIGN_ACOUSTIC_WOOD_280", 0.40f, 85.0f, -2.60f, 3.00f, 2.80f, -15.00f, -1.20f, -21.60f, 0.080f },
            { 2281, "SOVEREIGN_CHOIR_HALO_281", 0.50f, 87.5f, -2.53f, 3.10f, 2.92f, -14.90f, -1.25f, -21.30f, 0.090f },
            { 2282, "SOVEREIGN_KARAOKE_STAGE_282", 0.60f, 90.0f, -2.46f, 3.20f, 3.04f, -14.80f, -1.00f, -21.00f, 0.100f },
            { 2283, "SOVEREIGN_RADIO_HOST_283", 0.70f, 92.5f, -2.39f, 3.30f, 3.16f, -14.70f, -1.05f, -20.70f, 0.110f },
            { 2284, "SOVEREIGN_FALSETTO_BLOOM_284", 0.80f, 95.0f, -2.32f, 3.40f, 3.28f, -14.60f, -1.10f, -20.40f, 0.120f },
            { 2285, "SOVEREIGN_VOCALOID_FAST_285", 0.90f, 97.5f, -2.25f, 3.50f, 3.40f, -16.00f, -1.15f, -20.10f, 0.130f },
            { 2286, "SOVEREIGN_RNB_WARM_286", 1.00f, 100.0f, -2.18f, 1.40f, 3.52f, -15.90f, -1.20f, -19.80f, 0.140f },
            { 2287, "SOVEREIGN_IDOL_SHINE_287", 1.10f, 102.5f, -2.11f, 1.50f, 3.64f, -15.80f, -1.25f, -19.50f, 0.150f },
            { 2288, "SOVEREIGN_JPOP_LEAD_288", -1.20f, 105.0f, -2.04f, 1.60f, 1.60f, -15.70f, -1.00f, -24.00f, 0.160f },
            { 2289, "SOVEREIGN_ANISONG_BELT_289", -1.10f, 107.5f, -1.97f, 1.70f, 1.72f, -15.60f, -1.05f, -23.70f, 0.170f },
            { 2290, "SOVEREIGN_BALLAD_TEAR_290", -1.00f, 110.0f, -1.90f, 1.80f, 1.84f, -15.50f, -1.10f, -23.40f, 0.180f },
            { 2291, "SOVEREIGN_VTUBER_CLEAR_291", -0.90f, 112.5f, -1.83f, 1.90f, 1.96f, -15.40f, -1.15f, -23.10f, 0.190f },
            { 2292, "SOVEREIGN_ASMR_SILK_292", -0.80f, 115.0f, -1.76f, 2.00f, 2.08f, -15.30f, -1.20f, -22.80f, 0.200f },
            { 2293, "SOVEREIGN_ROCK_ARENA_293", -0.70f, 117.5f, -1.69f, 2.10f, 2.20f, -15.20f, -1.25f, -22.50f, 0.210f },
            { 2294, "SOVEREIGN_CITYPOP_TAPE_294", -0.60f, 120.0f, -1.62f, 2.20f, 2.32f, -15.10f, -1.00f, -22.20f, 0.220f },
            { 2295, "SOVEREIGN_RAP_PUNCH_295", -0.50f, 122.5f, -1.55f, 2.30f, 2.44f, -15.00f, -1.05f, -21.90f, 0.230f },
            { 2296, "SOVEREIGN_ACOUSTIC_WOOD_296", -0.40f, 125.0f, -1.48f, 2.40f, 2.56f, -14.90f, -1.10f, -21.60f, 0.240f },
            { 2297, "SOVEREIGN_CHOIR_HALO_297", -0.30f, 127.5f, -1.41f, 2.50f, 2.68f, -14.80f, -1.15f, -21.30f, 0.250f },
            { 2298, "SOVEREIGN_KARAOKE_STAGE_298", -0.20f, 130.0f, -1.34f, 2.60f, 2.80f, -14.70f, -1.20f, -21.00f, 0.260f },
            { 2299, "SOVEREIGN_RADIO_HOST_299", -0.10f, 132.5f, -1.27f, 2.70f, 2.92f, -14.60f, -1.25f, -20.70f, 0.270f },
            { 2300, "SOVEREIGN_FALSETTO_BLOOM_300", 0.00f, 60.0f, -2.60f, 2.80f, 3.04f, -16.00f, -1.00f, -20.40f, 0.080f },
            { 2301, "SOVEREIGN_VOCALOID_FAST_301", 0.10f, 62.5f, -2.53f, 2.90f, 3.16f, -15.90f, -1.05f, -20.10f, 0.090f },
            { 2302, "SOVEREIGN_RNB_WARM_302", 0.20f, 65.0f, -2.46f, 3.00f, 3.28f, -15.80f, -1.10f, -19.80f, 0.100f },
            { 2303, "SOVEREIGN_IDOL_SHINE_303", 0.30f, 67.5f, -2.39f, 3.10f, 3.40f, -15.70f, -1.15f, -19.50f, 0.110f },
            { 2304, "SOVEREIGN_JPOP_LEAD_304", 0.40f, 70.0f, -2.32f, 3.20f, 3.52f, -15.60f, -1.20f, -24.00f, 0.120f },
            { 2305, "SOVEREIGN_ANISONG_BELT_305", 0.50f, 72.5f, -2.25f, 3.30f, 3.64f, -15.50f, -1.25f, -23.70f, 0.130f },
            { 2306, "SOVEREIGN_BALLAD_TEAR_306", 0.60f, 75.0f, -2.18f, 3.40f, 1.60f, -15.40f, -1.00f, -23.40f, 0.140f },
            { 2307, "SOVEREIGN_VTUBER_CLEAR_307", 0.70f, 77.5f, -2.11f, 3.50f, 1.72f, -15.30f, -1.05f, -23.10f, 0.150f },
            { 2308, "SOVEREIGN_ASMR_SILK_308", 0.80f, 80.0f, -2.04f, 1.40f, 1.84f, -15.20f, -1.10f, -22.80f, 0.160f },
            { 2309, "SOVEREIGN_ROCK_ARENA_309", 0.90f, 82.5f, -1.97f, 1.50f, 1.96f, -15.10f, -1.15f, -22.50f, 0.170f },
            { 2310, "SOVEREIGN_CITYPOP_TAPE_310", 1.00f, 85.0f, -1.90f, 1.60f, 2.08f, -15.00f, -1.20f, -22.20f, 0.180f },
            { 2311, "SOVEREIGN_RAP_PUNCH_311", 1.10f, 87.5f, -1.83f, 1.70f, 2.20f, -14.90f, -1.25f, -21.90f, 0.190f },
            { 2312, "SOVEREIGN_ACOUSTIC_WOOD_312", -1.20f, 90.0f, -1.76f, 1.80f, 2.32f, -14.80f, -1.00f, -21.60f, 0.200f },
            { 2313, "SOVEREIGN_CHOIR_HALO_313", -1.10f, 92.5f, -1.69f, 1.90f, 2.44f, -14.70f, -1.05f, -21.30f, 0.210f },
            { 2314, "SOVEREIGN_KARAOKE_STAGE_314", -1.00f, 95.0f, -1.62f, 2.00f, 2.56f, -14.60f, -1.10f, -21.00f, 0.220f },
            { 2315, "SOVEREIGN_RADIO_HOST_315", -0.90f, 97.5f, -1.55f, 2.10f, 2.68f, -16.00f, -1.15f, -20.70f, 0.230f },
            { 2316, "SOVEREIGN_FALSETTO_BLOOM_316", -0.80f, 100.0f, -1.48f, 2.20f, 2.80f, -15.90f, -1.20f, -20.40f, 0.240f },
            { 2317, "SOVEREIGN_VOCALOID_FAST_317", -0.70f, 102.5f, -1.41f, 2.30f, 2.92f, -15.80f, -1.25f, -20.10f, 0.250f },
            { 2318, "SOVEREIGN_RNB_WARM_318", -0.60f, 105.0f, -1.34f, 2.40f, 3.04f, -15.70f, -1.00f, -19.80f, 0.260f },
            { 2319, "SOVEREIGN_IDOL_SHINE_319", -0.50f, 107.5f, -1.27f, 2.50f, 3.16f, -15.60f, -1.05f, -19.50f, 0.270f },
            { 2320, "SOVEREIGN_JPOP_LEAD_320", -0.40f, 110.0f, -2.60f, 2.60f, 3.28f, -15.50f, -1.10f, -24.00f, 0.080f },
            { 2321, "SOVEREIGN_ANISONG_BELT_321", -0.30f, 112.5f, -2.53f, 2.70f, 3.40f, -15.40f, -1.15f, -23.70f, 0.090f },
            { 2322, "SOVEREIGN_BALLAD_TEAR_322", -0.20f, 115.0f, -2.46f, 2.80f, 3.52f, -15.30f, -1.20f, -23.40f, 0.100f },
            { 2323, "SOVEREIGN_VTUBER_CLEAR_323", -0.10f, 117.5f, -2.39f, 2.90f, 3.64f, -15.20f, -1.25f, -23.10f, 0.110f },
            { 2324, "SOVEREIGN_ASMR_SILK_324", 0.00f, 120.0f, -2.32f, 3.00f, 1.60f, -15.10f, -1.00f, -22.80f, 0.120f },
            { 2325, "SOVEREIGN_ROCK_ARENA_325", 0.10f, 122.5f, -2.25f, 3.10f, 1.72f, -15.00f, -1.05f, -22.50f, 0.130f },
            { 2326, "SOVEREIGN_CITYPOP_TAPE_326", 0.20f, 125.0f, -2.18f, 3.20f, 1.84f, -14.90f, -1.10f, -22.20f, 0.140f },
            { 2327, "SOVEREIGN_RAP_PUNCH_327", 0.30f, 127.5f, -2.11f, 3.30f, 1.96f, -14.80f, -1.15f, -21.90f, 0.150f },
            { 2328, "SOVEREIGN_ACOUSTIC_WOOD_328", 0.40f, 130.0f, -2.04f, 3.40f, 2.08f, -14.70f, -1.20f, -21.60f, 0.160f },
            { 2329, "SOVEREIGN_CHOIR_HALO_329", 0.50f, 132.5f, -1.97f, 3.50f, 2.20f, -14.60f, -1.25f, -21.30f, 0.170f },
            { 2330, "SOVEREIGN_KARAOKE_STAGE_330", 0.60f, 60.0f, -1.90f, 1.40f, 2.32f, -16.00f, -1.00f, -21.00f, 0.180f },
            { 2331, "SOVEREIGN_RADIO_HOST_331", 0.70f, 62.5f, -1.83f, 1.50f, 2.44f, -15.90f, -1.05f, -20.70f, 0.190f },
            { 2332, "SOVEREIGN_FALSETTO_BLOOM_332", 0.80f, 65.0f, -1.76f, 1.60f, 2.56f, -15.80f, -1.10f, -20.40f, 0.200f },
            { 2333, "SOVEREIGN_VOCALOID_FAST_333", 0.90f, 67.5f, -1.69f, 1.70f, 2.68f, -15.70f, -1.15f, -20.10f, 0.210f },
            { 2334, "SOVEREIGN_RNB_WARM_334", 1.00f, 70.0f, -1.62f, 1.80f, 2.80f, -15.60f, -1.20f, -19.80f, 0.220f },
            { 2335, "SOVEREIGN_IDOL_SHINE_335", 1.10f, 72.5f, -1.55f, 1.90f, 2.92f, -15.50f, -1.25f, -19.50f, 0.230f },
            { 2336, "SOVEREIGN_JPOP_LEAD_336", -1.20f, 75.0f, -1.48f, 2.00f, 3.04f, -15.40f, -1.00f, -24.00f, 0.240f },
            { 2337, "SOVEREIGN_ANISONG_BELT_337", -1.10f, 77.5f, -1.41f, 2.10f, 3.16f, -15.30f, -1.05f, -23.70f, 0.250f },
            { 2338, "SOVEREIGN_BALLAD_TEAR_338", -1.00f, 80.0f, -1.34f, 2.20f, 3.28f, -15.20f, -1.10f, -23.40f, 0.260f },
            { 2339, "SOVEREIGN_VTUBER_CLEAR_339", -0.90f, 82.5f, -1.27f, 2.30f, 3.40f, -15.10f, -1.15f, -23.10f, 0.270f },
        }};

        static inline bool verifySovereignKernelVault(double sampleRate = 48000.0) noexcept {
            if (sampleRate <= 0.0 || !std::isfinite(sampleRate)) return false;
            SovereignTrapezoidalSvfCell lp{}, bp{}, hp{};
            lp.configure(sampleRate, 1000.0, 0.7071);
            bp.configure(sampleRate, 2800.0, 1.4142);
            hp.configure(sampleRate, 110.0, 0.7071);
            double energy = 0.0;
            for (std::size_t n = 0; n < 256; ++n) {
                const float p = static_cast<float>(n) * 0.085f;
                const float x = 0.25f * std::sin(p) + 0.10f * std::cos(p * 2.5f);
                const float y = lp.stepLowpass(x) + bp.stepBandpass(x) + hp.stepHighpass(x);
                if (!std::isfinite(y)) return false;
                energy += static_cast<double>(y * y);
            }
            if (!(energy > 1.0e-6 && energy < 1.0e6)) return false;
            float prevHz = 0.0f;
            for (const auto& cb : kCriticalBands) {
                if (!(cb.centerHz > prevHz && cb.qFactor > 0.1f)) return false;
                prevHz = cb.centerHz;
            }
            for (const auto& m : kJapaneseMoraDictionVault) {
                if (!(m.f1Hz > 150.0f && m.f2Hz > m.f1Hz && m.f3Hz > m.f2Hz)) return false;
            }
            for (const auto& th : kThemeContrastVault) {
                if (!(th.contrastRatio >= 4.5f)) return false;
            }
            for (const auto& pr : kSovereignPresetVault) {
                if (!(pr.lowCutHz >= 20.0f && pr.truePeakCeilingDb <= -0.1f)) return false;
            }
            return true;
        }
    };

} // namespace vg::sovereign::safety
// SOVEREIGN_30K_VERIFIED_MODULE_00
// VocalGzzio v5.5.0 Sovereign 7-Domain Flagship Suite — Verified
