// ============================================================================
// Adapter.cpp
// VocalGzzioAudio.sys: DriverEntry / AddDevice / StartDevice / 共有ループバック管理
// ============================================================================
#define INITGUID
#include "VocalGzzioAudio.h"

// ----------------------------------------------------------------------------
// Windows 11 HVCI (メモリ整合性) 完全準拠の NX プール operator new / delete
// ----------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4996) // ExAllocatePoolWithTag (Win10 1809〜Win11 全世代互換のため明示使用)

static POOL_TYPE NormalizeNxPoolType (POOL_TYPE poolType) noexcept
{
    if (poolType == PagedPool)
        return PagedPool;
    return NonPagedPoolNx;
}

PVOID operator new (size_t iSize, POOL_TYPE poolType) noexcept
{
    const size_t allocBytes = (iSize > 0) ? iSize : 1;
    PVOID p = ExAllocatePoolWithTag (NormalizeNxPoolType (poolType), allocBytes, VOCALGZZIO_POOL_TAG);
    if (p != nullptr)
        RtlZeroMemory (p, allocBytes);
    return p;
}

PVOID operator new (size_t iSize, POOL_TYPE poolType, ULONG tag) noexcept
{
    const size_t allocBytes = (iSize > 0) ? iSize : 1;
    PVOID p = ExAllocatePoolWithTag (NormalizeNxPoolType (poolType), allocBytes, tag);
    if (p != nullptr)
        RtlZeroMemory (p, allocBytes);
    return p;
}

// unsized operator delete(PVOID) は stdunk.lib(stdunk.obj) 側の実装を使用し、
// C++17 の sized deallocation operator delete(PVOID, size_t) のみここで定義する
void operator delete (PVOID pVoid, size_t /*cbSize*/) noexcept
{
    if (pVoid != nullptr)
        ExFreePool (pVoid);
}

#pragma warning(pop)

