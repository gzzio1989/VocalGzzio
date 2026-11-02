#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX 1
 #endif
 #include <windows.h>
 #include <mfapi.h>
 #include <mfidl.h>
 #include <mfreadwrite.h>
 #include <mferror.h>
 #include <codecapi.h>
 #include <wrl/client.h>
 #pragma comment(lib, "mfplat.lib")
 #pragma comment(lib, "mfreadwrite.lib")
 #pragma comment(lib, "mfuuid.lib")
 #ifdef small
  #undef small
 #endif
 #ifdef near
  #undef near
 #endif
#endif

namespace gz
{

//==============================================================================
/** v5.5.0: SNS投稿用ビジュアライザ動画（.mp4 / H.264 + AAC）1クリック書き出し器。
    Windows 標準搭載の Media Foundation (IMFSinkWriter) を使用するため、
    外部 FFmpeg 等の追加インストール不要で X(Twitter) / YouTube / Shorts / TikTok 向けの
    16:9 横型 (1280x720) および 9:16 縦型 (720x1280) 動画を単体生成する。 */
class SnsVideoExporter
{
public:
    struct Options
    {
        int width  = 1280;
        int height = 720;
        int fps    = 30;
        double maxDurationSeconds = 120.0;

        bool compareBeforeAfter = false; // true: 前半50% Dry(素の声) ➔ 後半50% Wet(-14 LUFS完パケ)
        bool showWatermark      = true;  // 右下の「VocalGzzio」透かしロゴ表示（任意ON/OFF）
        bool english            = false;

        juce::String titleText        { "VocalGzzio Take" };
        juce::String subTitleText     { "-14 LUFS Auto-Mastered" };
        juce::String metaBadgeText    {};

        float orbX = 0.50f;
        float orbY = 0.50f;

        // テーマ連動カラー（未指定時は銀河ネイビー既定色）
        juce::Colour bgTop   { 0xff0c1224 };
        juce::Colour bgBot   { 0xff1a2546 };
        juce::Colour panel   { 0xff141d36 };
        juce::Colour ink     { 0xfff3f6ff };
        juce::Colour inkSoft { 0xffa4b4d8 };
        juce::Colour accent  { 0xff5fd4ff };
        juce::Colour salmon  { 0xffff79a8 };
        juce::Colour green   { 0xff56e3a3 };
        juce::Colour ice     { 0xffb2e9ff };
    };

    static constexpr int kNumBars = 36;

