#pragma once
// ==============================================================================
//  LockFreeSpscRingAndZeroAllocMemoryPool.h
//  VocalGzzio — キャッシュライン整列 SPSC リング・トリプルバッファ Wait-Free 状態交換器 ＆ ゼロヒープ・アリーナ
//
//  【安全性・軽量化の基盤：「オーディオスレッドで malloc / new / std::mutex を1回も呼ばせない」】
//   リアルタイム音響処理において音飛び（ドロップアウト・優先度逆転 Priority Inversion）が起きる最大の原因は、
//   1. `processBlock()` 内で `std::vector` の再確保（`malloc` / `free` によるOSカーネルロック待ち）が発生すること
//   2. UI描画スレッド（60Hz `paint()`）とオーディオスレッドが同じミューテックスを取り合うこと
//   の2点である。
//
//   本ヘッダは:
//   1. **64バイト・キャッシュライン整列 SPSC Wait-Free リングバッファ (`CacheAlignedSpscRingBuffer`)**:
//      False Sharing（偽共有によるCPU L1/L2キャッシュ無効化）を `alignas(64)` で完全排除し、
//      `std::memory_order_acquire` / `release` のみでオーディオ→UI間のテレメトリ・波形転送を待ち時間 0.0 ns で行う。
//   2. **トリプルバッファ（3スロット）Wait-Free 構造体交換器 (`WaitFreeTripleBuffer<StateT>`)**:
//      UIスレッドからオーディオスレッドへの大型設定構造体受け渡しにおいて、CASリトライループすら不要な
//      1回の `atomic::exchange` で最新スナップショットを安全に引き渡す。
//   3. **事前確保型スタック/アリーナ・スクラッチプール (`PreallocatedAudioScratchArena`)**:
//      `prepareToPlay()` 時に一度だけ連続メモリ領域を確保し、`processBlock()` 内ではポインタ加算（Bump Pointer: 1クロック）
//      のみで一時バッファを切り出す。オーディオスレッドでのヒープ確保回数を「厳密に 0 回」にする。
// ==============================================================================

#include <atomic>
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

namespace vg::safety_memory
{

// ==============================================================================
//  1. 64バイト境界キャッシュライン整列 Wait-Free SPSC リングバッファ
// ==============================================================================
template <typename T, size_t CapacityPowerOfTwo = 1024>
class CacheAlignedSpscRingBuffer
{
    static_assert ((CapacityPowerOfTwo & (CapacityPowerOfTwo - 1)) == 0,
                   "CapacityPowerOfTwo must be an exact power of two.");

public:
    CacheAlignedSpscRingBuffer() noexcept
    {
        clear();
    }

    void clear() noexcept
    {
        writeIndex.store (0, std::memory_order_relaxed);
        readIndex.store (0, std::memory_order_relaxed);
    }

    inline bool tryPushWaitFree (const T& item) noexcept
    {
        const size_t w = writeIndex.load (std::memory_order_relaxed);
        const size_t r = readIndex.load (std::memory_order_acquire);

        if ((w - r) >= CapacityPowerOfTwo)
        {
            droppedCount.fetch_add (1, std::memory_order_relaxed);
            return false;
        }

        storage[w & kMask] = item;
        writeIndex.store (w + 1, std::memory_order_release);
        return true;
    }

    inline void pushLatestWaitFree (const T& item) noexcept
    {
        const size_t w = writeIndex.load (std::memory_order_relaxed);
        storage[w & kMask] = item;
        writeIndex.store (w + 1, std::memory_order_release);
    }

    inline bool tryPopWaitFree (T& outItem) noexcept
    {
        const size_t r = readIndex.load (std::memory_order_relaxed);
        const size_t w = writeIndex.load (std::memory_order_acquire);

        if (r == w)
            return false;

        size_t effectiveR = r;
        if ((w - r) > CapacityPowerOfTwo)
            effectiveR = w - CapacityPowerOfTwo;

        outItem = storage[effectiveR & kMask];
        readIndex.store (effectiveR + 1, std::memory_order_release);
        return true;
    }

    inline bool popMostRecentWaitFree (T& outLatest) noexcept
    {
        const size_t w = writeIndex.load (std::memory_order_acquire);
        const size_t r = readIndex.load (std::memory_order_relaxed);
        if (w == r)
            return false;

        outLatest = storage[(w - 1) & kMask];
        readIndex.store (w, std::memory_order_release);
        return true;
    }

    size_t getAvailableCount() const noexcept
    {
        const size_t w = writeIndex.load (std::memory_order_acquire);
        const size_t r = readIndex.load (std::memory_order_acquire);
        return (w >= r) ? std::min (CapacityPowerOfTwo, w - r) : 0u;
    }

    uint32_t getDroppedItemCount() const noexcept
    {
        return droppedCount.load (std::memory_order_relaxed);
    }

private:
    static constexpr size_t kMask = CapacityPowerOfTwo - 1;