// ============================================================================
// CVocalGzzioAdapterCommon
// Render ストリームと Capture ストリームの間で音声を受け渡す NonPagedPoolNx バス
// ============================================================================
class CVocalGzzioAdapterCommon : public IVocalGzzioAdapterCommon,
                                 public CUnknown
{
private:
    KSPIN_LOCK            m_SpinLock;
    vgaudio::LoopbackBus* m_LoopbackBus;

public:
    DECLARE_STD_UNKNOWN()

    explicit CVocalGzzioAdapterCommon (PUNKNOWN pUnknownOuter)
        : CUnknown (pUnknownOuter),
          m_LoopbackBus (nullptr)
    {
        KeInitializeSpinLock (&m_SpinLock);
    }

    ~CVocalGzzioAdapterCommon() override
    {
        if (m_LoopbackBus != nullptr)
        {
            delete m_LoopbackBus;
            m_LoopbackBus = nullptr;
        }
    }

    STDMETHODIMP_ (NTSTATUS) Init (_In_ PDEVICE_OBJECT DeviceObject) override
    {
        UNREFERENCED_PARAMETER (DeviceObject);
        m_LoopbackBus = new (NonPagedPoolNx, VOCALGZZIO_POOL_TAG) vgaudio::LoopbackBus();
        if (m_LoopbackBus == nullptr)
            return STATUS_INSUFFICIENT_RESOURCES;

        m_LoopbackBus->reset();
        return STATUS_SUCCESS;
    }

    STDMETHODIMP_ (VOID) PushRenderFrames (
        _In_reads_bytes_ (CyclicBytes) const uint8_t* CyclicBuffer,
        _In_ uint32_t                                 CyclicBytes,
        _In_ uint32_t                                 ByteOffset,
        _In_ uint32_t                                 FrameCount,
        _In_ const vgaudio::StreamFormatInfo*         Format) override
    {
        if (m_LoopbackBus == nullptr || CyclicBuffer == nullptr || Format == nullptr)
            return;

        KIRQL oldIrql = PASSIVE_LEVEL;
        KeAcquireSpinLock (&m_SpinLock, &oldIrql);
        m_LoopbackBus->pushFromCyclicRenderBuffer (CyclicBuffer, CyclicBytes, ByteOffset, FrameCount, *Format);
        KeReleaseSpinLock (&m_SpinLock, oldIrql);
    }

    STDMETHODIMP_ (VOID) PopCaptureFrames (
        _Out_writes_bytes_ (CyclicBytes) uint8_t*     CyclicBuffer,
        _In_ uint32_t                                 CyclicBytes,
        _In_ uint32_t                                 ByteOffset,
        _In_ uint32_t                                 FrameCount,
        _In_ const vgaudio::StreamFormatInfo*         Format) override
    {
        if (m_LoopbackBus == nullptr || CyclicBuffer == nullptr || Format == nullptr)
            return;

        KIRQL oldIrql = PASSIVE_LEVEL;
        KeAcquireSpinLock (&m_SpinLock, &oldIrql);
        m_LoopbackBus->popToCyclicCaptureBuffer (CyclicBuffer, CyclicBytes, ByteOffset, FrameCount, *Format);
        KeReleaseSpinLock (&m_SpinLock, oldIrql);
    }

    STDMETHODIMP_ (VOID) SetRenderActive (
        _In_ BOOLEAN  Active,
        _In_ uint32_t SampleRate) override
    {
        if (m_LoopbackBus == nullptr)
            return;

        KIRQL oldIrql = PASSIVE_LEVEL;
        KeAcquireSpinLock (&m_SpinLock, &oldIrql);
        m_LoopbackBus->setRenderStreamState (Active != FALSE, SampleRate);
        KeReleaseSpinLock (&m_SpinLock, oldIrql);
    }
};

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioAdapterCommon::NonDelegatingQueryInterface (
    _In_         REFIID riid,
    _COM_Outptr_ PVOID* ppvObject)
{
    if (ppvObject == nullptr)
        return STATUS_INVALID_PARAMETER;

    if (IsEqualGUIDAligned (riid, IID_IUnknown))
    {
        *ppvObject = reinterpret_cast<PVOID> (static_cast<PUNKNOWN> (static_cast<PVOCALGZZIOADAPTERCOMMON> (this)));
    }
    else if (IsEqualGUIDAligned (riid, IID_IVocalGzzioAdapterCommon))
    {
        *ppvObject = reinterpret_cast<PVOID> (static_cast<PVOCALGZZIOADAPTERCOMMON> (this));
    }
    else
    {
        *ppvObject = nullptr;
        return STATUS_INVALID_PARAMETER;
    }

    reinterpret_cast<PUNKNOWN> (*ppvObject)->AddRef();
    return STATUS_SUCCESS;
}

NTSTATUS CreateVocalGzzioAdapterCommon (
    _Out_    PUNKNOWN* ppUnknown,
    _In_opt_ PUNKNOWN  pUnknownOuter,
    _In_     POOL_TYPE poolType)
{
    if (ppUnknown == nullptr)
        return STATUS_INVALID_PARAMETER;

    STD_CREATE_BODY_WITH_TAG_ (
        CVocalGzzioAdapterCommon,
        ppUnknown,
        pUnknownOuter,
        NormalizeNxPoolType (poolType),
        VOCALGZZIO_POOL_TAG,
        PVOCALGZZIOADAPTERCOMMON);
}

// ============================================================================
// StartDevice: Topology サブデバイスと WaveRT サブデバイスを生成・接続する
// ============================================================================
static NTSTATUS InstallSubdevice (
    _In_     PDEVICE_OBJECT       DeviceObject,
    _In_     PIRP                 Irp,
    _In_     PWSTR                Name,
    _In_     REFGUID              PortClassId,
    _In_     NTSTATUS             (*MiniportCreate) (_Out_ PUNKNOWN*, _In_opt_ PUNKNOWN, _In_ POOL_TYPE),
    _In_opt_ PUNKNOWN             UnknownAdapter,
    _In_opt_ PRESOURCELIST        ResourceList,
    _Out_    PUNKNOWN*            OutPortUnknown)
{
    if (OutPortUnknown == nullptr)
        return STATUS_INVALID_PARAMETER;
    *OutPortUnknown = nullptr;

    PPORT    port     = nullptr;
    PUNKNOWN miniport = nullptr;

    NTSTATUS status = PcNewPort (&port, PortClassId);
    if (NT_SUCCESS (status))
    {
        status = MiniportCreate (&miniport, nullptr, NonPagedPoolNx);
    }

    if (NT_SUCCESS (status))
    {
        status = port->Init (DeviceObject, Irp, miniport, UnknownAdapter, ResourceList);
    }

    if (NT_SUCCESS (status))
    {
        status = PcRegisterSubdevice (DeviceObject, Name, port);
    }

    if (NT_SUCCESS (status))
    {
        port->QueryInterface (IID_IUnknown, reinterpret_cast<PVOID*> (OutPortUnknown));
    }

    if (miniport != nullptr)
        miniport->Release();
    if (port != nullptr)
        port->Release();

    return status;
}