    //==============================================================================
    /** 録音テイク（Dry+BGM および -14 LUFS マスタリング済み Wet+BGM）から MP4 動画を書き出す。
        ワーカースレッドからの呼び出しを想定（オーディオ処理スレッドからは呼ばないこと）。 */
    static bool exportMp4 (const juce::File& outFile,
                           const juce::AudioBuffer<float>& dryMix,
                           const juce::AudioBuffer<float>& wetMasteredMix,
                           double srcSampleRate,
                           const Options& rawOpt,
                           std::atomic<float>* progressOut = nullptr,
                           std::atomic<bool>* cancelFlag = nullptr,
                           juce::String* errOut = nullptr)
    {
        if (progressOut != nullptr)
            progressOut->store (0.0f, std::memory_order_relaxed);

        if (wetMasteredMix.getNumSamples() <= 64 || srcSampleRate <= 1000.0)
        {
            if (errOut != nullptr)
                *errOut = rawOpt.english ? "No recorded audio take available to export."
                                         : juce::String::fromUTF8 ("書き出し可能な録音データがありません（先にWAV録音または30秒さかのぼり録音を行ってください）。");
            return false;
        }

       #if ! JUCE_WINDOWS
        juce::ignoreUnused (outFile, dryMix, cancelFlag);
        if (errOut != nullptr)
            *errOut = rawOpt.english ? "MP4 hardware encoding is currently supported on Windows 10/11."
                                     : juce::String::fromUTF8 ("MP4動画書き出しは現在 Windows 10/11 環境に対応しています。");
        return false;
       #else
        Options opt = rawOpt;
        // H.264 エンコーダの仕様に合わせて偶数解像度へ正規化
        opt.width  = juce::jlimit (320, 1920, (opt.width  & ~1));
        opt.height = juce::jlimit (320, 1920, (opt.height & ~1));
        opt.fps    = juce::jlimit (15, 60, opt.fps);

        // Windows AAC エンコーダ MFT は 44100 Hz または 48000 Hz ステレオが標準要件
        const int targetSr = (std::abs (srcSampleRate - 44100.0) < 1.0) ? 44100 : 48000;

        juce::AudioBuffer<float> wet48, dry48;
        resampleToStereoRate (wetMasteredMix, srcSampleRate, wet48, targetSr, opt.maxDurationSeconds);
        const bool hasDry = (opt.compareBeforeAfter && dryMix.getNumSamples() > 64);
        if (hasDry)
            resampleToStereoRate (dryMix, srcSampleRate, dry48, targetSr, opt.maxDurationSeconds);

        const int totalSamples = wet48.getNumSamples();
        if (totalSamples < targetSr / 10)
        {
            if (errOut != nullptr)
                *errOut = opt.english ? "Recorded clip is too short (< 0.1s)."
                                      : juce::String::fromUTF8 ("録音データが短すぎます（0.1秒以上必要です）。");
            return false;
        }

        // 最終音声トラックを構築（Before/After 比較時は中央50%で 30ms 等電力クロスフェード）
        juce::AudioBuffer<float> finalAudio (2, totalSamples);
        const int switchSample = hasDry ? (totalSamples / 2) : 0;
        buildFinalAudioTrack (finalAudio, dry48, wet48, hasDry, switchSample, targetSr);

        const double durationSec = (double) totalSamples / (double) targetSr;
        const int numFrames = juce::jmax (1, (int) std::ceil (durationSec * (double) opt.fps));

        // 各フレームの視覚特徴量（RMS・高域エアー・36本スペクトルバー）を事前解析
        std::vector<FrameFeatures> features;
        analyzeVisualFeatures (finalAudio, targetSr, opt.fps, numFrames, features);

        if (auto parent = outFile.getParentDirectory(); ! parent.isDirectory())
            parent.createDirectory();
        if (outFile.existsAsFile())
            outFile.deleteFile();

        // COM & Media Foundation 初期化ガード
        struct ComMfScope
        {
            HRESULT comHr = E_FAIL;
            bool mfStarted = false;
            ComMfScope()
            {
                comHr = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
                const HRESULT mfHr = MFStartup (MF_VERSION, MFSTARTUP_LITE);
                mfStarted = SUCCEEDED (mfHr);
            }
            ~ComMfScope()
            {
                if (mfStarted)
                    MFShutdown();
                if (comHr == S_OK || comHr == S_FALSE)
                    CoUninitialize();
            }
        } mfScope;

        if (! mfScope.mfStarted)
        {
            if (errOut != nullptr)
                *errOut = opt.english ? "Failed to initialize Windows Media Foundation."
                                      : juce::String::fromUTF8 ("Windows Media Foundation の初期化に失敗しました。");
            return false;
        }

        using Microsoft::WRL::ComPtr;
        ComPtr<IMFSinkWriter> writer;
        DWORD videoStreamIdx = 0;
        DWORD audioStreamIdx = 0;

        auto createConfiguredWriter = [&] (bool allowHwTransforms) -> HRESULT
        {
            writer.Reset();
            ComPtr<IMFAttributes> attrs;
            HRESULT hr = MFCreateAttributes (&attrs, 2);
            if (SUCCEEDED (hr))
            {
                attrs->SetUINT32 (MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, allowHwTransforms ? TRUE : FALSE);
                attrs->SetGUID   (MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
            }

            hr = MFCreateSinkWriterFromURL (outFile.getFullPathName().toWideCharPointer(),
                                            nullptr, attrs.Get(), &writer);
            if (FAILED (hr) || writer == nullptr)
                return FAILED (hr) ? hr : E_FAIL;

            // --- 1. H.264 映像ストリーム設定 ---
            ComPtr<IMFMediaType> vidOut;
            hr = MFCreateMediaType (&vidOut);
            if (FAILED (hr)) return hr;
            vidOut->SetGUID   (MF_MT_MAJOR_TYPE, MFMediaType_Video);
            vidOut->SetGUID   (MF_MT_SUBTYPE, MFVideoFormat_H264);
            vidOut->SetUINT32 (MF_MT_AVG_BITRATE, 4800000u);
            vidOut->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            MFSetAttributeSize  (vidOut.Get(), MF_MT_FRAME_SIZE, (UINT32) opt.width, (UINT32) opt.height);
            MFSetAttributeRatio (vidOut.Get(), MF_MT_FRAME_RATE, (UINT32) opt.fps, 1u);
            MFSetAttributeRatio (vidOut.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1u, 1u);

            hr = writer->AddStream (vidOut.Get(), &videoStreamIdx);
            if (FAILED (hr)) return hr;

            ComPtr<IMFMediaType> vidIn;
            hr = MFCreateMediaType (&vidIn);
            if (FAILED (hr)) return hr;
            vidIn->SetGUID   (MF_MT_MAJOR_TYPE, MFMediaType_Video);
            vidIn->SetGUID   (MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            vidIn->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            vidIn->SetUINT32 (MF_MT_DEFAULT_STRIDE, (UINT32) (opt.width * 4));
            MFSetAttributeSize  (vidIn.Get(), MF_MT_FRAME_SIZE, (UINT32) opt.width, (UINT32) opt.height);
            MFSetAttributeRatio (vidIn.Get(), MF_MT_FRAME_RATE, (UINT32) opt.fps, 1u);
            MFSetAttributeRatio (vidIn.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1u, 1u);

            hr = writer->SetInputMediaType (videoStreamIdx, vidIn.Get(), nullptr);
            if (FAILED (hr)) return hr;

            // --- 2. AAC 音声ストリーム設定 (192 kbps ステレオ) ---
            ComPtr<IMFMediaType> audOut;
            hr = MFCreateMediaType (&audOut);
            if (FAILED (hr)) return hr;
            audOut->SetGUID   (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            audOut->SetGUID   (MF_MT_SUBTYPE, MFAudioFormat_AAC);
            audOut->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, 2u);
            audOut->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32) targetSr);
            audOut->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16u);
            audOut->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000u); // 192 kbps

