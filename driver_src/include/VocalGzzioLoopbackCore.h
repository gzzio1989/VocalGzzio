// ============================================================================
// VocalGzzioLoopbackCore.h
// 100%自社実装 Windows 仮想オーディオドライバ (VocalGzzioAudio.sys) 用
// 浮動小数点命令ゼロ（FPU/SSE/NEON不使用・カーネル DISPATCH_LEVEL 完全安全）の
// ステレオ Q31 ループバック・リングバッファ＆整数線形リサンプラ・フォーマット変換エンジン
// ============================================================================
#pragma once

#if defined(_KERNEL_MODE)
using int8_t   = signed char;
using uint8_t  = unsigned char;
using int16_t  = short;
using uint16_t = unsigned short;
using int32_t  = int;
using uint32_t = unsigned int;
using int64_t  = long long;
using uint64_t = unsigned long long;
#else
#include <stdint.h>
#include <stddef.h>
#endif

static_assert (sizeof (int16_t) == 2 && sizeof (int32_t) == 4 && sizeof (int64_t) == 8, "Fixed-width integer size mismatch");

namespace vgaudio
{

// 対応サンプルフォーマット種別
enum class SampleFormat : uint32_t
{
    Pcm16       = 0, // 16-bit Signed Integer PCM (2 bytes / sample)
    Pcm24Packed = 1, // 24-bit Signed Integer Packed PCM (3 bytes / sample)
    Pcm24In32   = 2, // 24-bit Signed Integer in 32-bit Container (4 bytes / sample, MSB aligned or 24-bit valid)
    Pcm32       = 3, // 32-bit Signed Integer PCM (4 bytes / sample)
    Float32     = 4  // 32-bit IEEE-754 Float [-1.0, +1.0] (4 bytes / sample)
};

// ストリーム形式情報
struct StreamFormatInfo
{
    uint32_t     sampleRate;     // 44100 / 48000 / 88200 / 96000
    uint16_t     channels;       // 1 または 2
    uint16_t     bytesPerSample; // 2, 3, 4
    uint16_t     blockAlign;     // channels * bytesPerSample
    SampleFormat format;
};

// ----------------------------------------------------------------------------
// FPU命令を一切使わない IEEE-754 binary32 <-> Signed Q31 固定小数点変換
// x64 / ARM64 カーネルの DISPATCH_LEVEL (DPC) でもレジスタ退避不要で完全安全
// ----------------------------------------------------------------------------
inline int32_t ieee754BitsToQ31 (uint32_t bits) noexcept
{
    const uint32_t sign = (bits >> 31) & 1u;
    const int32_t  exp  = static_cast<int32_t> ((bits >> 23) & 0xFFu);
    const uint32_t mant = bits & 0x7FFFFFu;

    // NaN は無音(0)へ安全に落とし、+/-Inf はフルスケールへクランプ
    if (exp == 0xFF)
    {
        if (mant != 0u)
            return 0;
        return sign ? -2147483647 : 2147483647;
    }

    // ゼロまたは非正規化数は無音(0)
    if (exp == 0)
        return 0;

    // |x| >= 1.0f は Q31 最大値へハードクリップ
    if (exp >= 127)
        return sign ? -2147483647 : 2147483647;

    const uint32_t sig = 0x800000u | mant; // 24-bit 仮数 (1.mant * 2^23)
    uint32_t mag = 0u;

    if (exp >= 119)
    {
        // exp in [119..126] -> 左シフト [0..7]、最大 0xFFFFFF << 7 = 0x7FFFFF80 <= INT32_MAX
        mag = sig << static_cast<uint32_t> (exp - 119);
    }
    else
    {
        const int32_t rshift = 119 - exp;
        if (rshift >= 25)
            return 0;
        mag = (sig + (1u << static_cast<uint32_t> (rshift - 1))) >> static_cast<uint32_t> (rshift);
    }

    if (mag > 2147483647u)
        mag = 2147483647u;

    return sign ? -static_cast<int32_t> (mag) : static_cast<int32_t> (mag);
}

inline uint32_t highestBitIndex32 (uint32_t v) noexcept
{
    uint32_t r = 0u;
    if (v & 0xFFFF0000u) { v >>= 16; r += 16u; }
    if (v & 0x0000FF00u) { v >>= 8;  r += 8u;  }
    if (v & 0x000000F0u) { v >>= 4;  r += 4u;  }
    if (v & 0x0000000Cu) { v >>= 2;  r += 2u;  }
    if (v & 0x00000002u) {           r += 1u;  }
    return r;
}

inline uint32_t q31ToIeee754Bits (int32_t q31) noexcept
{
    if (q31 == 0)
        return 0u;

    const uint32_t sign = (q31 < 0) ? 0x80000000u : 0u;
    uint32_t mag = 0u;
    if (q31 == (-2147483647 - 1))
        return sign | 0x3F800000u; // -1.0f
    mag = (q31 < 0) ? static_cast<uint32_t> (-q31) : static_cast<uint32_t> (q31);

    if (mag >= 0x80000000u)
        return sign | 0x3F800000u;

    const uint32_t msb = highestBitIndex32 (mag); // 0..30
    uint32_t exp  = msb + 96u;                    // msb=30 (0.5) -> exp=126
    uint32_t mant = 0u;

    if (msb > 23u)
    {
        const uint32_t rshift  = msb - 23u; // 1..7
        const uint32_t rounded = mag + (1u << (rshift - 1u));
        if (rounded >= (1u << (msb + 1u)))
        {
            exp += 1u;
            mant = 0u;
        }
        else
        {
            mant = (rounded >> rshift) & 0x7FFFFFu;
        }
    }
    else
    {
        const uint32_t lshift = 23u - msb;
        mant = (mag << lshift) & 0x7FFFFFu;
    }

    return sign | (exp << 23) | mant;
}

// ----------------------------------------------------------------------------
// 1サンプルのバイト列 <-> Q31 変換（非アライン境界・循環バッファ折り返し安全）
// ----------------------------------------------------------------------------
inline int32_t decodeSampleToQ31 (const uint8_t* bytes, SampleFormat fmt) noexcept
{
    switch (fmt)
    {
        case SampleFormat::Pcm16:
        {
            const uint16_t u = static_cast<uint16_t> (bytes[0])
                             | (static_cast<uint16_t> (bytes[1]) << 8);
            const int16_t s = static_cast<int16_t> (u);
            return static_cast<int32_t> (s) << 16;
        }
        case SampleFormat::Pcm24Packed:
        {
            uint32_t u = (static_cast<uint32_t> (bytes[0]) << 8)
                       | (static_cast<uint32_t> (bytes[1]) << 16)
                       | (static_cast<uint32_t> (bytes[2]) << 24);
            return static_cast<int32_t> (u);
        }
        case SampleFormat::Pcm24In32:
        case SampleFormat::Pcm32:
        {
            const uint32_t u = static_cast<uint32_t> (bytes[0])
                             | (static_cast<uint32_t> (bytes[1]) << 8)
                             | (static_cast<uint32_t> (bytes[2]) << 16)
                             | (static_cast<uint32_t> (bytes[3]) << 24);
            return static_cast<int32_t> (u);
        }
        case SampleFormat::Float32:
        default:
        {
            const uint32_t u = static_cast<uint32_t> (bytes[0])
                             | (static_cast<uint32_t> (bytes[1]) << 8)
                             | (static_cast<uint32_t> (bytes[2]) << 16)
                             | (static_cast<uint32_t> (bytes[3]) << 24);
            return ieee754BitsToQ31 (u);
        }
    }
}

inline void encodeQ31ToSample (int32_t q31, uint8_t* bytes, SampleFormat fmt) noexcept
{
    switch (fmt)
    {
        case SampleFormat::Pcm16:
        {
            // 丸め付き Q31 -> 16-bit signed クランプ
            int32_t rounded = (q31 >> 15);
            rounded = (rounded + 1) >> 1;
            if (rounded > 32767)  rounded = 32767;
            if (rounded < -32768) rounded = -32768;
            const uint16_t u = static_cast<uint16_t> (static_cast<int16_t> (rounded));
            bytes[0] = static_cast<uint8_t> (u & 0xFFu);
            bytes[1] = static_cast<uint8_t> ((u >> 8) & 0xFFu);
            break;
        }
        case SampleFormat::Pcm24Packed:
        {
            int64_t rounded = (static_cast<int64_t> (q31) + 128) >> 8;
            if (rounded > 8388607)  rounded = 8388607;
            if (rounded < -8388608) rounded = -8388608;
            const uint32_t u = static_cast<uint32_t> (static_cast<int32_t> (rounded));
            bytes[0] = static_cast<uint8_t> (u & 0xFFu);
            bytes[1] = static_cast<uint8_t> ((u >> 8) & 0xFFu);
            bytes[2] = static_cast<uint8_t> ((u >> 16) & 0xFFu);
            break;
        }
        case SampleFormat::Pcm24In32:
        {
            // 下位8ビットをマスクして24bit有効MSBアライン格納
            const uint32_t u = static_cast<uint32_t> (q31) & 0xFFFFFF00u;
            bytes[0] = 0u;
            bytes[1] = static_cast<uint8_t> ((u >> 8) & 0xFFu);
            bytes[2] = static_cast<uint8_t> ((u >> 16) & 0xFFu);
            bytes[3] = static_cast<uint8_t> ((u >> 24) & 0xFFu);
            break;
        }
        case SampleFormat::Pcm32:
        {
            const uint32_t u = static_cast<uint32_t> (q31);
            bytes[0] = static_cast<uint8_t> (u & 0xFFu);
            bytes[1] = static_cast<uint8_t> ((u >> 8) & 0xFFu);
            bytes[2] = static_cast<uint8_t> ((u >> 16) & 0xFFu);
            bytes[3] = static_cast<uint8_t> ((u >> 24) & 0xFFu);
            break;
        }
        case SampleFormat::Float32:
        default:
        {
            const uint32_t u = q31ToIeee754Bits (q31);
            bytes[0] = static_cast<uint8_t> (u & 0xFFu);
            bytes[1] = static_cast<uint8_t> ((u >> 8) & 0xFFu);
            bytes[2] = static_cast<uint8_t> ((u >> 16) & 0xFFu);
            bytes[3] = static_cast<uint8_t> ((u >> 24) & 0xFFu);
            break;
        }
    }
}

// 循環 WaveRT DMA バッファから安全に1フレーム分のバイトを読み書きするヘルパー
inline void readCyclicBytes (const uint8_t* ring, uint32_t ringSize, uint32_t offset, uint8_t* dst, uint32_t count) noexcept
{
    uint32_t pos = offset % ringSize;
    for (uint32_t i = 0; i < count; ++i)
    {
        dst[i] = ring[pos];
        if (++pos >= ringSize)
            pos = 0u;
    }
}

inline void writeCyclicBytes (uint8_t* ring, uint32_t ringSize, uint32_t offset, const uint8_t* src, uint32_t count) noexcept
{
    uint32_t pos = offset % ringSize;
    for (uint32_t i = 0; i < count; ++i)
    {
        ring[pos] = src[i];
        if (++pos >= ringSize)
            pos = 0u;
    }
}

// ============================================================================
// ステレオ Q31 ループバック・リングバッファ本体
// ・Render(再生側: VocalGzzio 外部出力) -> Capture(録音側: OBS / Discord / VRChat)
// ・サンプルレート不一致時 (48kHz <-> 44.1kHz <-> 96kHz) でも 32.32 固定小数点線形補間で変換
// ・Render停止時は残留バッファの「ブーー」という発振ループを防ぎ、64サンプルで滑らかに無音減衰
// ============================================================================
struct LoopbackBus
{
    static constexpr uint32_t kCapacityFrames = 16384u; // 2のべき乗 (~341ms @ 48kHz)
    static constexpr uint32_t kIndexMask      = kCapacityFrames - 1u;

