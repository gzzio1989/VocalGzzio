#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
patch_juce_wasapi.py
JUCE 8.0.12 の WASAPIInputDevice / WASAPIAudioIODevice における
長時間稼働（約4時間前後）のクロックドリフト蓄積によるリングバッファ枯渇・満杯ウェッジ（チリチリ音）を解消するパッチ。
"""
import pathlib
import sys

OLD_BLOCK_1 = """    bool start (int userBufferSizeIn)
    {
        const auto reservoirSize = nextPowerOfTwo ((int) (actualBufferSize + (UINT32) userBufferSizeIn));

        queue = SingleThreadedAbstractFifo (reservoirSize);
        reservoir.setSize ((size_t) (queue.getSize() * bytesPerFrame), true);
        xruns = 0;

        if (! check (client->Start()))
            return false;

        purgeInputBuffers();
        delegate.deviceSessionBecameActive (device);

        return true;
    }

    void purgeInputBuffers()
    {
        uint8* inputData;
        UINT32 numSamplesAvailable;
        DWORD flags;

        while (captureClient->GetBuffer (&inputData, &numSamplesAvailable, &flags, nullptr, nullptr) != MAKE_HRESULT (0, 0x889, 0x1) /* AUDCLNT_S_BUFFER_EMPTY */)
            captureClient->ReleaseBuffer (numSamplesAvailable);
    }

    int getNumSamplesInReservoir() const noexcept    { return queue.getNumReadable(); }

    void handleDeviceBuffer()
    {
        if (numChannels <= 0)
            return;

        uint8* inputData = nullptr;
        UINT32 numSamplesAvailable = 0;
        DWORD flags = 0;

        while (check (captureClient->GetBuffer (&inputData, &numSamplesAvailable, &flags, nullptr, nullptr)) && numSamplesAvailable > 0)
        {
            if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0)
                xruns++;

            if (numSamplesAvailable > (UINT32) queue.getRemainingSpace())
            {
                captureClient->ReleaseBuffer (0);
                return;
            }

            auto offset = 0;

            for (const auto& block : queue.write ((int) numSamplesAvailable))
            {
                const auto samplesToDoBytes = block.getLength() * bytesPerFrame;

                auto* reservoirPtr = addBytesToPointer (reservoir.getData(), block.getStart() * bytesPerFrame);

                if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0)
                    zeromem (reservoirPtr, (size_t) samplesToDoBytes);
                else
                    memcpy (reservoirPtr, inputData + offset * bytesPerFrame, (size_t) samplesToDoBytes);

                offset += block.getLength();
            }

            captureClient->ReleaseBuffer (numSamplesAvailable);
        }
    }

    void copyBuffersFromReservoir (float* const* destBuffers, const int numDestBuffers, const int bufferSize)
    {
        if ((numChannels <= 0 && bufferSize == 0) || reservoir.isEmpty())
            return;

        auto offset = jmax (0, bufferSize - queue.getNumReadable());

        if (offset > 0)
            for (int i = 0; i < numDestBuffers; ++i)
                zeromem (destBuffers[i], (size_t) offset * sizeof (float));

        for (const auto& block : queue.read (jmin (queue.getNumReadable(), bufferSize)))
        {
            for (auto i = 0; i < numDestBuffers; ++i)
                converter->convertSamples (destBuffers[i] + offset,
                                           0,
                                           addBytesToPointer (reservoir.getData(), block.getStart() * bytesPerFrame),
                                           channelMaps.getUnchecked (i),
                                           block.getLength());

            offset += block.getLength();
        }
    }

    ComSmartPtr<IAudioCaptureClient> captureClient;
    MemoryBlock reservoir;
    SingleThreadedAbstractFifo queue;
    int xruns = 0;"""

NEW_BLOCK_1 = """    bool start (int userBufferSizeIn)
    {
        lastUserBufferSize = jmax (64, userBufferSizeIn);
        const auto reservoirSize = nextPowerOfTwo (jmax (16384, (int) (actualBufferSize * 8 + (UINT32) lastUserBufferSize * 8)));

        queue = SingleThreadedAbstractFifo (reservoirSize);
        reservoir.setSize ((size_t) (queue.getSize() * bytesPerFrame), true);
        lastInputSample.clearQuick();
        lastInputSample.insertMultiple (0, 0.0f, jmax (8, actualNumChannels + 2));
        xruns = 0;

        if (! check (client->Start()))
            return false;

        purgeInputBuffers();
        delegate.deviceSessionBecameActive (device);

        return true;
    }

    void purgeInputBuffers()
    {
        uint8* inputData;
        UINT32 numSamplesAvailable;
        DWORD flags;

        while (captureClient->GetBuffer (&inputData, &numSamplesAvailable, &flags, nullptr, nullptr) != MAKE_HRESULT (0, 0x889, 0x1) /* AUDCLNT_S_BUFFER_EMPTY */)
            captureClient->ReleaseBuffer (numSamplesAvailable);
    }

    int getNumSamplesInReservoir() const noexcept    { return queue.getNumReadable(); }

    void handleDeviceBuffer()
    {
        if (numChannels <= 0)
            return;

        uint8* inputData = nullptr;
        UINT32 numSamplesAvailable = 0;
        DWORD flags = 0;

        while (check (captureClient->GetBuffer (&inputData, &numSamplesAvailable, &flags, nullptr, nullptr)) && numSamplesAvailable > 0)
        {
            if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0)
                xruns++;

            if (numSamplesAvailable > (UINT32) queue.getRemainingSpace())
            {
                const int targetKeep = jmax (lastUserBufferSize, (int) actualBufferSize) * 2;
                const int toDrop = jmax ((int) numSamplesAvailable - queue.getRemainingSpace(),
                                         queue.getNumReadable() - targetKeep);
                if (toDrop > 0)
                    queue.read (jmin (toDrop, queue.getNumReadable()));
                xruns++;
            }

            auto offset = 0;

            for (const auto& block : queue.write ((int) numSamplesAvailable))
            {
                const auto samplesToDoBytes = block.getLength() * bytesPerFrame;

                auto* reservoirPtr = addBytesToPointer (reservoir.getData(), block.getStart() * bytesPerFrame);

                if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0)
                    zeromem (reservoirPtr, (size_t) samplesToDoBytes);
                else
                    memcpy (reservoirPtr, inputData + offset * bytesPerFrame, (size_t) samplesToDoBytes);

                offset += block.getLength();
            }

            captureClient->ReleaseBuffer (numSamplesAvailable);
        }
    }

    void copyBuffersFromReservoir (float* const* destBuffers, const int numDestBuffers, const int bufferSize)
    {
        if ((numChannels <= 0 && bufferSize == 0) || reservoir.isEmpty() || bufferSize <= 0)
            return;

        while (lastInputSample.size() < numDestBuffers)
            lastInputSample.add (0.0f);

        const int readable  = queue.getNumReadable();
        const int lowWater  = bufferSize + (int) actualBufferSize + jmax (256, bufferSize / 2);
        const int highWater = lowWater + jmax (1024, (int) actualBufferSize * 2);
        const int kFade     = jmin (32, bufferSize / 2);

        if (readable >= bufferSize - 1 && readable < lowWater && kFade >= 8)
        {
            const int toRead = bufferSize - 1;
            int offset = 0;
            for (const auto& block : queue.read (toRead))
            {
                for (auto i = 0; i < numDestBuffers; ++i)
                    converter->convertSamples (destBuffers[i] + offset,
                                               0,
                                               addBytesToPointer (reservoir.getData(), block.getStart() * bytesPerFrame),
                                               channelMaps.getUnchecked (i),
                                               block.getLength());
                offset += block.getLength();
            }

            const int startIdx = bufferSize - kFade;
            float tail[32];
            for (auto i = 0; i < numDestBuffers; ++i)
            {
                for (int k = 0; k < kFade - 1; ++k)
                    tail[k] = destBuffers[i][startIdx + k];
                for (int k = 0; k < kFade; ++k)
                {
                    const float pos = (float) k * (float) (kFade - 2) / (float) (kFade - 1);
                    const int idx = jmin (kFade - 3, (int) pos);
                    const float frac = pos - (float) idx;
                    destBuffers[i][startIdx + k] = tail[idx] + frac * (tail[idx + 1] - tail[idx]);
                }
                lastInputSample.setUnchecked (i, destBuffers[i][bufferSize - 1]);
            }
            return;
        }

        if (readable >= bufferSize)
        {
            int offset = 0;
            for (const auto& block : queue.read (bufferSize))
            {
                for (auto i = 0; i < numDestBuffers; ++i)
                    converter->convertSamples (destBuffers[i] + offset,
                                               0,
                                               addBytesToPointer (reservoir.getData(), block.getStart() * bytesPerFrame),
                                               channelMaps.getUnchecked (i),
                                               block.getLength());
                offset += block.getLength();
            }

            if (readable > highWater && queue.getNumReadable() >= 1 && kFade >= 8)
            {
                for (const auto& block : queue.read (1))
                {
                    if (block.getLength() > 0)
                    {
                        const int startIdx = bufferSize - kFade;
                        float tail[33];
                        for (auto i = 0; i < numDestBuffers; ++i)
                        {
                            float extraSample = destBuffers[i][bufferSize - 1];
                            converter->convertSamples (&extraSample,
                                                       0,
                                                       addBytesToPointer (reservoir.getData(), block.getStart() * bytesPerFrame),
                                                       channelMaps.getUnchecked (i),
                                                       1);
                            for (int k = 0; k < kFade; ++k)
                                tail[k] = destBuffers[i][startIdx + k];
                            tail[kFade] = extraSample;
                            for (int k = 0; k < kFade; ++k)
                            {
                                const float pos = (float) k * (float) kFade / (float) (kFade - 1);
                                const int idx = jmin (kFade - 1, (int) pos);
                                const float frac = pos - (float) idx;
                                destBuffers[i][startIdx + k] = tail[idx] + frac * (tail[idx + 1] - tail[idx]);
                            }
                        }
                    }
                }
            }

            for (auto i = 0; i < numDestBuffers; ++i)
                lastInputSample.setUnchecked (i, destBuffers[i][bufferSize - 1]);
            return;
        }

        int offset = 0;
        for (const auto& block : queue.read (jmin (readable, bufferSize)))
        {
            for (auto i = 0; i < numDestBuffers; ++i)
                converter->convertSamples (destBuffers[i] + offset,
                                           0,
                                           addBytesToPointer (reservoir.getData(), block.getStart() * bytesPerFrame),
                                           channelMaps.getUnchecked (i),
                                           block.getLength());

            offset += block.getLength();
        }

        if (offset > 0)
        {
            for (auto i = 0; i < numDestBuffers; ++i)
                lastInputSample.setUnchecked (i, destBuffers[i][offset - 1]);
        }

        for (auto i = 0; i < numDestBuffers; ++i)
        {
            float s = lastInputSample.getUnchecked (i);
            for (int k = offset; k < bufferSize; ++k)
            {
                s *= 0.985f;
                destBuffers[i][k] = s;
            }
            lastInputSample.setUnchecked (i, s);
        }
    }

    ComSmartPtr<IAudioCaptureClient> captureClient;
    MemoryBlock reservoir;
    SingleThreadedAbstractFifo queue;
    Array<float> lastInputSample;
    int lastUserBufferSize = 480;
    int xruns = 0;"""

OLD_BLOCK_2 = """                else
                {
                    if (isExclusiveMode (deviceMode) && WaitForSingleObject (inputDevice->clientEvent, 0) == WAIT_OBJECT_0)
                        inputDevice->handleDeviceBuffer();
                }"""

NEW_BLOCK_2 = """                else
                {
                    if ((! isExclusiveMode (deviceMode)) || WaitForSingleObject (inputDevice->clientEvent, 0) == WAIT_OBJECT_0)
                        inputDevice->handleDeviceBuffer();
                }"""


def patch_file(path: pathlib.Path) -> bool:
    if not path.is_file():
        return False
    raw = path.read_text(encoding="utf-8")
    norm = raw.replace("\r\n", "\n")
    changed = False
    if OLD_BLOCK_1 in norm:
        norm = norm.replace(OLD_BLOCK_1, NEW_BLOCK_1, 1)
        changed = True
    prev_water = (
        "        const int lowWater  = bufferSize + jmax (128, (int) actualBufferSize / 2);\n"
        "        const int highWater = bufferSize + jmax (1024, (int) actualBufferSize * 3);"
    )
    curr_water = (
        "        const int lowWater  = bufferSize + (int) actualBufferSize + jmax (256, bufferSize / 2);\n"
        "        const int highWater = lowWater + jmax (1024, (int) actualBufferSize * 2);"
    )
    if prev_water in norm:
        norm = norm.replace(prev_water, curr_water, 1)
        changed = True
    if OLD_BLOCK_2 in norm:
        norm = norm.replace(OLD_BLOCK_2, NEW_BLOCK_2, 1)
        changed = True
    if changed:
        path.write_text(norm, encoding="utf-8", newline="\r\n")
        print(f"Patched: {path}")
    else:
        print(f"Already patched or unchanged: {path}")
    return True


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    targets = list(sys.argv[1:])
    if not targets:
        for bdir in ("build", "build_prod", "build_trial"):
            p = root / bdir / "_deps" / "juce-src" / "modules" / "juce_audio_devices" / "native" / "juce_WASAPI_windows.cpp"
            if p.exists():
                targets.append(str(p))
    for t in targets:
        patch_file(pathlib.Path(t))


if __name__ == "__main__":
    main()
