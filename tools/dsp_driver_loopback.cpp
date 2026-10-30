// ============================================================================
// dsp_driver_loopback.cpp
// 独自仮想オーディオドライバ (VocalGzzioAudio.sys) のカーネル内部ループバックエンジン
// (driver_src/include/VocalGzzioLoopbackCore.h) の精度・折り返し・SRC・停止減衰回帰検査
// ============================================================================
#include "../driver_src/include/VocalGzzioLoopbackCore.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace
{

uint32_t floatToBits (float f) noexcept
{
    uint32_t u = 0;
    std::memcpy (&u, &f, sizeof (u));
    return u;
}

float bitsToFloat (uint32_t u) noexcept
{
    float f = 0.0f;
    std::memcpy (&f, &u, sizeof (f));
    return f;
}

bool checkFloatQ31RoundTrip()
{
    // 1. ゼロおよび特殊値 (NaN / Inf / 過大入力クリップ)
    if (vgaudio::ieee754BitsToQ31 (floatToBits (0.0f)) != 0)
        return false;
    if (vgaudio::q31ToIeee754Bits (0) != 0u)
        return false;

    const uint32_t nanBits = 0x7FC00001u;
    if (vgaudio::ieee754BitsToQ31 (nanBits) != 0)
        return false;

    if (vgaudio::ieee754BitsToQ31 (floatToBits (1.5f)) != 2147483647)
        return false;
    if (vgaudio::ieee754BitsToQ31 (floatToBits (-1.5f)) != -2147483647)
        return false;

    // 2. 代表振幅 (-80dBFS 〜 0dBFS) の往復誤差が 24bit 仮数分解能以内であること
    const float testValues[] = {
        0.5f, -0.5f, 0.25f, -0.25f, 0.9999f, -0.9999f,
        0.1f, -0.1f, 0.01f, -0.01f, 0.0001f, -0.0001f
    };
    for (float v : testValues)
    {
        const int32_t  q31 = vgaudio::ieee754BitsToQ31 (floatToBits (v));
        const float    rt  = bitsToFloat (vgaudio::q31ToIeee754Bits (q31));
        const float    err = std::fabs (rt - v);
        if (err > 2.0e-7f)
        {
            std::fprintf (stderr, "[FAIL] Float<->Q31 roundtrip: in=%.8f out=%.8f err=%.9f\n", v, rt, err);
            return false;
        }
    }
    return true;
}

bool checkSameRateSineSnrAndCyclicWrap()
{
    auto bus = std::make_unique<vgaudio::LoopbackBus>();
    bus->reset();

    // わざと素数に近いフレーム数 (313 frames = 2504 bytes) の循環バッファを使い、折り返し境界を毎回踏ませる
    constexpr uint32_t kCyclicFrames = 313u;
    constexpr uint32_t kSampleRate   = 48000u;

    const vgaudio::StreamFormatInfo floatFmt { kSampleRate, 2, 4, 8, vgaudio::SampleFormat::Float32 };
    const vgaudio::StreamFormatInfo pcm16Fmt { kSampleRate, 2, 2, 4, vgaudio::SampleFormat::Pcm16   };

    std::vector<uint8_t> renderRing (kCyclicFrames * floatFmt.blockAlign, 0);
    std::vector<uint8_t> capFloatRing (kCyclicFrames * floatFmt.blockAlign, 0);

    uint32_t renderByteOff = 17u * floatFmt.blockAlign; // 非ゼロ開始オフセット
    uint32_t capByteOff    = 89u * floatFmt.blockAlign;

    constexpr uint32_t kChunkFrames = 96u; // 2ms @ 48kHz
    constexpr uint32_t kTotalChunks = 50u; // 4800 frames (100ms)
    const double twoPi = 6.28318530717958647692;
    const double freqL = 997.0;
    const double freqR = 440.0;

    double sigPower = 0.0;
    double errPower = 0.0;
    uint32_t producedFrame = 0;
    uint32_t verifiedFrame = 0;

    for (uint32_t c = 0; c < kTotalChunks; ++c)
    {
        for (uint32_t i = 0; i < kChunkFrames; ++i)
        {
            const uint32_t idx = producedFrame + i;
            const float sL = static_cast<float> (0.7 * std::sin (twoPi * freqL * idx / kSampleRate));
            const float sR = static_cast<float> (0.5 * std::cos (twoPi * freqR * idx / kSampleRate));
            uint8_t raw[8];
            const uint32_t bL = floatToBits (sL);
            const uint32_t bR = floatToBits (sR);
            std::memcpy (raw,     &bL, 4);
            std::memcpy (raw + 4, &bR, 4);
            vgaudio::writeCyclicBytes (
                renderRing.data(),
                static_cast<uint32_t> (renderRing.size()),
                (renderByteOff + i * floatFmt.blockAlign) % static_cast<uint32_t> (renderRing.size()),
                raw,
                8);
        }

        bus->pushFromCyclicRenderBuffer (
            renderRing.data(),
            static_cast<uint32_t> (renderRing.size()),
            renderByteOff,
            kChunkFrames,
            floatFmt);
        renderByteOff = (renderByteOff + kChunkFrames * floatFmt.blockAlign) % static_cast<uint32_t> (renderRing.size());
        producedFrame += kChunkFrames;

        bus->popToCyclicCaptureBuffer (
            capFloatRing.data(),
            static_cast<uint32_t> (capFloatRing.size()),
            capByteOff,
            kChunkFrames,
            floatFmt);

        for (uint32_t i = 0; i < kChunkFrames; ++i)
        {
            uint8_t raw[8];
            vgaudio::readCyclicBytes (
                capFloatRing.data(),
                static_cast<uint32_t> (capFloatRing.size()),
                (capByteOff + i * floatFmt.blockAlign) % static_cast<uint32_t> (capFloatRing.size()),
                raw,
                8);
            uint32_t bL = 0, bR = 0;
            std::memcpy (&bL, raw,     4);
            std::memcpy (&bR, raw + 4, 4);
            const float outL = bitsToFloat (bL);
            const float outR = bitsToFloat (bR);

            const uint32_t idx = verifiedFrame + i;
            const float refL = static_cast<float> (0.7 * std::sin (twoPi * freqL * idx / kSampleRate));
            const float refR = static_cast<float> (0.5 * std::cos (twoPi * freqR * idx / kSampleRate));

            const double dL = static_cast<double> (outL) - static_cast<double> (refL);
            const double dR = static_cast<double> (outR) - static_cast<double> (refR);
            sigPower += static_cast<double> (refL) * refL + static_cast<double> (refR) * refR;
            errPower += dL * dL + dR * dR;
        }
        capByteOff = (capByteOff + kChunkFrames * floatFmt.blockAlign) % static_cast<uint32_t> (capFloatRing.size());
        verifiedFrame += kChunkFrames;
    }

    const double snrDb = 10.0 * std::log10 (sigPower / (errPower + 1.0e-30));
    if (snrDb < 130.0)
    {
        std::fprintf (stderr, "[FAIL] Float32 48kHz->48kHz SNR too low: %.2f dB\n", snrDb);
        return false;
    }

    // PCM16 / PCM24Packed エンコード・デコード単体検査
    uint8_t pcm24[3] = { 0 };
    const int32_t targetQ31 = vgaudio::ieee754BitsToQ31 (floatToBits (-0.375f));
    vgaudio::encodeQ31ToSample (targetQ31, pcm24, vgaudio::SampleFormat::Pcm24Packed);
    const int32_t decoded24 = vgaudio::decodeSampleToQ31 (pcm24, vgaudio::SampleFormat::Pcm24Packed);
    if (std::abs (decoded24 - targetQ31) > 512)
    {
        std::fprintf (stderr, "[FAIL] PCM24Packed mismatch: got=%d want=%d\n", decoded24, targetQ31);
        return false;
    }

    uint8_t pcm16[2] = { 0 };
    vgaudio::encodeQ31ToSample (targetQ31, pcm16, pcm16Fmt.format);
    const int32_t decoded16 = vgaudio::decodeSampleToQ31 (pcm16, pcm16Fmt.format);
    if (std::abs (decoded16 - targetQ31) > 65536)
    {
        std::fprintf (stderr, "[FAIL] PCM16 mismatch: got=%d want=%d\n", decoded16, targetQ31);
        return false;
    }

    return true;
}

bool checkCrossSampleRateAndAntiBuzzStop()
{
    auto bus = std::make_unique<vgaudio::LoopbackBus>();
    bus->reset();

    // Render 48000 Hz Float32 -> Capture 44100 Hz Float32 (異サンプルレート線形補間)
    const vgaudio::StreamFormatInfo render48 { 48000u, 2, 4, 8, vgaudio::SampleFormat::Float32 };
    const vgaudio::StreamFormatInfo cap44    { 44100u, 2, 4, 8, vgaudio::SampleFormat::Float32 };

    constexpr uint32_t kRenderFrames = 4800u; // 100 ms @ 48kHz
    constexpr uint32_t kCapFrames    = 4000u; // ~90.7 ms @ 44.1kHz
    std::vector<uint8_t> renderBuf (kRenderFrames * 8, 0);
    std::vector<uint8_t> capBuf (kCapFrames * 8, 0);

    const double twoPi = 6.28318530717958647692;
    for (uint32_t i = 0; i < kRenderFrames; ++i)
    {
        const float s = static_cast<float> (0.6 * std::sin (twoPi * 440.0 * i / 48000.0));
        const uint32_t b = floatToBits (s);
        std::memcpy (&renderBuf[i * 8],     &b, 4);
        std::memcpy (&renderBuf[i * 8 + 4], &b, 4);
    }

    bus->pushFromCyclicRenderBuffer (renderBuf.data(), static_cast<uint32_t> (renderBuf.size()), 0, kRenderFrames, render48);
    bus->popToCyclicCaptureBuffer (capBuf.data(), static_cast<uint32_t> (capBuf.size()), 0, kCapFrames, cap44);

    // 44.1kHz 出力された 440Hz サイン波との相関・誤差検証
    double sigPower = 0.0;
    double errPower = 0.0;
    for (uint32_t i = 64; i < kCapFrames - 64; ++i)
    {
        uint32_t bL = 0;
        std::memcpy (&bL, &capBuf[i * 8], 4);
        const double outL = static_cast<double> (bitsToFloat (bL));
        const double refL = 0.6 * std::sin (twoPi * 440.0 * i / 44100.0);
        sigPower += refL * refL;
        errPower += (outL - refL) * (outL - refL);
    }
    const double srcSnrDb = 10.0 * std::log10 (sigPower / (errPower + 1.0e-30));
    if (srcSnrDb < 65.0)
    {
        std::fprintf (stderr, "[FAIL] 48kHz->44.1kHz linear SRC SNR too low: %.2f dB\n", srcSnrDb);
        return false;
    }

    // Render 停止時の「ブーー」残留発振防止＆滑らかなゼロ減衰検査
    bus->setRenderStreamState (false, 48000u);
    std::vector<uint8_t> stopBuf (256 * 8, 0);
    bus->popToCyclicCaptureBuffer (stopBuf.data(), static_cast<uint32_t> (stopBuf.size()), 0, 256, cap44);

    float maxJump = 0.0f;
    uint32_t prevB = 0;
    std::memcpy (&prevB, &capBuf[(kCapFrames - 1) * 8], 4);
    float prevSample = bitsToFloat (prevB);

    for (uint32_t i = 0; i < 256; ++i)
    {
        uint32_t b = 0;
        std::memcpy (&b, &stopBuf[i * 8], 4);
        const float cur = bitsToFloat (b);
        const float jump = std::fabs (cur - prevSample);
        if (jump > maxJump)
            maxJump = jump;
        prevSample = cur;
    }

    // 128サンプル以降で完全に 0.0f へ収束しており、かつ急激なクリック段差 (< 0.1f) が無いこと
    if (std::fabs (prevSample) != 0.0f || maxJump > 0.10f)
    {
        std::fprintf (stderr, "[FAIL] Anti-buzz stop tail failed: final=%.8f maxJump=%.6f\n", prevSample, maxJump);
        return false;
    }

    return true;
}

} // namespace

int main()
{
    if (! checkFloatQ31RoundTrip())
        return 1;
    if (! checkSameRateSineSnrAndCyclicWrap())
        return 1;
    if (! checkCrossSampleRateAndAntiBuzzStop())
        return 1;

    std::printf ("OK: dsp_driver_loopback (Float32<->Q31, cyclic wrap, 48k<->44.1k SRC, anti-buzz fade)\n");
    return 0;
}