    int32_t  left[kCapacityFrames];
    int32_t  right[kCapacityFrames];

    uint64_t writeFrame;        // Render側が書き込んだ累積フレーム位置
    uint64_t readFrameInt;      // Capture側が読み進めた整数フレーム位置 (Render座標系)
    uint32_t readPhaseFrac;     // 32-bit 小数位相 (0 .. 0xFFFFFFFF)
    uint32_t renderSampleRate;  // 現在アクティブな Render サンプルレート
    bool     renderActive;      // Render ストリームが KSSTATE_RUN 中か
    int32_t  tailLeft;          // アンダーラン時のクリック防止フェードアウト用保持値
    int32_t  tailRight;

    void reset() noexcept
    {
        for (uint32_t i = 0; i < kCapacityFrames; ++i)
        {
            left[i]  = 0;
            right[i] = 0;
        }
        writeFrame       = 0ull;
        readFrameInt     = 0ull;
        readPhaseFrac    = 0u;
        renderSampleRate = 48000u;
        renderActive     = false;
        tailLeft         = 0;
        tailRight        = 0;
    }

    void setRenderStreamState (bool active, uint32_t sampleRate) noexcept
    {
        if (sampleRate >= 8000u && sampleRate <= 192000u)
            renderSampleRate = sampleRate;
        renderActive = active;
    }