extern "C" NTSTATUS VocalGzzioStartDevice (
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp,
    _In_ PRESOURCELIST  ResourceList)
{
    PUNKNOWN                  unknownCommon  = nullptr;
    PVOCALGZZIOADAPTERCOMMON  adapterCommon  = nullptr;
    PUNKNOWN                  unknownTopo    = nullptr;
    PUNKNOWN                  unknownWave    = nullptr;

    NTSTATUS status = CreateVocalGzzioAdapterCommon (&unknownCommon, nullptr, NonPagedPoolNx);
    if (NT_SUCCESS (status))
    {
        status = unknownCommon->QueryInterface (
            IID_IVocalGzzioAdapterCommon,
            reinterpret_cast<PVOID*> (&adapterCommon));
    }

    if (NT_SUCCESS (status))
    {
        status = adapterCommon->Init (DeviceObject);
    }

    // 1. Topology サブデバイス ("Topology") の登録
    if (NT_SUCCESS (status))
    {
        status = InstallSubdevice (
            DeviceObject,
            Irp,
            const_cast<PWSTR> (L"Topology"),
            CLSID_PortTopology,
            CreateVocalGzzioMiniportTopology,
            unknownCommon,
            ResourceList,
            &unknownTopo);
    }

    // 2. WaveRT サブデバイス ("Wave") の登録
    if (NT_SUCCESS (status))
    {
        status = InstallSubdevice (
            DeviceObject,
            Irp,
            const_cast<PWSTR> (L"Wave"),
            CLSID_PortWaveRT,
            CreateVocalGzzioMiniportWaveRT,
            unknownCommon,
            ResourceList,
            &unknownWave);
    }

    // 3. 再生経路: WaveRT (kWavePinRenderBridge) -> Topology (kTopoPinRenderIn)
    if (NT_SUCCESS (status))
    {
        status = PcRegisterPhysicalConnection (
            DeviceObject,
            unknownWave,
            kWavePinRenderBridge,
            unknownTopo,
            kTopoPinRenderIn);
    }

    // 4. 録音経路: Topology (kTopoPinCaptureOut) -> WaveRT (kWavePinCaptureBridge)
    if (NT_SUCCESS (status))
    {
        status = PcRegisterPhysicalConnection (
            DeviceObject,
            unknownTopo,
            kTopoPinCaptureOut,
            unknownWave,
            kWavePinCaptureBridge);
    }

    if (unknownWave != nullptr)
        unknownWave->Release();
    if (unknownTopo != nullptr)
        unknownTopo->Release();
    if (adapterCommon != nullptr)
        adapterCommon->Release();
    if (unknownCommon != nullptr)
        unknownCommon->Release();

    return status;
}

extern "C" NTSTATUS VocalGzzioAddDevice (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    return PcAddAdapterDevice (
        DriverObject,
        PhysicalDeviceObject,
        VocalGzzioStartDevice,
        VOCALGZZIO_MAX_MINIPORTS,
        0);
}

extern "C" DRIVER_INITIALIZE DriverEntry;
extern "C" NTSTATUS DriverEntry (
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPathName)
{
    // Windows 11 HVCI (コア分離 / メモリ整合性) の NX プールランタイムを有効化
    ExInitializeDriverRuntime (DrvRtPoolNxOptIn);

    return PcInitializeAdapterDriver (
        DriverObject,
        RegistryPathName,
        reinterpret_cast<PDRIVER_ADD_DEVICE> (VocalGzzioAddDevice));
}
