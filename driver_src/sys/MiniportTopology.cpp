// ============================================================================
// MiniportTopology.cpp
// VocalGzzioAudio.sys: Topology ミニポート実装
// Windows AudioEndpointBuilder に以下の再生・録音エンドポイントを公開する:
//   ・再生: ライン (VocalGzzio Driver (WDM))  [KSNODETYPE_LINE_CONNECTOR]
//   ・録音: ライン (VocalGzzio Driver (WDM))  [KSNODETYPE_LINE_CONNECTOR]
// また KSPROPERTY_JACK_DESCRIPTION で常時接続 (IsConnected = TRUE) を応答する。
// ============================================================================
#include "VocalGzzioAudio.h"

// ----------------------------------------------------------------------------
// アナログブリッジピン用データレンジ
// ----------------------------------------------------------------------------
static const KSDATARANGE g_TopoBridgeDataRange =
{
    sizeof (KSDATARANGE),
    0,
    0,
    0,
    STATICGUIDOF (KSDATAFORMAT_TYPE_AUDIO),
    STATICGUIDOF (KSDATAFORMAT_SUBTYPE_ANALOG),
    STATICGUIDOF (KSDATAFORMAT_SPECIFIER_NONE)
};

static const PKSDATARANGE g_TopoPinDataRangePointers[] =
{
    const_cast<PKSDATARANGE> (&g_TopoBridgeDataRange)
};

// ----------------------------------------------------------------------------
// KSPROPERTY_JACK_DESCRIPTION ハンドラ (常にプラグ接続済み ACTIVE として通知)
// ----------------------------------------------------------------------------
static NTSTATUS PropertyHandler_TopoFilter (_In_ PPCPROPERTY_REQUEST PropertyRequest)
{
    if (PropertyRequest == nullptr || PropertyRequest->PropertyItem == nullptr)
        return STATUS_INVALID_PARAMETER;

    if (PropertyRequest->PropertyItem->Id != KSPROPERTY_JACK_DESCRIPTION)
        return STATUS_INVALID_DEVICE_REQUEST;

    // PortCls は KSP_PIN から先頭の KSPROPERTY を除いた { ULONG PinId; ULONG Reserved; } を Instance に渡す
    if (PropertyRequest->Instance == nullptr || PropertyRequest->InstanceSize < sizeof (ULONG))
        return STATUS_INVALID_PARAMETER;

    const ULONG pinId = *reinterpret_cast<const ULONG*> (PropertyRequest->Instance);
    if (pinId != kTopoPinLineOut && pinId != kTopoPinLineIn)
        return STATUS_INVALID_PARAMETER;

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        const ULONG flags = KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT;
        if (PropertyRequest->ValueSize == 0)
        {
            PropertyRequest->ValueSize = sizeof (KSPROPERTY_DESCRIPTION);
            return STATUS_BUFFER_OVERFLOW;
        }
        if (PropertyRequest->ValueSize < sizeof (ULONG) || PropertyRequest->Value == nullptr)
            return STATUS_BUFFER_TOO_SMALL;

        if (PropertyRequest->ValueSize >= sizeof (KSPROPERTY_DESCRIPTION))
        {
            auto* desc = reinterpret_cast<PKSPROPERTY_DESCRIPTION> (PropertyRequest->Value);
            RtlZeroMemory (desc, sizeof (KSPROPERTY_DESCRIPTION));
            desc->AccessFlags      = flags;
            desc->DescriptionSize  = sizeof (KSPROPERTY_DESCRIPTION);
            desc->PropTypeSet.Set  = KSPROPTYPESETID_General;
            desc->PropTypeSet.Id   = 0;
            desc->MembersListCount = 0;
            PropertyRequest->ValueSize = sizeof (KSPROPERTY_DESCRIPTION);
        }
        else
        {
            *reinterpret_cast<PULONG> (PropertyRequest->Value) = flags;
            PropertyRequest->ValueSize = sizeof (ULONG);
        }
        return STATUS_SUCCESS;
    }

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        const ULONG cbNeeded = static_cast<ULONG> (sizeof (KSMULTIPLE_ITEM) + sizeof (KSJACK_DESCRIPTION));
        if (PropertyRequest->ValueSize == 0)
        {
            PropertyRequest->ValueSize = cbNeeded;
            return STATUS_BUFFER_OVERFLOW;
        }
        if (PropertyRequest->ValueSize < cbNeeded || PropertyRequest->Value == nullptr)
            return STATUS_BUFFER_TOO_SMALL;

        auto* multi = reinterpret_cast<PKSMULTIPLE_ITEM> (PropertyRequest->Value);
        multi->Size  = cbNeeded;
        multi->Count = 1;

        auto* jack = reinterpret_cast<PKSJACK_DESCRIPTION> (multi + 1);
        RtlZeroMemory (jack, sizeof (KSJACK_DESCRIPTION));
        jack->ChannelMapping = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        jack->Color          = 0x00000000;
        jack->ConnectionType = eConnTypeOtherAnalog;
        jack->GeoLocation    = eGeoLocNotApplicable;
        jack->GenLocation    = eGenLocInternal;
        jack->PortConnection = ePortConnIntegratedDevice;
        jack->IsConnected    = TRUE;

        PropertyRequest->ValueSize = cbNeeded;
        return STATUS_SUCCESS;
    }

    return STATUS_INVALID_DEVICE_REQUEST;
}