    // Render の WaveRT 循環バッファから指定フレーム数をデコードしてリングへ積む
    void pushFromCyclicRenderBuffer (
        const uint8_t*          cyclicBuf,
        uint32_t                cyclicBytes,
        uint32_t                byteOffset,
        uint32_t                framesToPush,
        const StreamFormatInfo& fmt) noexcept
    {
        if (cyclicBuf == nullptr || cyclicBytes == 0u || fmt.blockAlign == 0u || framesToPush == 0u)
            return;

        if (fmt.sampleRate >= 8000u && fmt.sampleRate <= 192000u)
            renderSampleRate = fmt.sampleRate;
        renderActive = true;

        uint8_t frameBytes[8] = { 0 };
        const uint32_t copyBytes = (fmt.blockAlign <= sizeof (frameBytes)) ? fmt.blockAlign : sizeof (frameBytes);
        uint32_t curOffset = byteOffset % cyclicBytes;

        for (uint32_t f = 0; f < framesToPush; ++f)
        {
            readCyclicBytes (cyclicBuf, cyclicBytes, curOffset, frameBytes, copyBytes);
            curOffset += fmt.blockAlign;
            if (curOffset >= cyclicBytes)
                curOffset %= cyclicBytes;

            const int32_t l = decodeSampleToQ31 (frameBytes, fmt.format);
            const int32_t r = (fmt.channels >= 2u)
                            ? decodeSampleToQ31 (frameBytes + fmt.bytesPerSample, fmt.format)
                            : l;

            const uint32_t dstIdx = static_cast<uint32_t> (writeFrame) & kIndexMask;
            left[dstIdx]  = l;
            right[dstIdx] = r;
            ++writeFrame;
        }
    }

