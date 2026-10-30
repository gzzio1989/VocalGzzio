// ============================================================================
// VocalGzzioAudio.h
// 100%自社実装 Windows 10 / 11 (x64 & ARM64) 仮想オーディオドライバ
// PortCls + WaveRT + HVCI (コア分離・メモリ整合性) 完全準拠ヘッダ
// ============================================================================
#pragma once

// Windows 11 HVCI (メモリ整合性) 対応: すべての動的プール確保を Non-Executable (NX) に強制
#ifndef POOL_NX_OPTIN
#define POOL_NX_OPTIN 1
#endif

// stdunk.h のインライン operator new 警告 (C4595) を回避し、NX プール版を自前提供する
#define _NEW_DELETE_OPERATORS_

#include <portcls.h>
#include <stdunk.h>
#include <ksdebug.h>

#include "../include/VocalGzzioLoopbackCore.h"

// メモリプール識別タグ ('VGgz' in little-endian)
#define VOCALGZZIO_POOL_TAG          'zgGV'
#define VOCALGZZIO_MAX_MINIPORTS     2

// NX プールを使用する非インライン operator new / delete (Adapter.cpp で定義)
PVOID operator new (size_t iSize, POOL_TYPE poolType) noexcept;
PVOID operator new (size_t iSize, POOL_TYPE poolType, ULONG tag) noexcept;
void  operator delete (PVOID pVoid) noexcept;
void  operator delete (PVOID pVoid, size_t cbSize) noexcept;

// ----------------------------------------------------------------------------
// WaveRT ミニポートのピン番号定義
// ----------------------------------------------------------------------------
enum VocalGzzioWavePin : ULONG
{
    kWavePinRenderSink    = 0, // Windows Audio Engine (audiodg.exe) からの再生ストリーム入力
    kWavePinRenderBridge  = 1, // Topology フィルタへのブリッジ出力
    kWavePinCaptureBridge = 2, // Topology フィルタからのブリッジ入力
    kWavePinCaptureSource = 3, // Windows Audio Engine (audiodg.exe) への録音ストリーム出力
    kWavePinCount         = 4
};

// ----------------------------------------------------------------------------
// WaveRT ミニポートの内部ノード番号定義
// ----------------------------------------------------------------------------
enum VocalGzzioWaveNode : ULONG
{
    kWaveNodeDac   = 0,
    kWaveNodeAdc   = 1,
    kWaveNodeCount = 2
};

// ----------------------------------------------------------------------------
// Topology ミニポートのピン番号定義
// ----------------------------------------------------------------------------
enum VocalGzzioTopoPin : ULONG
{
    kTopoPinRenderIn   = 0, // WaveRT (kWavePinRenderBridge) から入るブリッジピン
    kTopoPinLineOut    = 1, // 再生エンドポイント: ライン (VocalGzzio Driver (WDM))
    kTopoPinLineIn     = 2, // 録音エンドポイント: ライン (VocalGzzio Driver (WDM))
    kTopoPinCaptureOut = 3, // WaveRT (kWavePinCaptureBridge) へ出るブリッジピン
    kTopoPinCount      = 4
};

// ----------------------------------------------------------------------------
// アダプタ共通オブジェクト (Render <-> Capture 間の共有ループバックバス保持)
// ----------------------------------------------------------------------------
// {9E4E7F21-73A8-4E2A-9B0D-6C21A9155401}
DEFINE_GUID (IID_IVocalGzzioAdapterCommon,
    0x9e4e7f21, 0x73a8, 0x4e2a, 0x9b, 0x0d, 0x6c, 0x21, 0xa9, 0x15, 0x54, 0x01);

DECLARE_INTERFACE_ (IVocalGzzioAdapterCommon, IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()

    STDMETHOD_ (NTSTATUS, Init) (THIS_ _In_ PDEVICE_OBJECT DeviceObject) PURE;

    STDMETHOD_ (VOID, PushRenderFrames) (
        THIS_
        _In_reads_bytes_ (CyclicBytes) const uint8_t* CyclicBuffer,
        _In_ uint32_t                                 CyclicBytes,
        _In_ uint32_t                                 ByteOffset,
        _In_ uint32_t                                 FrameCount,
        _In_ const vgaudio::StreamFormatInfo*         Format
    ) PURE;

    STDMETHOD_ (VOID, PopCaptureFrames) (
        THIS_
        _Out_writes_bytes_ (CyclicBytes) uint8_t*     CyclicBuffer,
        _In_ uint32_t                                 CyclicBytes,
        _In_ uint32_t                                 ByteOffset,
        _In_ uint32_t                                 FrameCount,
        _In_ const vgaudio::StreamFormatInfo*         Format
    ) PURE;

    STDMETHOD_ (VOID, SetRenderActive) (
        THIS_
        _In_ BOOLEAN  Active,
        _In_ uint32_t SampleRate
    ) PURE;
};

typedef IVocalGzzioAdapterCommon* PVOCALGZZIOADAPTERCOMMON;

// ファクトリ関数
NTSTATUS CreateVocalGzzioAdapterCommon (
    _Out_    PUNKNOWN* ppUnknown,
    _In_opt_ PUNKNOWN  pUnknownOuter,
    _In_     POOL_TYPE poolType
);

NTSTATUS CreateVocalGzzioMiniportTopology (
    _Out_    PUNKNOWN* ppUnknown,
    _In_opt_ PUNKNOWN  pUnknownOuter,
    _In_     POOL_TYPE poolType
);

NTSTATUS CreateVocalGzzioMiniportWaveRT (
    _Out_    PUNKNOWN* ppUnknown,
    _In_opt_ PUNKNOWN  pUnknownOuter,
    _In_     POOL_TYPE poolType
);