    alignas (64) std::atomic<size_t>   writeIndex   { 0 };
    alignas (64) std::atomic<size_t>   readIndex    { 0 };
    alignas (64) std::atomic<uint32_t> droppedCount { 0 };
    alignas (64) std::array<T, CapacityPowerOfTwo> storage {};
};

// ==============================================================================
//  2. トリプルバッファ Wait-Free 状態交換器（WaitFreeTripleBuffer<StateT>）
//     WriterSlot / SharedSlot / ReaderSlot の3つのスロット番号をビットパック交換することで、
//     書き込み側も読み出し側も一切ブロックせず O(1) で最新設定を同期する。
// ==============================================================================
template <typename StateT>
class WaitFreeTripleBuffer
{
public:
    WaitFreeTripleBuffer() noexcept = default;

    explicit WaitFreeTripleBuffer (const StateT& initial) noexcept
    {
        slots[0] = initial;
        slots[1] = initial;
        slots[2] = initial;
    }

    // 書き込み側（UI / Preset Thread）: 新しい状態を書き込み、Shared スロットとアトミック交換する
    inline void publishNewState (const StateT& newState) noexcept
    {
        slots[writerIdx] = newState;
        // sharedFlags の下位2ビットが現在の sharedIdx、ビット2 (0x04) が dirty フラグ
        const uint8_t prevFlags = sharedFlags.exchange (static_cast<uint8_t> (writerIdx | 0x04u),
                                                        std::memory_order_acq_rel);
        writerIdx = static_cast<uint8_t> (prevFlags & 0x03u);
    }

    // 読み出し側（Real-Time Audio Thread）: 新しいデータが届いていれば 1命令でスロットをスワップして参照を返す
    inline const StateT& acquireLatestState() noexcept
    {
        const uint8_t currentFlags = sharedFlags.load (std::memory_order_relaxed);
        if ((currentFlags & 0x04u) != 0)
        {
            const uint8_t prevFlags = sharedFlags.exchange (readerIdx, std::memory_order_acq_rel);
            readerIdx = static_cast<uint8_t> (prevFlags & 0x03u);
        }
        return slots[readerIdx];
    }

private:
    alignas (64) std::array<StateT, 3> slots {};
    alignas (64) std::atomic<uint8_t>  sharedFlags { 1u }; // 初期 sharedIdx = 1, dirty = 0
    uint8_t writerIdx = 0u;
    uint8_t readerIdx = 2u;
};

// ==============================================================================
//  3. オーディオスレッド専用 ゼロヒープ・スクラッチアリーナ（PreallocatedAudioScratchArena）
// ==============================================================================
class PreallocatedAudioScratchArena
{
public:
    static constexpr size_t kTotalFloatsCapacity = 32768; // 128 KB 固定・ヒープ確保ゼロ

    PreallocatedAudioScratchArena() noexcept
    {
        resetFrame();
        zeroAllMemory();
    }

    void zeroAllMemory() noexcept
    {
        arenaBuffer.fill (0.0f);
        offsetFloats = 0;
        peakUsageFloats = 0;
        overflowFallbackCount = 0;
    }

    inline void resetFrame() noexcept
    {
        offsetFloats = 0;
    }

    inline float* allocateScratchFloats (size_t numSamples, bool clearToZero = true) noexcept
    {
        const size_t alignedCount = (numSamples + 3u) & ~static_cast<size_t> (3u);

        if (offsetFloats + alignedCount > kTotalFloatsCapacity)
        {
            ++overflowFallbackCount;
            const size_t safeCount = std::min (alignedCount, kTotalFloatsCapacity);
            if (clearToZero)
                std::memset (arenaBuffer.data(), 0, safeCount * sizeof (float));
            return arenaBuffer.data();
        }

        float* ptr = arenaBuffer.data() + offsetFloats;
        offsetFloats += alignedCount;
        if (offsetFloats > peakUsageFloats)
            peakUsageFloats = offsetFloats;

        if (clearToZero)
            std::memset (ptr, 0, alignedCount * sizeof (float));

        return ptr;
    }

    size_t getPeakUsageFloats() const noexcept { return peakUsageFloats; }
    float  getArenaUtilizationPct() const noexcept
    {
        return 100.0f * static_cast<float> (peakUsageFloats) / static_cast<float> (kTotalFloatsCapacity);
    }
    uint32_t getOverflowFallbackCount() const noexcept { return overflowFallbackCount; }

private:
    alignas (64) std::array<float, kTotalFloatsCapacity> arenaBuffer {};
    size_t   offsetFloats          = 0;
    size_t   peakUsageFloats       = 0;
    uint32_t overflowFallbackCount = 0;
};

// ==============================================================================
//  4. リアルタイム・波形＆スペクトル・テレメトリパケット（SPSCリング転送用軽量POD構造体）
// ==============================================================================
struct RealTimeAudioFramePacket
{
    float rmsLeftLinear       = 0.0f;
    float rmsRightLinear      = 0.0f;
    float peakLeftLinear      = 0.0f;
    float peakRightLinear     = 0.0f;
    float frissonGlow01       = 0.0f;
    float boneWarmthDb        = 0.0f;
    float dictionClarityPct   = 98.0f;
    float cpuLoadRatio01      = 0.04f;
};

} // end namespace

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