    // リングから指定フレーム数を取り出し（必要に応じリサンプル）、Capture の WaveRT 循環バッファへ書き込む
    void popToCyclicCaptureBuffer (
        uint8_t*                cyclicBuf,
        uint32_t                cyclicBytes,
        uint32_t                byteOffset,
        uint32_t                framesToPop,
        const StreamFormatInfo& capFmt) noexcept
    {
        if (cyclicBuf == nullptr || cyclicBytes == 0u || capFmt.blockAlign == 0u || framesToPop == 0u)
            return;

        const uint32_t dstRate = (capFmt.sampleRate >= 8000u && capFmt.sampleRate <= 192000u) ? capFmt.sampleRate : 48000u;
        const uint32_t srcRate = (renderSampleRate  >= 8000u && renderSampleRate  <= 192000u) ? renderSampleRate  : 48000u;

        // 32.32 固定小数点ステップ: 同一レートなら厳密に 1.0 (1ULL << 32)
        const uint64_t step32 = (srcRate == dstRate)
                              ? (1ull << 32)
                              : ((static_cast<uint64_t> (srcRate) << 32) / static_cast<uint64_t> (dstRate));

        // 必要な入力フレーム見積もり + 線形補間用の1フレーム余裕
        const uint64_t neededSrcFrames = ((static_cast<uint64_t> (framesToPop) * step32) >> 32) + 2ull;

        // 目標遅延 (~10ms) と最大許容遅延 (~45ms): ドリフト蓄積を自動補正
        const uint64_t targetLatencyFrames = static_cast<uint64_t> (srcRate / 100u);      // 10 ms (480 frames @ 48kHz)
        const uint64_t maxLatencyFrames    = static_cast<uint64_t> ((srcRate * 45u) / 1000u); // 45 ms

        if (writeFrame > readFrameInt)
        {
            const uint64_t avail = writeFrame - readFrameInt;
            if (avail > maxLatencyFrames && writeFrame > targetLatencyFrames + neededSrcFrames)
            {
                readFrameInt  = writeFrame - (targetLatencyFrames + neededSrcFrames);
                readPhaseFrac = 0u;
            }
        }
        else if (readFrameInt > writeFrame)
        {
            readFrameInt  = writeFrame;
            readPhaseFrac = 0u;
        }

        uint8_t frameBytes[8] = { 0 };
        uint32_t curOffset = byteOffset % cyclicBytes;

        for (uint32_t f = 0; f < framesToPop; ++f)
        {
            int32_t outL = 0;
            int32_t outR = 0;

            const uint64_t avail = (writeFrame > readFrameInt) ? (writeFrame - readFrameInt) : 0ull;
            const uint64_t minRequired = (srcRate == dstRate) ? 1ull : 2ull;

            if (renderActive && avail >= minRequired)
            {
                const uint32_t idx0 = static_cast<uint32_t> (readFrameInt) & kIndexMask;
                if (srcRate == dstRate && readPhaseFrac == 0u)
                {
                    outL = left[idx0];
                    outR = right[idx0];
                }
                else
                {
                    const uint32_t idx1 = (idx0 + 1u) & kIndexMask;
                    const int64_t  frac = static_cast<int64_t> (readPhaseFrac >> 16); // 0..65535 (Q16)
                    const int64_t  l0   = left[idx0];
                    const int64_t  l1   = left[idx1];
                    const int64_t  r0   = right[idx0];
                    const int64_t  r1   = right[idx1];
                    outL = static_cast<int32_t> (l0 + (((l1 - l0) * frac) >> 16));
                    outR = static_cast<int32_t> (r0 + (((r1 - r0) * frac) >> 16));
                }

                tailLeft  = outL;
                tailRight = outR;

                const uint64_t nextPhase = static_cast<uint64_t> (readPhaseFrac) + step32;
                readFrameInt  += (nextPhase >> 32);
                readPhaseFrac  = static_cast<uint32_t> (nextPhase & 0xFFFFFFFFull);
            }
            else
            {
                // Render停止・アンダーラン時: クリック音を出さず約2〜3msで滑らかにゼロへ減衰
                if (tailLeft != 0 || tailRight != 0)
                {
                    tailLeft  -= (tailLeft >> 4);
                    tailRight -= (tailRight >> 4);
                    if (tailLeft > -64 && tailLeft < 64)   tailLeft  = 0;
                    if (tailRight > -64 && tailRight < 64) tailRight = 0;
                }
                outL = tailLeft;
                outR = tailRight;
            }

            if (capFmt.channels >= 2u)
            {
                encodeQ31ToSample (outL, frameBytes, capFmt.format);
                encodeQ31ToSample (outR, frameBytes + capFmt.bytesPerSample, capFmt.format);
            }
            else
            {
                const int32_t mono = static_cast<int32_t> ((static_cast<int64_t> (outL) + static_cast<int64_t> (outR)) >> 1);
                encodeQ31ToSample (mono, frameBytes, capFmt.format);
            }

            writeCyclicBytes (cyclicBuf, cyclicBytes, curOffset, frameBytes, capFmt.blockAlign);
            curOffset += capFmt.blockAlign;
            if (curOffset >= cyclicBytes)
                curOffset %= cyclicBytes;
        }
    }
};

} // namespace vgaudio
