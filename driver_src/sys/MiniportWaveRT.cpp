// ============================================================================
// MiniportWaveRT.cpp
// VocalGzzioAudio.sys: WaveRT ミニポート ＆ イベント駆動対応ストリーム実装
// ・32-bit IEEE Float / 24-bit PCM / 32-bit PCM / 16-bit PCM (44.1〜96kHz, 2ch)
// ・高精度 QPC (KeQueryPerformanceCounter) 剰余累積クロックで長時間配信でもドリフトゼロ
// ・IMiniportWaveRTStreamNotification 対応により OBS / Discord / WASAPI 低遅延動作
// ============================================================================
#include "VocalGzzioAudio.h"

// ----------------------------------------------------------------------------
// 対応フォーマットのデータレンジ定義 (Float32 を先頭に置いて Windows 既定音質を最大化)
// ----------------------------------------------------------------------------
static const KSDATARANGE_AUDIO g_WaveFloatDataRange =
{
    {
        sizeof (KSDATARANGE_AUDIO),
        0,
        0,
        0,
        STATICGUIDOF (KSDATAFORMAT_TYPE_AUDIO),
        STATICGUIDOF (KSDATAFORMAT_SUBTYPE_IEEE_FLOAT),
        STATICGUIDOF (KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
    },
    2,      // MaximumChannels (Stereo)
    32,     // MinimumBitsPerSample
    32,     // MaximumBitsPerSample
    44100,  // MinimumSampleFrequency
    96000   // MaximumSampleFrequency
};

static const KSDATARANGE_AUDIO g_WavePcmDataRange =
{
    {
        sizeof (KSDATARANGE_AUDIO),
        0,
        0,
        0,
        STATICGUIDOF (KSDATAFORMAT_TYPE_AUDIO),
        STATICGUIDOF (KSDATAFORMAT_SUBTYPE_PCM),
        STATICGUIDOF (KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
    },
    2,      // MaximumChannels (Stereo)
    16,     // MinimumBitsPerSample
    32,     // MaximumBitsPerSample
    44100,  // MinimumSampleFrequency
    96000   // MaximumSampleFrequency
};

static const KSDATARANGE g_WaveBridgeDataRange =
{
    sizeof (KSDATARANGE),
    0,
    0,
    0,
    STATICGUIDOF (KSDATAFORMAT_TYPE_AUDIO),
    STATICGUIDOF (KSDATAFORMAT_SUBTYPE_ANALOG),
    STATICGUIDOF (KSDATAFORMAT_SPECIFIER_NONE)
};

static const PKSDATARANGE g_WaveStreamingDataRanges[] =
{
    reinterpret_cast<PKSDATARANGE> (const_cast<KSDATARANGE_AUDIO*> (&g_WaveFloatDataRange)),
    reinterpret_cast<PKSDATARANGE> (const_cast<KSDATARANGE_AUDIO*> (&g_WavePcmDataRange))
};

static const PKSDATARANGE g_WaveBridgeDataRanges[] =
{
    const_cast<PKSDATARANGE> (&g_WaveBridgeDataRange)
};

// ----------------------------------------------------------------------------
// WaveRT フィルタのピン・ノード・接続記述子テーブル
// ----------------------------------------------------------------------------
static const PCPIN_DESCRIPTOR g_WaveMiniportPins[kWavePinCount] =
{
    // [0] kWavePinRenderSink: audiodg.exe からの再生ストリーム入力
    {
        1, 1, 0,
        nullptr,
        {
            0, nullptr,
            0, nullptr,
            SIZEOF_ARRAY (g_WaveStreamingDataRanges),
            const_cast<PKSDATARANGE*> (g_WaveStreamingDataRanges),
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_SINK,
            &KSCATEGORY_AUDIO,
            nullptr,
            0
        }
    },
    // [1] kWavePinRenderBridge: Topology へのブリッジ出力
    {
        0, 0, 0,
        nullptr,
        {
            0, nullptr,
            0, nullptr,
            SIZEOF_ARRAY (g_WaveBridgeDataRanges),
            const_cast<PKSDATARANGE*> (g_WaveBridgeDataRanges),
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            nullptr,
            0
        }
    },
    // [2] kWavePinCaptureBridge: Topology からのブリッジ入力
    {
        0, 0, 0,
        nullptr,
        {
            0, nullptr,
            0, nullptr,
            SIZEOF_ARRAY (g_WaveBridgeDataRanges),
            const_cast<PKSDATARANGE*> (g_WaveBridgeDataRanges),
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            nullptr,
            0
        }
    },
    // [3] kWavePinCaptureSource: audiodg.exe への録音ストリーム出力
    {
        1, 1, 0,
        nullptr,
        {
            0, nullptr,
            0, nullptr,
            SIZEOF_ARRAY (g_WaveStreamingDataRanges),
            const_cast<PKSDATARANGE*> (g_WaveStreamingDataRanges),
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_SINK,
            &KSCATEGORY_AUDIO,
            &KSAUDFNAME_RECORDING_CONTROL,
            0
        }
    }
};

static const PCNODE_DESCRIPTOR g_WaveMiniportNodes[kWaveNodeCount] =
{
    // [0] kWaveNodeDac
    { 0, nullptr, &KSNODETYPE_DAC, nullptr },
    // [1] kWaveNodeAdc
    { 0, nullptr, &KSNODETYPE_ADC, nullptr }
};

static const PCCONNECTION_DESCRIPTOR g_WaveMiniportConnections[] =
{
    // 再生経路: RenderSink -> DAC -> RenderBridge
    { PCFILTER_NODE, kWavePinRenderSink,    kWaveNodeDac,  1                    },
    { kWaveNodeDac,  0,                     PCFILTER_NODE, kWavePinRenderBridge },
    // 録音経路: CaptureBridge -> ADC -> CaptureSource
    { PCFILTER_NODE, kWavePinCaptureBridge, kWaveNodeAdc,  1                     },
    { kWaveNodeAdc,  0,                     PCFILTER_NODE, kWavePinCaptureSource }
};

static const GUID g_WaveCategories[] =
{
    STATICGUIDOF (KSCATEGORY_AUDIO),
    STATICGUIDOF (KSCATEGORY_RENDER),
    STATICGUIDOF (KSCATEGORY_CAPTURE),
    STATICGUIDOF (KSCATEGORY_REALTIME)
};

static const PCFILTER_DESCRIPTOR g_WaveMiniportFilterDescriptor =
{
    0,
    nullptr,
    sizeof (PCPIN_DESCRIPTOR),
    SIZEOF_ARRAY (g_WaveMiniportPins),
    g_WaveMiniportPins,
    sizeof (PCNODE_DESCRIPTOR),
    SIZEOF_ARRAY (g_WaveMiniportNodes),
    g_WaveMiniportNodes,
    SIZEOF_ARRAY (g_WaveMiniportConnections),
    g_WaveMiniportConnections,
    SIZEOF_ARRAY (g_WaveCategories),
    g_WaveCategories
};

// ----------------------------------------------------------------------------
// KSDATAFORMAT -> vgaudio::StreamFormatInfo 検証・変換ヘルパ
// ----------------------------------------------------------------------------
static bool IsSupportedSampleRate (ULONG sr) noexcept
{
    return sr == 44100u || sr == 48000u || sr == 88200u || sr == 96000u;
}

static ULONG ChooseBestSampleRateInRange (ULONG minHz, ULONG maxHz) noexcept
{
    // 配信・映像標準の 48000 Hz を最優先で選択
    const ULONG candidates[] = { 48000u, 44100u, 96000u, 88200u };
    for (ULONG c : candidates)
    {
        if (c >= minHz && c <= maxHz)
            return c;
    }
    return 0u;
}

static NTSTATUS ParseKsDataFormat (
    _In_  PKSDATAFORMAT              DataFormat,
    _Out_ vgaudio::StreamFormatInfo* OutInfo) noexcept
{
    if (DataFormat == nullptr || OutInfo == nullptr)
        return STATUS_INVALID_PARAMETER;

    if (DataFormat->FormatSize < sizeof (KSDATAFORMAT_WAVEFORMATEX))
        return STATUS_INVALID_PARAMETER;

    if (! IsEqualGUIDAligned (DataFormat->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
        ! IsEqualGUIDAligned (DataFormat->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX))
    {
        return STATUS_INVALID_PARAMETER;
    }

    const WAVEFORMATEX* wf = &reinterpret_cast<PKSDATAFORMAT_WAVEFORMATEX> (DataFormat)->WaveFormatEx;
    if (wf->nChannels < 1 || wf->nChannels > 2)
        return STATUS_NOT_SUPPORTED;
    if (! IsSupportedSampleRate (wf->nSamplesPerSec))
        return STATUS_NOT_SUPPORTED;

    vgaudio::SampleFormat sampleFmt = vgaudio::SampleFormat::Pcm16;
    uint16_t bytesPerSample = 0;

    if (wf->wFormatTag == WAVE_FORMAT_PCM)
    {
        if (wf->wBitsPerSample == 16)
        {
            sampleFmt      = vgaudio::SampleFormat::Pcm16;
            bytesPerSample = 2;
        }
        else if (wf->wBitsPerSample == 24)
        {
            sampleFmt      = vgaudio::SampleFormat::Pcm24Packed;
            bytesPerSample = 3;
        }
        else if (wf->wBitsPerSample == 32)
        {
            sampleFmt      = vgaudio::SampleFormat::Pcm32;
            bytesPerSample = 4;
        }
        else
        {
            return STATUS_NOT_SUPPORTED;
        }
    }
    else if (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        if (wf->wBitsPerSample != 32)
            return STATUS_NOT_SUPPORTED;
        sampleFmt      = vgaudio::SampleFormat::Float32;
        bytesPerSample = 4;
    }
    else if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        if (DataFormat->FormatSize < sizeof (KSDATAFORMAT_WAVEFORMATEXTENSIBLE) ||
            wf->cbSize < (sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX)))
        {
            return STATUS_INVALID_PARAMETER;
        }

        const auto* wfExt = &reinterpret_cast<PKSDATAFORMAT_WAVEFORMATEXTENSIBLE> (DataFormat)->WaveFormatExt;
        if (IsEqualGUIDAligned (wfExt->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
        {
            if (wf->wBitsPerSample != 32 || wfExt->Samples.wValidBitsPerSample != 32)
                return STATUS_NOT_SUPPORTED;
            sampleFmt      = vgaudio::SampleFormat::Float32;
            bytesPerSample = 4;
        }
        else if (IsEqualGUIDAligned (wfExt->SubFormat, KSDATAFORMAT_SUBTYPE_PCM))
        {
            if (wf->wBitsPerSample == 16 && wfExt->Samples.wValidBitsPerSample == 16)
            {
                sampleFmt      = vgaudio::SampleFormat::Pcm16;
                bytesPerSample = 2;
            }
            else if (wf->wBitsPerSample == 24 && wfExt->Samples.wValidBitsPerSample == 24)
            {
                sampleFmt      = vgaudio::SampleFormat::Pcm24Packed;
                bytesPerSample = 3;
            }
            else if (wf->wBitsPerSample == 32 && wfExt->Samples.wValidBitsPerSample == 24)
            {
                sampleFmt      = vgaudio::SampleFormat::Pcm24In32;
                bytesPerSample = 4;
            }
            else if (wf->wBitsPerSample == 32 && wfExt->Samples.wValidBitsPerSample == 32)
            {
                sampleFmt      = vgaudio::SampleFormat::Pcm32;
                bytesPerSample = 4;
            }
            else
            {
                return STATUS_NOT_SUPPORTED;
            }
        }
        else
        {
            return STATUS_NOT_SUPPORTED;
        }
    }
    else
    {
        return STATUS_NOT_SUPPORTED;
    }

    const uint16_t expectedBlockAlign = static_cast<uint16_t> (wf->nChannels * bytesPerSample);
    if (wf->nBlockAlign != expectedBlockAlign)
        return STATUS_INVALID_PARAMETER;

    OutInfo->sampleRate     = wf->nSamplesPerSec;
    OutInfo->channels       = wf->nChannels;
    OutInfo->bytesPerSample = bytesPerSample;
    OutInfo->blockAlign     = expectedBlockAlign;
    OutInfo->format         = sampleFmt;
    return STATUS_SUCCESS;
}

// ============================================================================
// CVocalGzzioMiniportWaveRTStream
// ============================================================================
class CVocalGzzioMiniportWaveRTStream : public IMiniportWaveRTStreamNotification,
                                        public CUnknown
{
private:
    static constexpr ULONG kMaxNotifyEvents = 8;

    PVOCALGZZIOADAPTERCOMMON  m_AdapterCommon;
    PPORTWAVERTSTREAM         m_PortStream;
    BOOLEAN                   m_IsCapture;
    KSSTATE                   m_State;
    vgaudio::StreamFormatInfo m_Format;

    PMDL                      m_DmaMdl;
    uint8_t*                  m_DmaBuffer;
    ULONG                     m_DmaBufferSize;
    ULONG                     m_NotificationCount;
    ULONG                     m_NotifyPeriodBytes;

    KSPIN_LOCK                m_StreamLock;
    KTIMER                    m_Timer;
    KDPC                      m_Dpc;
    PKEVENT                   m_NotifyEvents[kMaxNotifyEvents];

    LONGLONG                  m_QpcFreq;
    LONGLONG                  m_LastQpc;
    ULONG64                   m_TickRemainder;
    ULONG64                   m_TransferredBytes;
    ULONG64                   m_NextNotifyByte;

    static KDEFERRED_ROUTINE TimerDpcThunk;

    void AdvanceStreamLocked() noexcept;
    void StopTimerAndFlushDpc() noexcept;

public:
    DECLARE_STD_UNKNOWN()

    explicit CVocalGzzioMiniportWaveRTStream (PUNKNOWN pUnknownOuter);
    ~CVocalGzzioMiniportWaveRTStream() override;

    NTSTATUS Init (
        _In_ PVOCALGZZIOADAPTERCOMMON AdapterCommon,
        _In_ PPORTWAVERTSTREAM        PortStream,
        _In_ BOOLEAN                  Capture,
        _In_ PKSDATAFORMAT            DataFormat);

    IMP_IMiniportWaveRTStream;
    IMP_IMiniportWaveRTStreamNotification;
};

CVocalGzzioMiniportWaveRTStream::CVocalGzzioMiniportWaveRTStream (PUNKNOWN pUnknownOuter)
    : CUnknown (pUnknownOuter),
      m_AdapterCommon (nullptr),
      m_PortStream (nullptr),
      m_IsCapture (FALSE),
      m_State (KSSTATE_STOP),
      m_DmaMdl (nullptr),
      m_DmaBuffer (nullptr),
      m_DmaBufferSize (0),
      m_NotificationCount (0),
      m_NotifyPeriodBytes (0),
      m_QpcFreq (10000000LL),
      m_LastQpc (0),
      m_TickRemainder (0),
      m_TransferredBytes (0),
      m_NextNotifyByte (0)
{
    RtlZeroMemory (&m_Format, sizeof (m_Format));
    RtlZeroMemory (m_NotifyEvents, sizeof (m_NotifyEvents));

    KeInitializeSpinLock (&m_StreamLock);
    KeInitializeTimerEx (&m_Timer, SynchronizationTimer);
    KeInitializeDpc (&m_Dpc, TimerDpcThunk, this);

    LARGE_INTEGER freq = { 0 };
    KeQueryPerformanceCounter (&freq);
    if (freq.QuadPart > 0)
        m_QpcFreq = freq.QuadPart;
}

CVocalGzzioMiniportWaveRTStream::~CVocalGzzioMiniportWaveRTStream()
{
    StopTimerAndFlushDpc();

    if (! m_IsCapture && m_AdapterCommon != nullptr)
        m_AdapterCommon->SetRenderActive (FALSE, m_Format.sampleRate);

    if (m_DmaMdl != nullptr)
        FreeAudioBuffer (m_DmaMdl, m_DmaBufferSize);

    if (m_PortStream != nullptr)
    {
        m_PortStream->Release();
        m_PortStream = nullptr;
    }

    if (m_AdapterCommon != nullptr)
    {
        m_AdapterCommon->Release();
        m_AdapterCommon = nullptr;
    }
}

void CVocalGzzioMiniportWaveRTStream::StopTimerAndFlushDpc() noexcept
{
    KeCancelTimer (&m_Timer);
    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
        KeFlushQueuedDpcs();
}

VOID CVocalGzzioMiniportWaveRTStream::TimerDpcThunk (
    _In_     PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER (Dpc);
    UNREFERENCED_PARAMETER (SystemArgument1);
    UNREFERENCED_PARAMETER (SystemArgument2);

    auto* self = reinterpret_cast<CVocalGzzioMiniportWaveRTStream*> (DeferredContext);
    if (self == nullptr)
        return;

    KeAcquireSpinLockAtDpcLevel (&self->m_StreamLock);
    if (self->m_State == KSSTATE_RUN)
        self->AdvanceStreamLocked();
    KeReleaseSpinLockFromDpcLevel (&self->m_StreamLock);
}

void CVocalGzzioMiniportWaveRTStream::AdvanceStreamLocked() noexcept
{
    if (m_State != KSSTATE_RUN ||
        m_DmaBuffer == nullptr ||
        m_DmaBufferSize == 0 ||
        m_Format.blockAlign == 0 ||
        m_QpcFreq <= 0)
    {
        return;
    }

    const LARGE_INTEGER now = KeQueryPerformanceCounter (nullptr);
    LONGLONG deltaTicks = now.QuadPart - m_LastQpc;
    if (deltaTicks <= 0)
        return;
    m_LastQpc = now.QuadPart;

    // スリープ復帰等で1秒以上飛んだ場合は最大0.5秒分にクランプして安全に追従する
    if (deltaTicks > (m_QpcFreq / 2))
    {
        deltaTicks      = m_QpcFreq / 2;
        m_TickRemainder = 0;
    }

    const ULONG64 totalScaled = static_cast<ULONG64> (deltaTicks) * static_cast<ULONG64> (m_Format.sampleRate)
                              + m_TickRemainder;
    ULONG64 elapsedFrames = totalScaled / static_cast<ULONG64> (m_QpcFreq);
    m_TickRemainder       = totalScaled % static_cast<ULONG64> (m_QpcFreq);

    if (elapsedFrames == 0)
        return;

    const ULONG maxBufferFrames = m_DmaBufferSize / m_Format.blockAlign;
    if (maxBufferFrames == 0)
        return;

    while (elapsedFrames > 0)
    {
        const uint32_t chunkFrames = (elapsedFrames > maxBufferFrames)
                                   ? static_cast<uint32_t> (maxBufferFrames)
                                   : static_cast<uint32_t> (elapsedFrames);
        const uint32_t byteOffset  = static_cast<uint32_t> (m_TransferredBytes % m_DmaBufferSize);

        if (m_AdapterCommon != nullptr)
        {
            if (! m_IsCapture)
            {
                m_AdapterCommon->PushRenderFrames (
                    m_DmaBuffer,
                    m_DmaBufferSize,
                    byteOffset,
                    chunkFrames,
                    &m_Format);
            }
            else
            {
                m_AdapterCommon->PopCaptureFrames (
                    m_DmaBuffer,
                    m_DmaBufferSize,
                    byteOffset,
                    chunkFrames,
                    &m_Format);
            }
        }

        m_TransferredBytes += static_cast<ULONG64> (chunkFrames) * static_cast<ULONG64> (m_Format.blockAlign);
        elapsedFrames      -= chunkFrames;
    }

    // イベント駆動型 WASAPI の周期通知シグナル送出
    if (m_NotifyPeriodBytes > 0 && m_TransferredBytes >= m_NextNotifyByte)
    {
        while (m_NextNotifyByte <= m_TransferredBytes)
            m_NextNotifyByte += m_NotifyPeriodBytes;

        for (ULONG i = 0; i < kMaxNotifyEvents; ++i)
        {
            if (m_NotifyEvents[i] != nullptr)
                KeSetEvent (m_NotifyEvents[i], IO_SOUND_INCREMENT, FALSE);
        }
    }
}

NTSTATUS CVocalGzzioMiniportWaveRTStream::Init (
    _In_ PVOCALGZZIOADAPTERCOMMON AdapterCommon,
    _In_ PPORTWAVERTSTREAM        PortStream,
    _In_ BOOLEAN                  Capture,
    _In_ PKSDATAFORMAT            DataFormat)
{
    if (AdapterCommon == nullptr || PortStream == nullptr || DataFormat == nullptr)
        return STATUS_INVALID_PARAMETER;

    NTSTATUS status = ParseKsDataFormat (DataFormat, &m_Format);
    if (! NT_SUCCESS (status))
        return status;

    m_AdapterCommon = AdapterCommon;
    m_AdapterCommon->AddRef();

    m_PortStream = PortStream;
    m_PortStream->AddRef();

    m_IsCapture = Capture;
    return STATUS_SUCCESS;
}

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportWaveRTStream::NonDelegatingQueryInterface (
    _In_         REFIID riid,
    _COM_Outptr_ PVOID* ppvObject)
{
    if (ppvObject == nullptr)
        return STATUS_INVALID_PARAMETER;

    if (IsEqualGUIDAligned (riid, IID_IUnknown))
    {
        *ppvObject = reinterpret_cast<PVOID> (static_cast<PUNKNOWN> (static_cast<PMINIPORTWAVERTSTREAMNOTIFICATION> (this)));
    }
    else if (IsEqualGUIDAligned (riid, IID_IMiniportWaveRTStream))
    {
        *ppvObject = reinterpret_cast<PVOID> (static_cast<PMINIPORTWAVERTSTREAM> (this));
    }
    else if (IsEqualGUIDAligned (riid, IID_IMiniportWaveRTStreamNotification))
    {
        *ppvObject = reinterpret_cast<PVOID> (static_cast<PMINIPORTWAVERTSTREAMNOTIFICATION> (this));
    }
    else
    {
        *ppvObject = nullptr;
        return STATUS_INVALID_PARAMETER;
    }

    reinterpret_cast<PUNKNOWN> (*ppvObject)->AddRef();
    return STATUS_SUCCESS;
}

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportWaveRTStream::SetFormat (_In_ PKSDATAFORMAT DataFormat)
{
    vgaudio::StreamFormatInfo newFmt = {};
    NTSTATUS status = ParseKsDataFormat (DataFormat, &newFmt);
    if (! NT_SUCCESS (status))
        return status;

    KIRQL oldIrql = PASSIVE_LEVEL;
    KeAcquireSpinLock (&m_StreamLock, &oldIrql);
    if (m_State == KSSTATE_RUN)
    {
        KeReleaseSpinLock (&m_StreamLock, oldIrql);
        return STATUS_INVALID_DEVICE_STATE;
    }
    m_Format = newFmt;
    KeReleaseSpinLock (&m_StreamLock, oldIrql);
    return STATUS_SUCCESS;
}

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportWaveRTStream::SetState (_In_ KSSTATE State)
{
    if (State == KSSTATE_RUN)
    {
        KIRQL oldIrql = PASSIVE_LEVEL;
        KeAcquireSpinLock (&m_StreamLock, &oldIrql);
        if (m_State != KSSTATE_RUN)
        {
            const LARGE_INTEGER now = KeQueryPerformanceCounter (nullptr);
            m_LastQpc       = now.QuadPart;
            m_TickRemainder = 0;
            if (m_NotifyPeriodBytes > 0)
            {
                m_NextNotifyByte = ((m_TransferredBytes / m_NotifyPeriodBytes) + 1ull)
                                 * static_cast<ULONG64> (m_NotifyPeriodBytes);
            }
            m_State = KSSTATE_RUN;

            if (! m_IsCapture && m_AdapterCommon != nullptr)
                m_AdapterCommon->SetRenderActive (TRUE, m_Format.sampleRate);

            // 2ms 周期タイマ起動 (正確なフレーム数は QPC で補間計測するためタイマジッタの影響を受けない)
            LARGE_INTEGER dueTime;
            dueTime.QuadPart = -20000LL; // 相対 2.0 ms (100ns 単位)
            KeSetTimerEx (&m_Timer, dueTime, 2, &m_Dpc);
        }
        KeReleaseSpinLock (&m_StreamLock, oldIrql);
        return STATUS_SUCCESS;
    }

    // KSSTATE_PAUSE / KSSTATE_ACQUIRE / KSSTATE_STOP への遷移
    const bool wasRunning = (m_State == KSSTATE_RUN);
    if (wasRunning)
        StopTimerAndFlushDpc();

    KIRQL oldIrql = PASSIVE_LEVEL;
    KeAcquireSpinLock (&m_StreamLock, &oldIrql);
    if (m_State == KSSTATE_RUN)
        AdvanceStreamLocked();
    m_State = State;

    if (State == KSSTATE_STOP)
    {
        m_TransferredBytes = 0;
        m_TickRemainder    = 0;
        m_NextNotifyByte   = m_NotifyPeriodBytes;
        if (m_DmaBuffer != nullptr && m_DmaBufferSize > 0)
            RtlZeroMemory (m_DmaBuffer, m_DmaBufferSize);
    }
    KeReleaseSpinLock (&m_StreamLock, oldIrql);

    if (! m_IsCapture && m_AdapterCommon != nullptr)
        m_AdapterCommon->SetRenderActive (FALSE, m_Format.sampleRate);

    return STATUS_SUCCESS;
}

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportWaveRTStream::GetPosition (_Out_ PKSAUDIO_POSITION Position)
{
    if (Position == nullptr)
        return STATUS_INVALID_PARAMETER;

    KIRQL oldIrql = PASSIVE_LEVEL;
    KeAcquireSpinLock (&m_StreamLock, &oldIrql);

    if (m_State == KSSTATE_RUN)
        AdvanceStreamLocked();

    const ULONG64 posBytes = (m_DmaBufferSize > 0)
                           ? (m_TransferredBytes % static_cast<ULONG64> (m_DmaBufferSize))
                           : 0ull;

    Position->PlayOffset  = posBytes;
    Position->WriteOffset = posBytes;

    KeReleaseSpinLock (&m_StreamLock, oldIrql);
    return STATUS_SUCCESS;
}

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportWaveRTStream::AllocateBufferWithNotification (
    _In_  ULONG                NotificationCount,
    _In_  ULONG                RequestedSize,
    _Out_ PMDL*                AudioBufferMdl,
    _Out_ ULONG*               ActualSize,
    _Out_ ULONG*               OffsetFromFirstPage,
    _Out_ MEMORY_CACHING_TYPE* CacheType)
{
    if (AudioBufferMdl == nullptr || ActualSize == nullptr ||
        OffsetFromFirstPage == nullptr || CacheType == nullptr)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *AudioBufferMdl      = nullptr;
    *ActualSize          = 0;
    *OffsetFromFirstPage = 0;
    *CacheType           = MmCached;

    if (m_PortStream == nullptr || m_Format.blockAlign == 0)
        return STATUS_INVALID_DEVICE_STATE;

    if (RequestedSize == 0)
        return STATUS_UNSUCCESSFUL;

    // PAGE_SIZE (4096) かつ (BlockAlign * max(1, NotificationCount)) の公倍数へ切り上げる
    const ULONG notifyDiv  = (NotificationCount > 0) ? NotificationCount : 1u;
    const ULONG frameAlign = static_cast<ULONG> (m_Format.blockAlign) * notifyDiv;
    ULONG allocBytes       = (RequestedSize + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
    while ((allocBytes % frameAlign) != 0u)
    {
        if (allocBytes > 0x400000u) // 上限 4MB 安全ガード
            return STATUS_INSUFFICIENT_RESOURCES;
        allocBytes += PAGE_SIZE;
    }

    PHYSICAL_ADDRESS highAddr;
    highAddr.QuadPart = 0xFFFFFFFFFFFFFull;

    PMDL mdl = m_PortStream->AllocatePagesForMdl (highAddr, allocBytes);
    if (mdl == nullptr)
        return STATUS_INSUFFICIENT_RESOURCES;

    PVOID mapped = m_PortStream->MapAllocatedPages (mdl, MmCached);
    if (mapped == nullptr)
    {
        m_PortStream->FreePagesFromMdl (mdl);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory (mapped, allocBytes);

    m_DmaMdl            = mdl;
    m_DmaBuffer         = reinterpret_cast<uint8_t*> (mapped);
    m_DmaBufferSize     = allocBytes;
    m_NotificationCount = NotificationCount;
    m_NotifyPeriodBytes = (NotificationCount > 0) ? (allocBytes / NotificationCount) : 0u;
    m_NextNotifyByte    = m_NotifyPeriodBytes;

    *AudioBufferMdl      = mdl;
    *ActualSize          = allocBytes;
    *OffsetFromFirstPage = 0;
    *CacheType           = MmCached;
    return STATUS_SUCCESS;
}

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportWaveRTStream::AllocateAudioBuffer (
    _In_  ULONG                RequestedSize,
    _Out_ PMDL*                AudioBufferMdl,
    _Out_ ULONG*               ActualSize,
    _Out_ ULONG*               OffsetFromFirstPage,
    _Out_ MEMORY_CACHING_TYPE* CacheType)
{
    return AllocateBufferWithNotification (
        0,
        RequestedSize,
        AudioBufferMdl,
        ActualSize,
        OffsetFromFirstPage,
        CacheType);
}

STDMETHODIMP_ (VOID)
CVocalGzzioMiniportWaveRTStream::FreeBufferWithNotification (
    _In_ PMDL  AudioBufferMdl,
    _In_ ULONG BufferSize)
{
    FreeAudioBuffer (AudioBufferMdl, BufferSize);
}

STDMETHODIMP_ (VOID)
CVocalGzzioMiniportWaveRTStream::FreeAudioBuffer (
    _In_opt_ PMDL  AudioBufferMdl,
    _In_     ULONG BufferSize)
{
    UNREFERENCED_PARAMETER (BufferSize);

    StopTimerAndFlushDpc();

    KIRQL oldIrql = PASSIVE_LEVEL;
    KeAcquireSpinLock (&m_StreamLock, &oldIrql);
    PVOID   mapped = m_DmaBuffer;
    PMDL    mdl    = (AudioBufferMdl != nullptr) ? AudioBufferMdl : m_DmaMdl;
    m_DmaBuffer         = nullptr;
    m_DmaMdl            = nullptr;
    m_DmaBufferSize     = 0;
    m_NotificationCount = 0;
    m_NotifyPeriodBytes = 0;
    KeReleaseSpinLock (&m_StreamLock, oldIrql);

    if (m_PortStream != nullptr && mdl != nullptr)
    {
        if (mapped != nullptr)
            m_PortStream->UnmapAllocatedPages (mapped, mdl);
        m_PortStream->FreePagesFromMdl (mdl);
    }
}

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportWaveRTStream::RegisterNotificationEvent (_In_ PKEVENT NotificationEvent)
{
    if (NotificationEvent == nullptr)
        return STATUS_INVALID_PARAMETER;

    KIRQL oldIrql = PASSIVE_LEVEL;
    KeAcquireSpinLock (&m_StreamLock, &oldIrql);

    for (ULONG i = 0; i < kMaxNotifyEvents; ++i)
    {
        if (m_NotifyEvents[i] == NotificationEvent)
        {
            KeReleaseSpinLock (&m_StreamLock, oldIrql);
            return STATUS_SUCCESS;
        }
    }

    for (ULONG i = 0; i < kMaxNotifyEvents; ++i)
    {
        if (m_NotifyEvents[i] == nullptr)
        {
            m_NotifyEvents[i] = NotificationEvent;
            KeReleaseSpinLock (&m_StreamLock, oldIrql);
            return STATUS_SUCCESS;
        }
    }

    KeReleaseSpinLock (&m_StreamLock, oldIrql);
    return STATUS_INSUFFICIENT_RESOURCES;
}

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportWaveRTStream::UnregisterNotificationEvent (_In_ PKEVENT NotificationEvent)
{
    if (NotificationEvent == nullptr)
        return STATUS_INVALID_PARAMETER;

    KIRQL oldIrql = PASSIVE_LEVEL;
    KeAcquireSpinLock (&m_StreamLock, &oldIrql);
    for (ULONG i = 0; i < kMaxNotifyEvents; ++i)
    {
        if (m_NotifyEvents[i] == NotificationEvent)
        {
            m_NotifyEvents[i] = nullptr;
            KeReleaseSpinLock (&m_StreamLock, oldIrql);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock (&m_StreamLock, oldIrql);
    return STATUS_INVALID_PARAMETER;
}

STDMETHODIMP_ (VOID)
CVocalGzzioMiniportWaveRTStream::GetHWLatency (_Out_ KSRTAUDIO_HWLATENCY* hwLatency)
{
    if (hwLatency != nullptr)
    {
        hwLatency->FifoSize     = 0;
        hwLatency->ChipsetDelay = 0;
        hwLatency->CodecDelay   = 0;
    }
}

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportWaveRTStream::GetPositionRegister (_Out_ KSRTAUDIO_HWREGISTER* Register)
{
    UNREFERENCED_PARAMETER (Register);
    return STATUS_NOT_SUPPORTED;
}

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportWaveRTStream::GetClockRegister (_Out_ KSRTAUDIO_HWREGISTER* Register)
{
    UNREFERENCED_PARAMETER (Register);
    return STATUS_NOT_SUPPORTED;
}

// ============================================================================
// CVocalGzzioMiniportWaveRT クラス
// ============================================================================
class CVocalGzzioMiniportWaveRT : public IMiniportWaveRT,
                                  public CUnknown
{
private:
    PVOCALGZZIOADAPTERCOMMON m_AdapterCommon;

public:
    DECLARE_STD_UNKNOWN()

    explicit CVocalGzzioMiniportWaveRT (PUNKNOWN pUnknownOuter)
        : CUnknown (pUnknownOuter),
          m_AdapterCommon (nullptr)
    {
    }

    ~CVocalGzzioMiniportWaveRT() override
    {
        if (m_AdapterCommon != nullptr)
        {
            m_AdapterCommon->Release();
            m_AdapterCommon = nullptr;
        }
    }

    IMP_IMiniportWaveRT;
};

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportWaveRT::NonDelegatingQueryInterface (
    _In_         REFIID riid,
    _COM_Outptr_ PVOID* ppvObject)
{
    if (ppvObject == nullptr)
        return STATUS_INVALID_PARAMETER;

    if (IsEqualGUIDAligned (riid, IID_IUnknown))
    {
        *ppvObject = reinterpret_cast<PVOID> (static_cast<PUNKNOWN> (static_cast<PMINIPORTWAVERT> (this)));
    }
    else if (IsEqualGUIDAligned (riid, IID_IMiniport) ||
             IsEqualGUIDAligned (riid, IID_IMiniportWaveRT))
    {
        *ppvObject = reinterpret_cast<PVOID> (static_cast<PMINIPORTWAVERT> (this));
    }
    else
    {
        *ppvObject = nullptr;
        return STATUS_INVALID_PARAMETER;
    }

    reinterpret_cast<PUNKNOWN> (*ppvObject)->AddRef();
    return STATUS_SUCCESS;
}

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportWaveRT::Init (
    _In_ PUNKNOWN      UnknownAdapter,
    _In_ PRESOURCELIST ResourceList,
    _In_ PPORTWAVERT   Port)
{
    UNREFERENCED_PARAMETER (ResourceList);
    UNREFERENCED_PARAMETER (Port);

    if (UnknownAdapter == nullptr)
        return STATUS_INVALID_PARAMETER;

    return UnknownAdapter->QueryInterface (
        IID_IVocalGzzioAdapterCommon,
        reinterpret_cast<PVOID*> (&m_AdapterCommon));
}

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportWaveRT::GetDescription (
    _Out_ PPCFILTER_DESCRIPTOR* Description)
{
    if (Description == nullptr)
        return STATUS_INVALID_PARAMETER;

    *Description = const_cast<PPCFILTER_DESCRIPTOR> (&g_WaveMiniportFilterDescriptor);
    return STATUS_SUCCESS;
}

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportWaveRT::DataRangeIntersection (
    _In_  ULONG        PinId,
    _In_  PKSDATARANGE ClientDataRange,
    _In_  PKSDATARANGE MyDataRange,
    _In_  ULONG        OutputBufferLength,
    _Out_writes_bytes_to_opt_ (OutputBufferLength, *ResultantFormatLength)
          PVOID        ResultantFormat,
    _Out_ PULONG       ResultantFormatLength)
{
    if (ResultantFormatLength == nullptr || ClientDataRange == nullptr || MyDataRange == nullptr)
        return STATUS_INVALID_PARAMETER;

    if (PinId != kWavePinRenderSink && PinId != kWavePinCaptureSource)
        return STATUS_NOT_IMPLEMENTED;

    if (! IsEqualGUIDAligned (ClientDataRange->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) &&
        ! IsEqualGUIDAligned (ClientDataRange->MajorFormat, KSDATAFORMAT_TYPE_WILDCARD))
    {
        return STATUS_NO_MATCH;
    }

    if (! IsEqualGUIDAligned (ClientDataRange->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX) &&
        ! IsEqualGUIDAligned (ClientDataRange->Specifier, KSDATAFORMAT_SPECIFIER_WILDCARD))
    {
        return STATUS_NO_MATCH;
    }

    const auto* myAudio = reinterpret_cast<const KSDATARANGE_AUDIO*> (MyDataRange);
    ULONG minFreq = myAudio->MinimumSampleFrequency;
    ULONG maxFreq = myAudio->MaximumSampleFrequency;
    ULONG minBits = myAudio->MinimumBitsPerSample;
    ULONG maxBits = myAudio->MaximumBitsPerSample;
    ULONG maxCh   = myAudio->MaximumChannels;

    if (IsEqualGUIDAligned (ClientDataRange->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX) &&
        ClientDataRange->FormatSize >= sizeof (KSDATARANGE_AUDIO))
    {
        const auto* clientAudio = reinterpret_cast<const KSDATARANGE_AUDIO*> (ClientDataRange);
        if (clientAudio->MinimumSampleFrequency > minFreq) minFreq = clientAudio->MinimumSampleFrequency;
        if (clientAudio->MaximumSampleFrequency < maxFreq) maxFreq = clientAudio->MaximumSampleFrequency;
        if (clientAudio->MinimumBitsPerSample   > minBits) minBits = clientAudio->MinimumBitsPerSample;
        if (clientAudio->MaximumBitsPerSample   < maxBits) maxBits = clientAudio->MaximumBitsPerSample;
        if (clientAudio->MaximumChannels        < maxCh)   maxCh   = clientAudio->MaximumChannels;
    }

    if (minFreq > maxFreq || minBits > maxBits || maxCh < 2)
        return STATUS_NO_MATCH;

    const ULONG chosenRate = ChooseBestSampleRateInRange (minFreq, maxFreq);
    if (chosenRate == 0u)
        return STATUS_NO_MATCH;

    const bool isFloat = IsEqualGUIDAligned (MyDataRange->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    USHORT containerBits = 32;
    USHORT validBits     = 32;

    if (isFloat)
    {
        if (32u < minBits || 32u > maxBits)
            return STATUS_NO_MATCH;
        containerBits = 32;
        validBits     = 32;
    }
    else
    {
        if (32u >= minBits && 32u <= maxBits)
        {
            containerBits = 32;
            validBits     = 24; // 24-bit in 32-bit container (DAW / WASAPI 標準)
        }
        else if (24u >= minBits && 24u <= maxBits)
        {
            containerBits = 24;
            validBits     = 24;
        }
        else if (16u >= minBits && 16u <= maxBits)
        {
            containerBits = 16;
            validBits     = 16;
        }
        else
        {
            return STATUS_NO_MATCH;
        }
    }

    *ResultantFormatLength = sizeof (KSDATAFORMAT_WAVEFORMATEXTENSIBLE);
    if (OutputBufferLength == 0)
        return STATUS_BUFFER_OVERFLOW;
    if (OutputBufferLength < sizeof (KSDATAFORMAT_WAVEFORMATEXTENSIBLE) || ResultantFormat == nullptr)
        return STATUS_BUFFER_TOO_SMALL;

    auto* outFmt = reinterpret_cast<PKSDATAFORMAT_WAVEFORMATEXTENSIBLE> (ResultantFormat);
    RtlZeroMemory (outFmt, sizeof (KSDATAFORMAT_WAVEFORMATEXTENSIBLE));

    const USHORT channels   = 2;
    const USHORT blockAlign = static_cast<USHORT> (channels * (containerBits / 8u));

    outFmt->DataFormat.FormatSize  = sizeof (KSDATAFORMAT_WAVEFORMATEXTENSIBLE);
    outFmt->DataFormat.Flags       = 0;
    outFmt->DataFormat.SampleSize  = blockAlign;
    outFmt->DataFormat.Reserved    = 0;
    outFmt->DataFormat.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
    outFmt->DataFormat.SubFormat   = isFloat ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
    outFmt->DataFormat.Specifier   = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;

    outFmt->WaveFormatExt.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
    outFmt->WaveFormatExt.Format.nChannels       = channels;
    outFmt->WaveFormatExt.Format.nSamplesPerSec  = chosenRate;
    outFmt->WaveFormatExt.Format.wBitsPerSample  = containerBits;
    outFmt->WaveFormatExt.Format.nBlockAlign     = blockAlign;
    outFmt->WaveFormatExt.Format.nAvgBytesPerSec = chosenRate * blockAlign;
    outFmt->WaveFormatExt.Format.cbSize          = sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX);

    outFmt->WaveFormatExt.Samples.wValidBitsPerSample = validBits;
    outFmt->WaveFormatExt.dwChannelMask               = KSAUDIO_SPEAKER_STEREO;
    outFmt->WaveFormatExt.SubFormat                   = outFmt->DataFormat.SubFormat;

    return STATUS_SUCCESS;
}

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportWaveRT::NewStream (
    _Out_ PMINIPORTWAVERTSTREAM* Stream,
    _In_  PPORTWAVERTSTREAM      PortStream,
    _In_  ULONG                  Pin,
    _In_  BOOLEAN                Capture,
    _In_  PKSDATAFORMAT          DataFormat)
{
    if (Stream == nullptr || PortStream == nullptr || DataFormat == nullptr)
        return STATUS_INVALID_PARAMETER;
    *Stream = nullptr;

    if ((Capture && Pin != kWavePinCaptureSource) ||
        (! Capture && Pin != kWavePinRenderSink))
    {
        return STATUS_INVALID_PARAMETER;
    }

    auto* streamObj = new (NonPagedPoolNx, VOCALGZZIO_POOL_TAG) CVocalGzzioMiniportWaveRTStream (nullptr);
    if (streamObj == nullptr)
        return STATUS_INSUFFICIENT_RESOURCES;

    streamObj->AddRef();
    NTSTATUS status = streamObj->Init (m_AdapterCommon, PortStream, Capture, DataFormat);
    if (NT_SUCCESS (status))
    {
        *Stream = static_cast<PMINIPORTWAVERTSTREAM> (streamObj);
    }
    else
    {
        streamObj->Release();
    }

    return status;
}

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportWaveRT::GetDeviceDescription (
    _Out_ PDEVICE_DESCRIPTION DeviceDescription)
{
    if (DeviceDescription == nullptr)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory (DeviceDescription, sizeof (DEVICE_DESCRIPTION));
    DeviceDescription->Version           = DEVICE_DESCRIPTION_VERSION2;
    DeviceDescription->Master            = TRUE;
    DeviceDescription->ScatterGather     = TRUE;
    DeviceDescription->Dma32BitAddresses = TRUE;
    DeviceDescription->Dma64BitAddresses = TRUE;
    DeviceDescription->MaximumLength     = 0x00100000; // 1 MB
    return STATUS_SUCCESS;
}

NTSTATUS CreateVocalGzzioMiniportWaveRT (
    _Out_    PUNKNOWN* ppUnknown,
    _In_opt_ PUNKNOWN  pUnknownOuter,
    _In_     POOL_TYPE poolType)
{
    if (ppUnknown == nullptr)
        return STATUS_INVALID_PARAMETER;

    STD_CREATE_BODY_WITH_TAG_ (
        CVocalGzzioMiniportWaveRT,
        ppUnknown,
        pUnknownOuter,
        poolType,
        VOCALGZZIO_POOL_TAG,
        PMINIPORTWAVERT);
}