static const PCPROPERTY_ITEM g_TopoFilterProperties[] =
{
    {
        &KSPROPSETID_Jack,
        KSPROPERTY_JACK_DESCRIPTION,
        PCPROPERTY_ITEM_FLAG_GET | PCPROPERTY_ITEM_FLAG_BASICSUPPORT,
        PropertyHandler_TopoFilter
    }
};

DEFINE_PCAUTOMATION_TABLE_PROP (g_TopoFilterAutomation, g_TopoFilterProperties);

// ----------------------------------------------------------------------------
// Topology ピン・接続・フィルタ記述子テーブル
// ----------------------------------------------------------------------------
static const PCPIN_DESCRIPTOR g_TopoMiniportPins[kTopoPinCount] =
{
    // [0] kTopoPinRenderIn: WaveRT 再生ブリッジからの入力
    {
        0, 0, 0,
        nullptr,
        {
            0, nullptr,
            0, nullptr,
            SIZEOF_ARRAY (g_TopoPinDataRangePointers),
            const_cast<PKSDATARANGE*> (g_TopoPinDataRangePointers),
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            nullptr,
            0
        }
    },
    // [1] kTopoPinLineOut: Windows 再生エンドポイント「ライン (VocalGzzio Driver (WDM))」
    {
        0, 0, 0,
        nullptr,
        {
            0, nullptr,
            0, nullptr,
            SIZEOF_ARRAY (g_TopoPinDataRangePointers),
            const_cast<PKSDATARANGE*> (g_TopoPinDataRangePointers),
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSNODETYPE_LINE_CONNECTOR,
            nullptr,
            0
        }
    },
    // [2] kTopoPinLineIn: Windows 録音エンドポイント「ライン (VocalGzzio Driver (WDM))」
    {
        0, 0, 0,
        nullptr,
        {
            0, nullptr,
            0, nullptr,
            SIZEOF_ARRAY (g_TopoPinDataRangePointers),
            const_cast<PKSDATARANGE*> (g_TopoPinDataRangePointers),
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_NONE,
            &KSNODETYPE_LINE_CONNECTOR,
            nullptr,
            0
        }
    },
    // [3] kTopoPinCaptureOut: WaveRT 録音ブリッジへの出力
    {
        0, 0, 0,
        nullptr,
        {
            0, nullptr,
            0, nullptr,
            SIZEOF_ARRAY (g_TopoPinDataRangePointers),
            const_cast<PKSDATARANGE*> (g_TopoPinDataRangePointers),
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            nullptr,
            0
        }
    }
};

static const PCCONNECTION_DESCRIPTOR g_TopoMiniportConnections[] =
{
    // 再生経路: WaveRT ブリッジ入力 -> ライン出力コネクタ
    { PCFILTER_NODE, kTopoPinRenderIn,  PCFILTER_NODE, kTopoPinLineOut    },
    // 録音経路: ライン入力コネクタ -> WaveRT ブリッジ出力
    { PCFILTER_NODE, kTopoPinLineIn,    PCFILTER_NODE, kTopoPinCaptureOut }
};

static const GUID g_TopoCategories[] =
{
    STATICGUIDOF (KSCATEGORY_AUDIO),
    STATICGUIDOF (KSCATEGORY_TOPOLOGY)
};