            hr = writer->AddStream (audOut.Get(), &audioStreamIdx);
            if (FAILED (hr)) return hr;

            ComPtr<IMFMediaType> audIn;
            hr = MFCreateMediaType (&audIn);
            if (FAILED (hr)) return hr;
            audIn->SetGUID   (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            audIn->SetGUID   (MF_MT_SUBTYPE, MFAudioFormat_PCM);
            audIn->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, 2u);
            audIn->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32) targetSr);
            audIn->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16u);
            audIn->SetUINT32 (MF_MT_AUDIO_BLOCK_ALIGNMENT, 4u);
            audIn->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32) (targetSr * 4));
            audIn->SetUINT32 (MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);

            hr = writer->SetInputMediaType (audioStreamIdx, audIn.Get(), nullptr);
            if (FAILED (hr)) return hr;

            return writer->BeginWriting();
        };

        // ハードウェアエンコーダ優先で試行し、ドライバ非対応環境ではソフトウェア MFT へ自動フォールバック
        HRESULT initHr = createConfiguredWriter (true);
        if (FAILED (initHr))
        {
            if (outFile.existsAsFile())
                outFile.deleteFile();
            initHr = createConfiguredWriter (false);
        }

        if (FAILED (initHr) || writer == nullptr)
        {
            if (errOut != nullptr)
                *errOut = (opt.english ? "Failed to open MP4 H.264/AAC encoder (HRESULT 0x"
                                       : juce::String::fromUTF8 ("MP4 H.264/AAC エンコーダの起動に失敗しました (0x"))
                          + juce::String::toHexString ((int) initHr) + ").";
            return false;
        }

        juce::Image canvas (juce::Image::ARGB, opt.width, opt.height, false);
        const DWORD frameBytes = (DWORD) (opt.width * opt.height * 4);
        const LONGLONG frameDurHns = (LONGLONG) std::llround (10000000.0 / (double) opt.fps);

        const float* pcmL = finalAudio.getReadPointer (0);
        const float* pcmR = finalAudio.getReadPointer (1);

        for (int f = 0; f < numFrames; ++f)
        {
            if (cancelFlag != nullptr && cancelFlag->load (std::memory_order_relaxed))
            {
                writer.Reset();
                outFile.deleteFile();
                if (errOut != nullptr)
                    *errOut = opt.english ? "Video export cancelled."
                                          : juce::String::fromUTF8 ("動画の書き出しを中断しました。");
                return false;
            }

            const double tSec = (double) f / (double) opt.fps;
            const int sampleStart = juce::jlimit (0, totalSamples, (int) std::llround (tSec * (double) targetSr));
            const int sampleEnd   = (f + 1 == numFrames)
                                        ? totalSamples
                                        : juce::jlimit (sampleStart, totalSamples,
                                                        (int) std::llround (((double) (f + 1) / (double) opt.fps) * (double) targetSr));

            const bool isAfterPhase = (! hasDry) || (sampleStart >= switchSample);
            const double switchSec  = hasDry ? ((double) switchSample / (double) targetSr) : -10.0;
            const float switchFlash = (hasDry && tSec >= switchSec && tSec < switchSec + 0.55)
                                          ? (float) (1.0 - (tSec - switchSec) / 0.55)
                                          : 0.0f;

            renderVisualizerFrame (canvas, opt, features[(size_t) f],
                                   tSec, durationSec, hasDry, isAfterPhase, switchFlash);

            // 映像フレーム送信（Windows H.264 MFT の標準ボトムアップ RGB32 行順で転送）
            {
                ComPtr<IMFMediaBuffer> vBuf;
                HRESULT hr = MFCreateMemoryBuffer (frameBytes, &vBuf);
                if (FAILED (hr)) break;

                BYTE* dstPtr = nullptr;
                hr = vBuf->Lock (&dstPtr, nullptr, nullptr);
                if (FAILED (hr)) break;

                {
                    juce::Image::BitmapData bmp (canvas, juce::Image::BitmapData::readOnly);
                    const size_t rowBytes = (size_t) opt.width * 4u;
                    for (int y = 0; y < opt.height; ++y)
                    {
                        const uint8_t* srcRow = bmp.getLinePointer (opt.height - 1 - y);
                        std::memcpy (dstPtr + (size_t) y * rowBytes, srcRow, rowBytes);
                    }
                }

                vBuf->Unlock();
                vBuf->SetCurrentLength (frameBytes);

                ComPtr<IMFSample> vSample;
                hr = MFCreateSample (&vSample);
                if (FAILED (hr)) break;
                vSample->AddBuffer (vBuf.Get());
                const LONGLONG vPts = (LONGLONG) std::llround (tSec * 10000000.0);
                vSample->SetSampleTime (vPts);
                vSample->SetSampleDuration (frameDurHns);

                hr = writer->WriteSample (videoStreamIdx, vSample.Get());
                if (FAILED (hr))
                {
                    if (errOut != nullptr)
                        *errOut = juce::String::fromUTF8 ("映像フレームの書き込みに失敗しました (0x")
                                  + juce::String::toHexString ((int) hr) + ").";
                    return false;
                }
            }

            // 対応する音声スライス送信（16-bit インターリーブ PCM）
            const int chunkFrames = sampleEnd - sampleStart;
            if (chunkFrames > 0)
            {
                const DWORD audioBytes = (DWORD) (chunkFrames * 4);
                ComPtr<IMFMediaBuffer> aBuf;
                HRESULT hr = MFCreateMemoryBuffer (audioBytes, &aBuf);
                if (FAILED (hr)) break;

                BYTE* aDst = nullptr;
                hr = aBuf->Lock (&aDst, nullptr, nullptr);
                if (FAILED (hr)) break;

                int16_t* s16 = reinterpret_cast<int16_t*> (aDst);
                for (int i = 0; i < chunkFrames; ++i)
                {
                    const int idx = sampleStart + i;
                    const float l = juce::jlimit (-1.0f, 1.0f, pcmL[idx]);
                    const float r = juce::jlimit (-1.0f, 1.0f, pcmR[idx]);
                    s16[i * 2]     = (int16_t) juce::roundToInt (l * 32767.0f);
                    s16[i * 2 + 1] = (int16_t) juce::roundToInt (r * 32767.0f);
                }

                aBuf->Unlock();
                aBuf->SetCurrentLength (audioBytes);

                ComPtr<IMFSample> aSample;
                hr = MFCreateSample (&aSample);
                if (FAILED (hr)) break;
                aSample->AddBuffer (aBuf.Get());
                const LONGLONG aPts = (LONGLONG) std::llround (((double) sampleStart / (double) targetSr) * 10000000.0);
                const LONGLONG aDur = (LONGLONG) std::llround (((double) chunkFrames / (double) targetSr) * 10000000.0);
                aSample->SetSampleTime (aPts);
                aSample->SetSampleDuration (aDur);

                hr = writer->WriteSample (audioStreamIdx, aSample.Get());
                if (FAILED (hr))
                {
                    if (errOut != nullptr)
                        *errOut = juce::String::fromUTF8 ("音声ストリームの書き込みに失敗しました (0x")
                                  + juce::String::toHexString ((int) hr) + ").";
                    return false;
                }
            }

            if (progressOut != nullptr)
                progressOut->store ((float) (f + 1) / (float) numFrames, std::memory_order_relaxed);
        }

        const HRESULT finHr = writer->Finalize();
        writer.Reset();

        if (FAILED (finHr) || ! outFile.existsAsFile() || outFile.getSize() < 256)
        {
            if (errOut != nullptr)
                *errOut = (opt.english ? "Failed to finalize MP4 file (0x"
                                       : juce::String::fromUTF8 ("MP4ファイルの完了処理に失敗しました (0x"))
                          + juce::String::toHexString ((int) finHr) + ").";
            return false;
        }

        if (progressOut != nullptr)
            progressOut->store (1.0f, std::memory_order_relaxed);
        return true;
       #endif
    }