static const PCFILTER_DESCRIPTOR g_TopoMiniportFilterDescriptor =
{
    0,
    &g_TopoFilterAutomation,
    sizeof (PCPIN_DESCRIPTOR),
    SIZEOF_ARRAY (g_TopoMiniportPins),
    g_TopoMiniportPins,
    sizeof (PCNODE_DESCRIPTOR),
    0,
    nullptr,
    SIZEOF_ARRAY (g_TopoMiniportConnections),
    g_TopoMiniportConnections,
    SIZEOF_ARRAY (g_TopoCategories),
    g_TopoCategories
};

// ============================================================================
// CVocalGzzioMiniportTopology クラス
// ============================================================================
class CVocalGzzioMiniportTopology : public IMiniportTopology,
                                    public CUnknown
{
private:
    PVOCALGZZIOADAPTERCOMMON m_AdapterCommon;

public:
    DECLARE_STD_UNKNOWN()

    explicit CVocalGzzioMiniportTopology (PUNKNOWN pUnknownOuter)
        : CUnknown (pUnknownOuter),
          m_AdapterCommon (nullptr)
    {
    }

    ~CVocalGzzioMiniportTopology() override
    {
        if (m_AdapterCommon != nullptr)
        {
            m_AdapterCommon->Release();
            m_AdapterCommon = nullptr;
        }
    }

    IMP_IMiniportTopology;
};

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportTopology::NonDelegatingQueryInterface (
    _In_         REFIID riid,
    _COM_Outptr_ PVOID* ppvObject)
{
    if (ppvObject == nullptr)
        return STATUS_INVALID_PARAMETER;

    if (IsEqualGUIDAligned (riid, IID_IUnknown))
    {
        *ppvObject = reinterpret_cast<PVOID> (static_cast<PUNKNOWN> (static_cast<PMINIPORTTOPOLOGY> (this)));
    }
    else if (IsEqualGUIDAligned (riid, IID_IMiniport) ||
             IsEqualGUIDAligned (riid, IID_IMiniportTopology))
    {
        *ppvObject = reinterpret_cast<PVOID> (static_cast<PMINIPORTTOPOLOGY> (this));
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
CVocalGzzioMiniportTopology::Init (
    _In_ PUNKNOWN      UnknownAdapter,
    _In_ PRESOURCELIST ResourceList,
    _In_ PPORTTOPOLOGY Port)
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
CVocalGzzioMiniportTopology::GetDescription (
    _Out_ PPCFILTER_DESCRIPTOR* Description)
{
    if (Description == nullptr)
        return STATUS_INVALID_PARAMETER;

    *Description = const_cast<PPCFILTER_DESCRIPTOR> (&g_TopoMiniportFilterDescriptor);
    return STATUS_SUCCESS;
}

STDMETHODIMP_ (NTSTATUS)
CVocalGzzioMiniportTopology::DataRangeIntersection (
    _In_  ULONG        PinId,
    _In_  PKSDATARANGE DataRange,
    _In_  PKSDATARANGE MatchingDataRange,
    _In_  ULONG        OutputBufferLength,
    _Out_writes_bytes_to_opt_ (OutputBufferLength, *ResultantFormatLength)
          PVOID        ResultantFormat,
    _Out_ PULONG       ResultantFormatLength)
{
    UNREFERENCED_PARAMETER (PinId);
    UNREFERENCED_PARAMETER (DataRange);
    UNREFERENCED_PARAMETER (MatchingDataRange);
    UNREFERENCED_PARAMETER (OutputBufferLength);
    UNREFERENCED_PARAMETER (ResultantFormat);
    UNREFERENCED_PARAMETER (ResultantFormatLength);

    // PortCls 既定のデータレンジ交差処理へ委譲する
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS CreateVocalGzzioMiniportTopology (
    _Out_    PUNKNOWN* ppUnknown,
    _In_opt_ PUNKNOWN  pUnknownOuter,
    _In_     POOL_TYPE poolType)
{
    if (ppUnknown == nullptr)
        return STATUS_INVALID_PARAMETER;

    STD_CREATE_BODY_WITH_TAG_ (
        CVocalGzzioMiniportTopology,
        ppUnknown,
        pUnknownOuter,
        poolType,
        VOCALGZZIO_POOL_TAG,
        PMINIPORTTOPOLOGY);
}