private:
    struct FrameFeatures
    {
        float rms  = 0.0f;
        float air  = 0.0f;
        float bars[kNumBars] {};
    };

    //==============================================================================
    static void resampleToStereoRate (const juce::AudioBuffer<float>& src,
                                      double srcRate,
                                      juce::AudioBuffer<float>& dst,
                                      int dstRate,
                                      double maxSeconds)
    {
        const int srcCh = src.getNumChannels();
        const int maxSrcSamples = (int) std::min<int64_t> ((int64_t) src.getNumSamples(),
                                                           (int64_t) std::llround (maxSeconds * srcRate));
        if (maxSrcSamples <= 0 || srcCh <= 0)
        {
            dst.setSize (2, 0);
            return;
        }

        const float* inL = src.getReadPointer (0);
        const float* inR = src.getReadPointer (srcCh > 1 ? 1 : 0);

        if (std::abs (srcRate - (double) dstRate) < 1.0)
        {
            dst.setSize (2, maxSrcSamples, false, false, false);
            dst.copyFrom (0, 0, inL, maxSrcSamples);
            dst.copyFrom (1, 0, inR, maxSrcSamples);
            return;
        }

        const double speedRatio = srcRate / (double) dstRate;
        const int outSamples = juce::jmax (1, (int) std::floor ((double) maxSrcSamples / speedRatio));
        dst.setSize (2, outSamples, false, true, false);

        juce::LagrangeInterpolator resL, resR;
        resL.reset();
        resR.reset();
        resL.process (speedRatio, inL, dst.getWritePointer (0), outSamples);
        resR.process (speedRatio, inR, dst.getWritePointer (1), outSamples);
    }

    static void buildFinalAudioTrack (juce::AudioBuffer<float>& out,
                                      const juce::AudioBuffer<float>& dry48,
                                      const juce::AudioBuffer<float>& wet48,
                                      bool compareMode,
                                      int switchSample,
                                      int sampleRate)
    {
        const int n = out.getNumSamples();
        out.copyFrom (0, 0, wet48, 0, 0, n);
        out.copyFrom (1, 0, wet48, 1, 0, n);

        if (! compareMode || dry48.getNumSamples() <= 0)
            return;

        const int dryLen = juce::jmin (n, dry48.getNumSamples());
        const int xfadeSamples = juce::jlimit (64, juce::jmax (65, sampleRate / 10),
                                               (int) std::lround (0.030 * (double) sampleRate));
        const int xfadeStart = juce::jmax (0, switchSample - xfadeSamples / 2);
        const int xfadeEnd   = juce::jmin (n, xfadeStart + xfadeSamples);

        float* outL = out.getWritePointer (0);
        float* outR = out.getWritePointer (1);
        const float* dL = dry48.getReadPointer (0);
        const float* dR = dry48.getReadPointer (1);
        const float* wL = wet48.getReadPointer (0);
        const float* wR = wet48.getReadPointer (1);

        // 前半は素の Dry(+BGM) を出力し、急激なクリップだけ -0.2 dBFS で安全ガード
        for (int i = 0; i < xfadeStart && i < dryLen; ++i)
        {
            outL[i] = juce::jlimit (-0.98f, 0.98f, dL[i]);
            outR[i] = juce::jlimit (-0.98f, 0.98f, dR[i]);
        }

        const int span = juce::jmax (1, xfadeEnd - xfadeStart);
        for (int i = xfadeStart; i < xfadeEnd; ++i)
        {
            const float phase = (float) (i - xfadeStart) / (float) span;
            const float gDry = std::cos (phase * juce::MathConstants<float>::halfPi);
            const float gWet = std::sin (phase * juce::MathConstants<float>::halfPi);
            const float dl = (i < dryLen) ? juce::jlimit (-0.98f, 0.98f, dL[i]) : 0.0f;
            const float dr = (i < dryLen) ? juce::jlimit (-0.98f, 0.98f, dR[i]) : 0.0f;
            outL[i] = juce::jlimit (-0.99f, 0.99f, dl * gDry + wL[i] * gWet);
            outR[i] = juce::jlimit (-0.99f, 0.99f, dr * gDry + wR[i] * gWet);
        }
    }

    static void analyzeVisualFeatures (const juce::AudioBuffer<float>& audio,
                                       int sampleRate,
                                       int fps,
                                       int numFrames,
                                       std::vector<FrameFeatures>& outFeatures)
    {
        outFeatures.assign ((size_t) numFrames, FrameFeatures {});
        const int totalSamples = audio.getNumSamples();
        const float* inL = audio.getReadPointer (0);
        const float* inR = audio.getReadPointer (1);

        // 90 Hz 〜 9,200 Hz の対数等間隔 36 バンド Goertzel 係数
        double coeff[kNumBars] {};
        for (int b = 0; b < kNumBars; ++b)
        {
            const double norm = (double) b / (double) (kNumBars - 1);
            const double freq = 90.0 * std::pow (9200.0 / 90.0, norm);
            const double omega = juce::MathConstants<double>::twoPi * freq / (double) sampleRate;
            coeff[b] = 2.0 * std::cos (omega);
        }

        float smoothRms = 0.0f;
        float smoothAir = 0.0f;
        float smoothBars[kNumBars] {};

        for (int f = 0; f < numFrames; ++f)
        {
            const int s0 = juce::jlimit (0, totalSamples, (int) std::llround (((double) f / (double) fps) * (double) sampleRate));
            const int s1 = juce::jlimit (s0, totalSamples, (int) std::llround (((double) (f + 1) / (double) fps) * (double) sampleRate));
            const int winLen = s1 - s0;

            float sumSq = 0.0f;
            float diffSq = 0.0f;
            float prevMid = 0.0f;

            if (winLen > 0)
            {
                for (int i = s0; i < s1; ++i)
                {
                    const float mid = 0.5f * (inL[i] + inR[i]);
                    sumSq += mid * mid;
                    const float d = mid - prevMid;
                    diffSq += d * d;
                    prevMid = mid;
                }
            }

            const float rawRms = (winLen > 0) ? std::sqrt (sumSq / (float) winLen) : 0.0f;
            const float rawAir = (winLen > 0) ? std::sqrt (diffSq / (float) winLen) : 0.0f;

            const float normRms = juce::jlimit (0.0f, 1.0f, std::pow (rawRms * 3.1f, 0.65f));
            const float normAir = juce::jlimit (0.0f, 1.0f, std::pow (rawAir * 5.2f, 0.70f));

            smoothRms = (normRms > smoothRms) ? (0.58f * normRms + 0.42f * smoothRms)
                                              : (0.24f * normRms + 0.76f * smoothRms);
            smoothAir = (normAir > smoothAir) ? (0.55f * normAir + 0.45f * smoothAir)
                                              : (0.26f * normAir + 0.74f * smoothAir);

            outFeatures[(size_t) f].rms = smoothRms;
            outFeatures[(size_t) f].air = smoothAir;

            const int goertzelN = juce::jmin (winLen, 1024);
            for (int b = 0; b < kNumBars; ++b)
            {
                double q1 = 0.0, q2 = 0.0;
                const double c = coeff[b];
                for (int i = 0; i < goertzelN; ++i)
                {
                    const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) juce::jmax (1, goertzelN - 1));
                    const float mid = 0.5f * (inL[s0 + i] + inR[s0 + i]) * w;
                    const double q0 = c * q1 - q2 + (double) mid;
                    q2 = q1;
                    q1 = q0;
                }
                const double magSq = std::max (0.0, q1 * q1 + q2 * q2 - c * q1 * q2);
                const float mag = (goertzelN > 0) ? (float) (std::sqrt (magSq) / (double) goertzelN) : 0.0f;
                // 高域ほど視覚的に見栄えするよう緩やかなチルト補正（+4.5dB/oct相当）
                const float tilt = 1.0f + 2.4f * ((float) b / (float) (kNumBars - 1));
                const float level = juce::jlimit (0.03f, 1.0f, std::pow (mag * 13.5f * tilt, 0.58f));
                smoothBars[b] = (level > smoothBars[b]) ? (0.62f * level + 0.38f * smoothBars[b])
                                                        : (0.25f * level + 0.75f * smoothBars[b]);
                outFeatures[(size_t) f].bars[b] = smoothBars[b];
            }
        }
    }

    static juce::String formatTimeMmSs (double sec)
    {
        const int total = juce::jmax (0, (int) std::floor (sec));
        const int mm = total / 60;
        const int ss = total % 60;
        return juce::String (mm) + ":" + (ss < 10 ? "0" : "") + juce::String (ss);
    }

    //==============================================================================
    static void renderVisualizerFrame (juce::Image& img,
                                       const Options& opt,
                                       const FrameFeatures& feat,
                                       double tSec,
                                       double totalSec,
                                       bool compareMode,
                                       bool isAfterPhase,
                                       float switchFlash)
    {
        juce::Graphics g (img);
        const float w = (float) opt.width;
        const float h = (float) opt.height;
        const bool portrait = (opt.height > opt.width);
        const float minDim = juce::jmin (w, h);

        // 1. 背景グラデーション + 声量連動ラジアルオーラ
        {
            juce::ColourGradient bgGrad (opt.bgTop, 0.0f, 0.0f,
                                         opt.bgBot, w * 0.25f, h, false);
            g.setGradientFill (bgGrad);
            g.fillRect (0.0f, 0.0f, w, h);

            const juce::Colour auraCol = (compareMode && ! isAfterPhase)
                                             ? opt.inkSoft.withAlpha (0.14f + 0.14f * feat.rms)
                                             : opt.accent.interpolatedWith (opt.salmon, opt.orbX)
                                                         .withAlpha (0.18f + 0.24f * feat.rms);
            const float cx = w * 0.5f;
            const float cy = portrait ? (h * 0.44f) : (h * 0.47f);
            juce::ColourGradient rad (auraCol, cx, cy,
                                      juce::Colours::transparentBlack, cx + minDim * 0.56f, cy, true);
            g.setGradientFill (rad);
            g.fillEllipse (cx - minDim * 0.58f, cy - minDim * 0.58f, minDim * 1.16f, minDim * 1.16f);
        }

        // 2. 漂う星屑パーティクル（決定論的位相なのでフレーム落ちゼロ）
        for (int p = 0; p < 22; ++p)
        {
            const float seedX = std::fmod ((float) (p * 73 + 19), 100.0f) / 100.0f;
            const float seedY = std::fmod ((float) (p * 41 + 7), 100.0f) / 100.0f;
            const float drift = (float) std::fmod (seedY - tSec * (0.022 + (p % 5) * 0.006) + 10.0, 1.0);
            const float px = w * (0.06f + 0.88f * seedX) + std::sin ((float) tSec * 1.3f + (float) p) * 8.0f;
            const float py = h * drift;
            const float pr = (1.4f + (float) (p % 3) * 1.1f) * (0.85f + 0.4f * feat.air);
            const float alpha = 0.14f + 0.22f * (0.5f + 0.5f * std::sin ((float) tSec * 2.4f + (float) p * 1.7f));
            g.setColour (opt.ice.withAlpha (alpha));
            g.fillEllipse (px - pr, py - pr, pr * 2.0f, pr * 2.0f);
        }

        // 3. 上部タイトルカード＆Before/After比較バッジ
        const float pad = minDim * 0.048f;
        const float headerH = portrait ? (h * 0.135f) : (h * 0.165f);
        const juce::Rectangle<float> headerRect (pad, pad * 0.85f, w - pad * 2.0f, headerH);
        g.setColour (opt.panel.withAlpha (0.82f));
        g.fillRoundedRectangle (headerRect, 16.0f);
        g.setColour ((isAfterPhase ? opt.accent : opt.inkSoft).withAlpha (0.38f));
        g.drawRoundedRectangle (headerRect, 16.0f, 1.5f);

        auto headerInner = headerRect.reduced (pad * 0.55f, headerH * 0.16f);
        if (compareMode)
        {
            const float badgeH = juce::jmin (36.0f, headerInner.getHeight() * 0.42f);
            auto badgeRow = headerInner.removeFromTop (badgeH);
            const juce::Colour badgeBg = isAfterPhase
                                             ? opt.accent.interpolatedWith (opt.salmon, 0.38f)
                                             : opt.inkSoft.withAlpha (0.35f);
            const juce::String phaseLabel = isAfterPhase
                ? (opt.english ? "AFTER : VocalGzzio Studio Polish (-14 LUFS)"
                               : juce::String::fromUTF8 ("✨ AFTER : VocalGzzio スタジオ艶加工 (-14 LUFS)"))
                : (opt.english ? "BEFORE : Raw Microphone (Dry)"
                               : juce::String::fromUTF8 ("🎙 BEFORE : 加工前・素の声 (Dry)"));

            const float pillW = juce::jmin (badgeRow.getWidth(), portrait ? (w * 0.80f) : (w * 0.58f));
            const juce::Rectangle<float> pill (badgeRow.getX(), badgeRow.getY(), pillW, badgeH);
            g.setColour (badgeBg.withAlpha (isAfterPhase ? 0.92f : 0.72f));
            g.fillRoundedRectangle (pill, badgeH * 0.5f);
            g.setColour (isAfterPhase ? juce::Colour (0xff091020) : opt.ink);
            g.setFont (juce::FontOptions (badgeH * 0.54f, juce::Font::bold));
            g.drawText (phaseLabel, pill.reduced (14.0f, 0.0f), juce::Justification::centredLeft, true);
            headerInner.removeFromTop (4.0f);
        }

        g.setColour (opt.ink);
        g.setFont (juce::FontOptions (juce::jlimit (18.0f, 34.0f, headerInner.getHeight() * 0.52f), juce::Font::bold));
        g.drawText (opt.titleText, headerInner.removeFromTop (headerInner.getHeight() * 0.58f),
                    juce::Justification::centredLeft, true);

        juce::String subLine = opt.subTitleText;
        if (opt.metaBadgeText.isNotEmpty())
            subLine += "   |   " + opt.metaBadgeText;
        g.setColour (opt.inkSoft);
        g.setFont (juce::FontOptions (juce::jlimit (13.0f, 21.0f, headerInner.getHeight() * 0.78f)));
        g.drawText (subLine, headerInner, juce::Justification::centredLeft, true);

        // 4. 中央 2D ボイスオーブ（声量・倍音エアー・声質座標に連動）
        const float orbCx = w * 0.5f;
        const float orbCy = portrait ? (h * 0.44f) : (h * 0.48f);
        const float baseR = minDim * (portrait ? 0.165f : 0.155f);
        const float pulseR = baseR * (1.0f + 0.24f * feat.rms + 0.06f * std::sin ((float) tSec * 3.2f));
        const juce::Colour coreCol = (compareMode && ! isAfterPhase)
                                         ? opt.inkSoft.interpolatedWith (opt.ice, 0.35f)
                                         : opt.accent.interpolatedWith (opt.salmon, juce::jlimit (0.0f, 1.0f, opt.orbX));

        // 外周の波紋リング 3 重
        for (int ring = 3; ring >= 1; --ring)
        {
            const float rr = pulseR * (1.0f + (float) ring * (0.18f + 0.07f * feat.air));
            g.setColour (coreCol.withAlpha ((0.22f / (float) ring) * (0.6f + 0.7f * feat.rms)));
            g.drawEllipse (orbCx - rr, orbCy - rr, rr * 2.0f, rr * 2.0f, 2.0f);
        }

        // オーブ本体球体グラデーション
        {
            juce::ColourGradient sphereGrad (opt.ice.interpolatedWith (juce::Colours::white, 0.45f),
                                             orbCx - pulseR * (0.32f - 0.4f * (opt.orbX - 0.5f)),
                                             orbCy - pulseR * (0.32f + 0.4f * (opt.orbY - 0.5f)),
                                             coreCol.darker (0.35f),
                                             orbCx + pulseR * 0.85f,
                                             orbCy + pulseR * 0.85f,
                                             true);
            g.setGradientFill (sphereGrad);
            g.fillEllipse (orbCx - pulseR, orbCy - pulseR, pulseR * 2.0f, pulseR * 2.0f);

            g.setColour (juce::Colours::white.withAlpha (0.55f + 0.35f * feat.air));
            g.drawEllipse (orbCx - pulseR, orbCy - pulseR, pulseR * 2.0f, pulseR * 2.0f, 2.4f);
        }

        // Before ➔ After 切替瞬間のフラッシュリング演出
        if (switchFlash > 0.01f)
        {
            const float flashR = pulseR * (1.15f + (1.0f - switchFlash) * 1.35f);
            g.setColour (opt.green.interpolatedWith (juce::Colours::white, 0.5f).withAlpha (switchFlash * 0.75f));
            g.drawEllipse (orbCx - flashR, orbCy - flashR, flashR * 2.0f, flashR * 2.0f, 4.5f * switchFlash);
        }

        // 5. 左右対称 36×2 スペクトルリボン
        const float ribbonTop = portrait ? (h * 0.64f) : (h * 0.69f);
        const float ribbonH   = portrait ? (h * 0.19f) : (h * 0.17f);
        const float ribbonW   = w - pad * 2.4f;
        const float ribbonMidY = ribbonTop + ribbonH * 0.5f;
        const int totalCols = kNumBars * 2;
        const float colStep = ribbonW / (float) totalCols;
        const float barW    = juce::jmax (2.5f, colStep * 0.66f);

        for (int c = 0; c < totalCols; ++c)
        {
            const int bandIdx = (c < kNumBars) ? (kNumBars - 1 - c) : (c - kNumBars);
            const float val = feat.bars[bandIdx];
            const float bh = juce::jmax (4.0f, val * ribbonH * 0.94f);
            const float bx = (w - ribbonW) * 0.5f + (float) c * colStep + (colStep - barW) * 0.5f;
            const float by = ribbonMidY - bh * 0.5f;

            const float ratio = (float) bandIdx / (float) (kNumBars - 1);
            const juce::Colour barCol = (compareMode && ! isAfterPhase)
                                            ? opt.inkSoft.withAlpha (0.62f + 0.25f * val)
                                            : opt.accent.interpolatedWith (opt.salmon, ratio * 0.78f)
                                                        .interpolatedWith (opt.ice, ratio * ratio * 0.35f)
                                                        .withAlpha (0.78f + 0.22f * val);
            g.setColour (barCol);
            g.fillRoundedRectangle (bx, by, barW, bh, barW * 0.48f);
        }

        // 6. 下部シーク進行バー＆経過時刻 + 右下透かしロゴ（任意ON/OFF）
        const float footerY = h - pad * 1.35f;
        const float watermarkReserve = opt.showWatermark ? (portrait ? 168.0f : 196.0f) : 0.0f;
        const float progW = w - pad * 2.0f - watermarkReserve;
        const float progH = 7.0f;
        const juce::Rectangle<float> progTrack (pad, footerY, juce::jmax (80.0f, progW), progH);

        g.setColour (opt.inkSoft.withAlpha (0.24f));
        g.fillRoundedRectangle (progTrack, progH * 0.5f);

        const float progress = (totalSec > 0.001) ? juce::jlimit (0.0f, 1.0f, (float) (tSec / totalSec)) : 0.0f;
        if (progress > 0.002f)
        {
            g.setColour (isAfterPhase ? opt.accent : opt.inkSoft);
            g.fillRoundedRectangle (progTrack.withWidth (progTrack.getWidth() * progress), progH * 0.5f);
        }

        if (compareMode)
        {
            // 中央50%切替マーカー
            const float midX = progTrack.getX() + progTrack.getWidth() * 0.5f;
            g.setColour (opt.green.withAlpha (0.9f));
            g.fillRoundedRectangle (midX - 2.0f, progTrack.getY() - 4.0f, 4.0f, progH + 8.0f, 2.0f);
        }

        g.setColour (opt.inkSoft.withAlpha (0.85f));
        g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        g.drawText (formatTimeMmSs (tSec) + " / " + formatTimeMmSs (totalSec),
                    juce::Rectangle<float> (pad, footerY - 22.0f, 160.0f, 18.0f),
                    juce::Justification::centredLeft, false);

        if (opt.showWatermark)
        {
            const float wmW = portrait ? 152.0f : 176.0f;
            const float wmH = 30.0f;
            const juce::Rectangle<float> wmRect (w - pad - wmW, footerY - wmH * 0.55f, wmW, wmH);
            g.setColour (opt.panel.withAlpha (0.86f));
            g.fillRoundedRectangle (wmRect, wmH * 0.5f);
            g.setColour (opt.accent.withAlpha (0.48f));
            g.drawRoundedRectangle (wmRect, wmH * 0.5f, 1.2f);

            g.setColour (opt.ink);
            g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
            g.drawText (juce::String::fromUTF8 ("✦ VocalGzzio"), wmRect, juce::Justification::centred, false);
        }
    }
};

} // namespace gz
