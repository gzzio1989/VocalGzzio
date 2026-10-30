// ============================================================================
// VocalGzzio Virtual WDM Audio Driver Installer (VocalGzzioWdmInstaller.exe)
// Windows の「サウンド (再生 / 録音)」コントロールパネルおよび
// OBS / Discord / VRChat / cluster / ツイキャス / SYNCROOM のマイク・スピーカー一覧に
// 『ライン (VocalGzzio Driver (WDM))』を追加登録・管理するネイティブC++ツール
// ============================================================================

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <cfgmgr32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <softpub.h>
#include <wintrust.h>
#include <stdio.h>
#include <wchar.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "newdev.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wintrust.lib")

// MEDIA クラス GUID: {4d36e96c-e325-11ce-bfc1-08002be10318}
static const GUID GUID_DEVCLASS_MEDIA_CUSTOM = {
    0x4d36e96c, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 }
};

// PKEY_Device_DeviceDesc: {a45c254e-df1c-4efd-8020-67d146a850e0}, 2
static const PROPERTYKEY PKEY_Endpoint_Desc = {
    { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 2
};

// PKEY_AudioEndpoint_Adapter_FriendlyName: {b3f8fa53-0004-438e-9003-51a46e139bfc}, 6
static const PROPERTYKEY PKEY_Adapter_Name = {
    { 0xb3f8fa53, 0x0004, 0x438e, { 0x90, 0x03, 0x51, 0xa4, 0x6e, 0x13, 0x9b, 0xfc } }, 6
};

// PKEY_AudioEndpoint_ParentInstance: {b3f8fa53-0004-438e-9003-51a46e139bfc}, 2
static const PROPERTYKEY PKEY_Adapter_Parent = {
    { 0xb3f8fa53, 0x0004, 0x438e, { 0x90, 0x03, 0x51, 0xa4, 0x6e, 0x13, 0x9b, 0xfc } }, 2
};

// PKEY_Device_FriendlyName: {a45c254e-df1c-4efd-8020-67d146a850e0}, 14
static const PROPERTYKEY PKEY_Endpoint_FriendlyName = {
    { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14
};

// IPolicyConfig (Windows 7/10/11 既定オーディオエンドポイント設定用 COM インターフェース)
static const CLSID CLSID_PolicyConfigClient_Custom = {
    0x870af99c, 0x171d, 0x4f9e, { 0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9 }
};
static const IID IID_IPolicyConfig_Custom = {
    0xf8679f50, 0x850a, 0x41cf, { 0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8 }
};

MIDL_INTERFACE("f8679f50-850a-41cf-9c72-430f290290c8")
IPolicyConfigCustom : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, BOOL, const void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, BOOL, const void*, const void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR wszDeviceId, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

static const wchar_t* VOCALGZZIO_WDM_ADAPTER_NAME = L"VocalGzzio Driver (WDM)";
static const wchar_t* VOCALGZZIO_WDM_LINE_NAME    = L"ライン";
static const wchar_t* VOCALGZZIO_WDM_FULL_NAME    = L"ライン (VocalGzzio Driver (WDM))";
static const wchar_t* VOCALGZZIO_NATIVE_HWID      = L"ROOT\\VocalGzzioAudio";
static const wchar_t* VOCALGZZIO_LEGACY_HWID      = L"*SYNCROOMSimple";

// カタログファイル (.cat) に信頼済みの Microsoft / Authenticode 署名が付与されているか検証する
static bool IsAuthenticodeSignatureTrusted(const wchar_t* filePath) {
    if (!filePath || GetFileAttributesW(filePath) == INVALID_FILE_ATTRIBUTES)
        return false;

    WINTRUST_FILE_INFO fileInfo;
    ZeroMemory(&fileInfo, sizeof(fileInfo));
    fileInfo.cbStruct       = sizeof(WINTRUST_FILE_INFO);
    fileInfo.pcwszFilePath  = filePath;

    WINTRUST_DATA wtd;
    ZeroMemory(&wtd, sizeof(wtd));
    wtd.cbStruct            = sizeof(WINTRUST_DATA);
    wtd.dwUIChoice          = WTD_UI_NONE;
    wtd.fdwRevocationChecks = WTD_REVOKE_NONE;
    wtd.dwUnionChoice       = WTD_CHOICE_FILE;
    wtd.pFile               = &fileInfo;
    wtd.dwStateAction       = WTD_STATEACTION_VERIFY;
    wtd.dwProvFlags         = WTD_CACHE_ONLY_URL_RETRIEVAL;

    GUID policyGuid = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status = WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &policyGuid, &wtd);

    wtd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &policyGuid, &wtd);
    return (status == ERROR_SUCCESS);
}

// .sys バイナリの PE ヘッダから対象アーキテクチャ (0x8664 = x64, 0xAA64 = ARM64) を読み取る
static WORD GetPeMachineArchitecture(const wchar_t* sysPath) {
    HANDLE hFile = CreateFileW(sysPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return 0;

    IMAGE_DOS_HEADER dosHdr;
    ZeroMemory(&dosHdr, sizeof(dosHdr));
    DWORD cbRead = 0;
    WORD machine = 0;
    if (ReadFile(hFile, &dosHdr, sizeof(dosHdr), &cbRead, NULL) && cbRead == sizeof(dosHdr) && dosHdr.e_magic == IMAGE_DOS_SIGNATURE) {
        if (SetFilePointer(hFile, dosHdr.e_lfanew, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER) {
            DWORD peSig = 0;
            IMAGE_FILE_HEADER fileHdr;
            ZeroMemory(&fileHdr, sizeof(fileHdr));
            if (ReadFile(hFile, &peSig, sizeof(peSig), &cbRead, NULL) && cbRead == sizeof(peSig) && peSig == IMAGE_NT_SIGNATURE) {
                if (ReadFile(hFile, &fileHdr, sizeof(fileHdr), &cbRead, NULL) && cbRead == sizeof(fileHdr)) {
                    machine = fileHdr.Machine;
                }
            }
        }
    }
    CloseHandle(hFile);
    return machine;
}

// ARM64 版 Windows 上で実行されているかを判定する。
// x64 カーネルドライバ (.sys) は ARM64 カーネルにロードできないため、安全にスキップするために使用する。
typedef BOOL (WINAPI *PFN_IsWow64Process2)(HANDLE, USHORT*, USHORT*);
static bool IsRunningOnArm64Windows() {
#if defined(_M_ARM64) || defined(__aarch64__)
    return true;
#else
    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    if (hKernel != NULL) {
        PFN_IsWow64Process2 pfnWow2 = (PFN_IsWow64Process2)GetProcAddress(hKernel, "IsWow64Process2");
        if (pfnWow2 != NULL) {
            USHORT processMachine = 0;
            USHORT nativeMachine = 0;
            if (pfnWow2(GetCurrentProcess(), &processMachine, &nativeMachine)) {
                // IMAGE_FILE_MACHINE_ARM64 (0xAA64) / IMAGE_FILE_MACHINE_ARMNT (0x01C4)
                if (nativeMachine == 0xAA64 || nativeMachine == 0x01C4)
                    return true;
            }
        }
    }
    SYSTEM_INFO si;
    ZeroMemory(&si, sizeof(si));
    GetNativeSystemInfo(&si);
    // PROCESSOR_ARCHITECTURE_ARM64 (12) / PROCESSOR_ARCHITECTURE_ARM (5)
    return (si.wProcessorArchitecture == 12 || si.wProcessorArchitecture == 5);
#endif
}

static bool IsVirtualCableNameW(const wchar_t* name) {
    if (!name) return false;
    return wcsstr(name, L"VocalGzzio") != NULL ||
           wcsstr(name, L"GzzioRoom") != NULL ||
           wcsstr(name, L"SYNCROOM") != NULL ||
           wcsstr(name, L"VB-Audio") != NULL ||
           wcsstr(name, L"CABLE") != NULL;
}

struct SavedDefaultEndpoints {
    wchar_t renderIds[3][256];
    wchar_t captureIds[3][256];
};

// 仮想ドライバ登録前に「本来のスピーカー・マイク（既定のデバイス）」を控え、
// もし既に仮想ドライバに奪われていた場合は実ハードウェア（Roland Rubix / Realtek 等）を探す
static void SnapshotRealDefaultEndpoints(SavedDefaultEndpoints* out) {
    ZeroMemory(out, sizeof(SavedDefaultEndpoints));
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* pEnum = NULL;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnum)) || !pEnum)
        return;

    ERole roles[3] = { eConsole, eMultimedia, eCommunications };
    EDataFlow flows[2] = { eRender, eCapture };
    for (int f = 0; f < 2; ++f) {
        wchar_t fallbackRealId[256] = {0};
        IMMDeviceCollection* pCol = NULL;
        if (SUCCEEDED(pEnum->EnumAudioEndpoints(flows[f], DEVICE_STATE_ACTIVE, &pCol)) && pCol) {
            UINT count = 0;
            pCol->GetCount(&count);
            for (UINT i = 0; i < count; ++i) {
                IMMDevice* pDev = NULL;
                if (SUCCEEDED(pCol->Item(i, &pDev)) && pDev) {
                    IPropertyStore* pStore = NULL;
                    wchar_t friendly[256] = {0};
                    if (SUCCEEDED(pDev->OpenPropertyStore(STGM_READ, &pStore)) && pStore) {
                        PROPVARIANT pv;
                        PropVariantInit(&pv);
                        if (SUCCEEDED(pStore->GetValue(PKEY_Endpoint_FriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal)
                            wcscpy_s(friendly, pv.pwszVal);
                        PropVariantClear(&pv);
                        pStore->Release();
                    }
                    if (!IsVirtualCableNameW(friendly)) {
                        LPWSTR idStr = NULL;
                        if (SUCCEEDED(pDev->GetId(&idStr)) && idStr) {
                            if (fallbackRealId[0] == 0 || wcsstr(friendly, L"Rubix") != NULL || wcsstr(friendly, L"Steinberg") != NULL || wcsstr(friendly, L"AG0") != NULL)
                                wcscpy_s(fallbackRealId, idStr);
                            CoTaskMemFree(idStr);
                        }
                    }
                    pDev->Release();
                }
            }
            pCol->Release();
        }

        for (int r = 0; r < 3; ++r) {
            wchar_t* dst = (f == 0) ? out->renderIds[r] : out->captureIds[r];
            IMMDevice* pDef = NULL;
            if (SUCCEEDED(pEnum->GetDefaultAudioEndpoint(flows[f], roles[r], &pDef)) && pDef) {
                IPropertyStore* pStore = NULL;
                wchar_t friendly[256] = {0};
                if (SUCCEEDED(pDef->OpenPropertyStore(STGM_READ, &pStore)) && pStore) {
                    PROPVARIANT pv;
                    PropVariantInit(&pv);
                    if (SUCCEEDED(pStore->GetValue(PKEY_Endpoint_FriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal)
                        wcscpy_s(friendly, pv.pwszVal);
                    PropVariantClear(&pv);
                    pStore->Release();
                }
                LPWSTR idStr = NULL;
                if (!IsVirtualCableNameW(friendly) && SUCCEEDED(pDef->GetId(&idStr)) && idStr) {
                    wcscpy_s(dst, 256, idStr);
                    CoTaskMemFree(idStr);
                } else if (fallbackRealId[0] != 0) {
                    wcscpy_s(dst, 256, fallbackRealId);
                }
                pDef->Release();
            } else if (fallbackRealId[0] != 0) {
                wcscpy_s(dst, 256, fallbackRealId);
            }
        }
    }
    pEnum->Release();
}

static void RestoreRealDefaultEndpoints(const SavedDefaultEndpoints* saved) {
    if (!saved) return;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IPolicyConfigCustom* pPolicy = NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_PolicyConfigClient_Custom, NULL, CLSCTX_ALL, IID_IPolicyConfig_Custom, (void**)&pPolicy)) && pPolicy) {
        ERole roles[3] = { eConsole, eMultimedia, eCommunications };
        for (int r = 0; r < 3; ++r) {
            if (saved->renderIds[r][0] != 0)
                pPolicy->SetDefaultEndpoint(saved->renderIds[r], roles[r]);
            if (saved->captureIds[r][0] != 0)
                pPolicy->SetDefaultEndpoint(saved->captureIds[r], roles[r]);
        }
        pPolicy->Release();
    }
    // Windowsの「通信アクティビティ検出時に他の音量を80%下げる」を「何もしない(3)」に設定し、cluster等の音量低下を防ぐ
    HKEY hAudioKey = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Multimedia\\Audio", 0, NULL, 0, KEY_SET_VALUE, NULL, &hAudioKey, NULL) == ERROR_SUCCESS) {
        DWORD noDuck = 3;
        RegSetValueExW(hAudioKey, L"UserDuckingPreference", 0, REG_DWORD, (const BYTE*)&noDuck, sizeof(DWORD));
        RegCloseKey(hAudioKey);
    }
}

static void PrintUtf8(const wchar_t* wstr) {
    if (!wstr) return;
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len > 1) {
        char* buf = (char*)HeapAlloc(GetProcessHeap(), 0, len);
        if (buf) {
            WideCharToMultiByte(CP_UTF8, 0, wstr, -1, buf, len, NULL, NULL);
            fputs(buf, stdout);
            HeapFree(GetProcessHeap(), 0, buf);
        }
    }
}

// MMDevices レジストリと COM PropertyStore を走査し、指定親インスタンスIDのエンドポイント名を VocalGzzio Driver (WDM) に設定
static int RenameEndpointsForInstance(const wchar_t* parentInstanceId, bool enableEndpoints) {
    int updatedCount = 0;
    wchar_t expectedParent[256];
    swprintf_s(expectedParent, L"{1}.%s", parentInstanceId);

    // 1. レジストリ直接更新 (Render & Capture)
    const wchar_t* flows[2] = { L"Render", L"Capture" };
    for (int f = 0; f < 2; ++f) {
        wchar_t baseKeyPath[512];
        swprintf_s(baseKeyPath, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\%s", flows[f]);
        HKEY hFlowKey = NULL;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, baseKeyPath, 0, KEY_READ, &hFlowKey) == ERROR_SUCCESS) {
            wchar_t subKeyName[128];
            DWORD idx = 0;
            while (RegEnumKeyW(hFlowKey, idx++, subKeyName, 128) == ERROR_SUCCESS) {
                wchar_t propPath[640];
                swprintf_s(propPath, L"%s\\%s\\Properties", baseKeyPath, subKeyName);
                HKEY hPropKey = NULL;
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, propPath, 0, KEY_READ | KEY_SET_VALUE, &hPropKey) == ERROR_SUCCESS) {
                    wchar_t parentVal[256] = {0};
                    DWORD cb = sizeof(parentVal);
                    DWORD type = 0;
                    if (RegQueryValueExW(hPropKey, L"{b3f8fa53-0004-438e-9003-51a46e139bfc},2", NULL, &type, (LPBYTE)parentVal, &cb) == ERROR_SUCCESS) {
                        if (_wcsicmp(parentVal, expectedParent) == 0) {
                            RegSetValueExW(
                                hPropKey,
                                L"{b3f8fa53-0004-438e-9003-51a46e139bfc},6",
                                0,
                                REG_SZ,
                                (const BYTE*)VOCALGZZIO_WDM_ADAPTER_NAME,
                                (DWORD)((wcslen(VOCALGZZIO_WDM_ADAPTER_NAME) + 1) * sizeof(wchar_t))
                            );
                            RegSetValueExW(
                                hPropKey,
                                L"{a45c254e-df1c-4efd-8020-67d146a850e0},2",
                                0,
                                REG_SZ,
                                (const BYTE*)VOCALGZZIO_WDM_LINE_NAME,
                                (DWORD)((wcslen(VOCALGZZIO_WDM_LINE_NAME) + 1) * sizeof(wchar_t))
                            );
                            updatedCount++;

                            if (enableEndpoints) {
                                wchar_t epKeyPath[600];
                                swprintf_s(epKeyPath, L"%s\\%s", baseKeyPath, subKeyName);
                                HKEY hEpKey = NULL;
                                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, epKeyPath, 0, KEY_SET_VALUE, &hEpKey) == ERROR_SUCCESS) {
                                    DWORD activeState = 1; // DEVICE_STATE_ACTIVE
                                    RegSetValueExW(hEpKey, L"DeviceState", 0, REG_DWORD, (const BYTE*)&activeState, sizeof(DWORD));
                                    RegCloseKey(hEpKey);
                                }
                            }
                        }
                    }
                    RegCloseKey(hPropKey);
                }
            }
            RegCloseKey(hFlowKey);
        }
    }

    // 2. IMMDevice PropertyStore 経由でも更新して Audiosrv / mmsys.cpl にリアルタイム通知を送る
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* pEnum = NULL;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&pEnum
    );
    if (SUCCEEDED(hr) && pEnum) {
        IMMDeviceCollection* pCol = NULL;
        if (SUCCEEDED(pEnum->EnumAudioEndpoints(eAll, DEVICE_STATEMASK_ALL, &pCol)) && pCol) {
            UINT count = 0;
            pCol->GetCount(&count);
            for (UINT i = 0; i < count; ++i) {
                IMMDevice* pDev = NULL;
                if (SUCCEEDED(pCol->Item(i, &pDev)) && pDev) {
                    IPropertyStore* pStore = NULL;
                    if (SUCCEEDED(pDev->OpenPropertyStore(STGM_READWRITE, &pStore)) && pStore) {
                        PROPVARIANT pvParent;
                        PropVariantInit(&pvParent);
                        if (SUCCEEDED(pStore->GetValue(PKEY_Adapter_Parent, &pvParent)) && pvParent.vt == VT_LPWSTR && pvParent.pwszVal) {
                            if (_wcsicmp(pvParent.pwszVal, expectedParent) == 0) {
                                PROPVARIANT pvAdapter;
                                PropVariantInit(&pvAdapter);
                                pvAdapter.vt = VT_LPWSTR;
                                pvAdapter.pwszVal = (LPWSTR)VOCALGZZIO_WDM_ADAPTER_NAME;
                                pStore->SetValue(PKEY_Adapter_Name, pvAdapter);

                                PROPVARIANT pvDesc;
                                PropVariantInit(&pvDesc);
                                pvDesc.vt = VT_LPWSTR;
                                pvDesc.pwszVal = (LPWSTR)VOCALGZZIO_WDM_LINE_NAME;
                                pStore->SetValue(PKEY_Endpoint_Desc, pvDesc);

                                pStore->Commit();
                            }
                        }
                        PropVariantClear(&pvParent);
                        pStore->Release();
                    }
                    pDev->Release();
                }
            }
            pCol->Release();
        }
        pEnum->Release();
    }

    // 3. SWD\MMDEVAPI PnP デバイスノードの FriendlyName も更新
    HDEVINFO hSwd = SetupDiGetClassDevsW(NULL, L"SWD\\MMDEVAPI", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (hSwd != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA swdData;
        swdData.cbSize = sizeof(SP_DEVINFO_DATA);
        for (DWORD i = 0; SetupDiEnumDeviceInfo(hSwd, i, &swdData); ++i) {
            wchar_t instId[256] = {0};
            if (SetupDiGetDeviceInstanceIdW(hSwd, &swdData, instId, 256, NULL)) {
                const wchar_t* guidStart = wcsrchr(instId, L'.');
                if (guidStart && *(guidStart + 1) == L'{') {
                    const wchar_t* guidStr = guidStart + 1;
                    for (int f = 0; f < 2; ++f) {
                        wchar_t propPath[640];
                        swprintf_s(propPath, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\%s\\%s\\Properties", flows[f], guidStr);
                        HKEY hProp = NULL;
                        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, propPath, 0, KEY_READ, &hProp) == ERROR_SUCCESS) {
                            wchar_t parentVal[256] = {0};
                            DWORD cb = sizeof(parentVal);
                            if (RegQueryValueExW(hProp, L"{b3f8fa53-0004-438e-9003-51a46e139bfc},2", NULL, NULL, (LPBYTE)parentVal, &cb) == ERROR_SUCCESS) {
                                if (_wcsicmp(parentVal, expectedParent) == 0) {
                                    SetupDiSetDeviceRegistryPropertyW(
                                        hSwd,
                                        &swdData,
                                        SPDRP_FRIENDLYNAME,
                                        (const BYTE*)VOCALGZZIO_WDM_FULL_NAME,
                                        (DWORD)((wcslen(VOCALGZZIO_WDM_FULL_NAME) + 1) * sizeof(wchar_t))
                                    );
                                }
                            }
                            RegCloseKey(hProp);
                        }
                    }
                }
            }
        }
        SetupDiDestroyDeviceInfoList(hSwd);
    }

    return updatedCount;
}

// 現在のステータスを JSON 出力 (--status)
static int PrintStatusJson() {
    wchar_t renderGuid[128] = {0};
    wchar_t captureGuid[128] = {0};
    wchar_t parentInst[128] = {0};
    DWORD renderState = 0;
    DWORD captureState = 0;

    const wchar_t* flows[2] = { L"Render", L"Capture" };
    for (int f = 0; f < 2; ++f) {
        wchar_t baseKeyPath[512];
        swprintf_s(baseKeyPath, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\%s", flows[f]);
        HKEY hFlowKey = NULL;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, baseKeyPath, 0, KEY_READ, &hFlowKey) == ERROR_SUCCESS) {
            wchar_t subKeyName[128];
            DWORD idx = 0;
            while (RegEnumKeyW(hFlowKey, idx++, subKeyName, 128) == ERROR_SUCCESS) {
                wchar_t epPath[600];
                swprintf_s(epPath, L"%s\\%s", baseKeyPath, subKeyName);
                DWORD devState = 0;
                HKEY hEpKey = NULL;
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, epPath, 0, KEY_READ, &hEpKey) == ERROR_SUCCESS) {
                    DWORD cb = sizeof(DWORD);
                    RegQueryValueExW(hEpKey, L"DeviceState", NULL, NULL, (LPBYTE)&devState, &cb);
                    RegCloseKey(hEpKey);
                }

                wchar_t propPath[640];
                swprintf_s(propPath, L"%s\\Properties", epPath);
                HKEY hPropKey = NULL;
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, propPath, 0, KEY_READ, &hPropKey) == ERROR_SUCCESS) {
                    wchar_t adapterVal[256] = {0};
                    DWORD cb = sizeof(adapterVal);
                    if (RegQueryValueExW(hPropKey, L"{b3f8fa53-0004-438e-9003-51a46e139bfc},6", NULL, NULL, (LPBYTE)adapterVal, &cb) == ERROR_SUCCESS) {
                        if (_wcsicmp(adapterVal, VOCALGZZIO_WDM_ADAPTER_NAME) == 0 && devState == 1) {
                            if (f == 0) {
                                wcscpy_s(renderGuid, subKeyName);
                                renderState = devState;
                            } else {
                                wcscpy_s(captureGuid, subKeyName);
                                captureState = devState;
                            }
                            DWORD cbP = sizeof(parentInst);
                            RegQueryValueExW(hPropKey, L"{b3f8fa53-0004-438e-9003-51a46e139bfc},2", NULL, NULL, (LPBYTE)parentInst, &cbP);
                        }
                    }
                    RegCloseKey(hPropKey);
                }
            }
            RegCloseKey(hFlowKey);
        }
    }

    bool installed = (renderGuid[0] != 0 && captureGuid[0] != 0);
    bool skippedArm64 = (!installed && IsRunningOnArm64Windows());
    printf("{\"ok\":true,\"installed\":%s,\"skippedArm64\":%s,\"adapterName\":\"VocalGzzio Driver (WDM)\",\"fullName\":\"",
           installed ? "true" : "false",
           skippedArm64 ? "true" : "false");
    PrintUtf8(VOCALGZZIO_WDM_FULL_NAME);
    printf("\",\"renderGuid\":\"");
    PrintUtf8(renderGuid);
    printf("\",\"captureGuid\":\"");
    PrintUtf8(captureGuid);
    printf("\",\"parentInstance\":\"");
    PrintUtf8(parentInst);
    printf("\"}\n");
    return 0;
}

// 同梱 driver\ 内に Microsoft 署名済みの自社製 VocalGzzioAudio.{inf,cat,sys} が揃っており、
// かつ実行中の Windows カーネルアーキテクチャ (x64 / ARM64) と一致するかを判定する
static bool HasSignedNativeVocalGzzioDriver(wchar_t* outDriverDir, size_t dirMax, wchar_t* outInfPath, size_t infMax) {
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *lastSlash = 0;

    wchar_t drvDir[MAX_PATH] = {0};
    wchar_t infPath[MAX_PATH] = {0};
    wchar_t catPath[MAX_PATH] = {0};
    wchar_t sysPath[MAX_PATH] = {0};
    swprintf_s(drvDir,  L"%s\\driver", exePath);
    swprintf_s(infPath, L"%s\\driver\\VocalGzzioAudio.inf", exePath);
    swprintf_s(catPath, L"%s\\driver\\VocalGzzioAudio.cat", exePath);
    swprintf_s(sysPath, L"%s\\driver\\VocalGzzioAudio.sys", exePath);

    if (outDriverDir && dirMax > 0)
        wcscpy_s(outDriverDir, dirMax, drvDir);

    if (GetFileAttributesW(infPath) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(catPath) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(sysPath) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    const WORD expectedMachine = IsRunningOnArm64Windows() ? 0xAA64 : 0x8664;
    if (GetPeMachineArchitecture(sysPath) != expectedMachine)
        return false;

    if (!IsAuthenticodeSignatureTrusted(catPath))
        return false;

    if (outInfPath && infMax > 0)
        wcscpy_s(outInfPath, infMax, infPath);
    return true;
}

// 既存の VocalGzzio Driver (WDM) デバイスノード (ROOT\MEDIA\xxxx) を探し、重複・旧世代ノードを整理する。
// 注意: 自社ハードウェアID (ROOT\VocalGzzioAudio) または FriendlyName / DeviceDesc が
//       "VocalGzzio Driver (WDM)" と完全一致するノードのみを対象とするため、
//       Yamaha SYNCROOM Driver (WDM) や GzzioRoom Driver (WDM) はインスタンス番号に関わらず一切触らない。
static bool FindExistingVocalGzzioWdmNode(const wchar_t* desiredHwId, wchar_t* outInstanceId, size_t maxCount) {
    HDEVINFO hDevInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_MEDIA_CUSTOM, L"ROOT\\MEDIA", NULL, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return false;

    bool foundPrimary = false;
    SP_DEVINFO_DATA devData;
    devData.cbSize = sizeof(SP_DEVINFO_DATA);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devData); ++i) {
        wchar_t instId[256] = {0};
        if (!SetupDiGetDeviceInstanceIdW(hDevInfo, &devData, instId, 256, NULL)) continue;

        wchar_t hwId[256] = {0};
        if (!SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devData, SPDRP_HARDWAREID, NULL, (PBYTE)hwId, sizeof(hwId), NULL)) continue;

        const bool isNativeHw = (_wcsicmp(hwId, VOCALGZZIO_NATIVE_HWID) == 0);
        const bool isLegacyHw = (_wcsicmp(hwId, VOCALGZZIO_LEGACY_HWID) == 0);
        if (!isNativeHw && !isLegacyHw) continue;

        wchar_t friendly[256] = {0};
        wchar_t desc[256] = {0};
        SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devData, SPDRP_FRIENDLYNAME, NULL, (PBYTE)friendly, sizeof(friendly), NULL);
        SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devData, SPDRP_DEVICEDESC, NULL, (PBYTE)desc, sizeof(desc), NULL);

        const bool isOwnedByVocalGzzio = isNativeHw ||
                                         (_wcsicmp(friendly, VOCALGZZIO_WDM_ADAPTER_NAME) == 0) ||
                                         (_wcsicmp(desc, VOCALGZZIO_WDM_ADAPTER_NAME) == 0);
        if (!isOwnedByVocalGzzio) continue;

        if (_wcsicmp(hwId, desiredHwId) == 0 && !foundPrimary) {
            wcscpy_s(outInstanceId, maxCount, instId);
            foundPrimary = true;
        } else {
            // 重複生成された余分なノード、または自社製ドライバ移行時に不要となった旧世代ノードを自動整理する
            SetupDiCallClassInstaller(DIF_REMOVE, hDevInfo, &devData);
        }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return foundPrimary;
}

// VocalGzzio Driver (WDM) の新規作成・インストール
static int InstallVocalGzzioWdmDriver() {
    wchar_t bundledDriverDir[MAX_PATH] = {0};
    wchar_t bundledInfPath[MAX_PATH] = {0};
    const bool useNativeDriver = HasSignedNativeVocalGzzioDriver(bundledDriverDir, MAX_PATH, bundledInfPath, MAX_PATH);

    if (!useNativeDriver && IsRunningOnArm64Windows()) {
        printf("{\"ok\":true,\"installed\":false,\"skippedArm64\":true,\"adapterName\":\"VocalGzzio Driver (WDM)\"}\n");
        return 0;
    }

    SavedDefaultEndpoints savedDefaults;
    SnapshotRealDefaultEndpoints(&savedDefaults);

    wchar_t targetInstanceId[256] = {0};
    const wchar_t* activeHwId = useNativeDriver ? VOCALGZZIO_NATIVE_HWID : VOCALGZZIO_LEGACY_HWID;

    if (!useNativeDriver) {
        swprintf_s(bundledInfPath, L"%s\\syncroom.inf", bundledDriverDir);
    }

    // 同梱の署名済みドライバ (.inf + .cat + .sys) を DriverStore へ事前登録する
    wchar_t stagedOemInf[MAX_PATH] = L"C:\\Windows\\INF\\oem69.inf";
    if (GetFileAttributesW(bundledInfPath) != INVALID_FILE_ATTRIBUTES) {
        wcscpy_s(stagedOemInf, bundledInfPath);
        wchar_t destInf[MAX_PATH] = {0};
        if (SetupCopyOEMInfW(bundledInfPath, bundledDriverDir, SPOST_PATH, 0, destInf, MAX_PATH, NULL, NULL)) {
            if (destInf[0] != 0) {
                wcscpy_s(stagedOemInf, destInf);
            }
        }
    }

    if (!FindExistingVocalGzzioWdmNode(activeHwId, targetInstanceId, 256)) {
        // 新しい ROOT\MEDIA デバイスノードを作成し、署名済みドライバ (ROOT\VocalGzzioAudio またはフォールバック) をバインド
        HDEVINFO hDevInfo = SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_MEDIA_CUSTOM, NULL);
        if (hDevInfo == INVALID_HANDLE_VALUE) {
            printf("{\"ok\":false,\"error\":\"SetupDiCreateDeviceInfoList failed (%lu)\"}\n", GetLastError());
            return 1;
        }

        SP_DEVINFO_DATA devInfoData;
        ZeroMemory(&devInfoData, sizeof(devInfoData));
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        if (!SetupDiCreateDeviceInfoW(
                hDevInfo,
                L"MEDIA",
                &GUID_DEVCLASS_MEDIA_CUSTOM,
                VOCALGZZIO_WDM_ADAPTER_NAME,
                NULL,
                DICD_GENERATE_ID,
                &devInfoData)) {
            printf("{\"ok\":false,\"error\":\"SetupDiCreateDeviceInfoW failed (%lu)\"}\n", GetLastError());
            SetupDiDestroyDeviceInfoList(hDevInfo);
            return 1;
        }

        wchar_t hwIdMultiSz[64] = {0};
        wcscpy_s(hwIdMultiSz, activeHwId);
        DWORD hwIdBytes = (DWORD)((wcslen(activeHwId) + 2) * sizeof(wchar_t));
        if (!SetupDiSetDeviceRegistryPropertyW(
                hDevInfo,
                &devInfoData,
                SPDRP_HARDWAREID,
                (const BYTE*)hwIdMultiSz,
                hwIdBytes)) {
            printf("{\"ok\":false,\"error\":\"SetupDiSetDeviceRegistryPropertyW(HARDWAREID) failed (%lu)\"}\n", GetLastError());
            SetupDiDestroyDeviceInfoList(hDevInfo);
            return 1;
        }

        if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, hDevInfo, &devInfoData)) {
            printf("{\"ok\":false,\"error\":\"SetupDiCallClassInstaller(DIF_REGISTERDEVICE) failed (%lu)\"}\n", GetLastError());
            SetupDiDestroyDeviceInfoList(hDevInfo);
            return 1;
        }

        SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfoData, targetInstanceId, 256, NULL);

        // この新しいデバイスノードに DriverStore 内の署名済みドライバをインストール
        BOOL needReboot = FALSE;
        if (!DiInstallDevice(NULL, hDevInfo, &devInfoData, NULL, 0, &needReboot)) {
            // フォールバック: 同梱INFまたはDriverStoreのINFを指定して UpdateDriverForPlugAndPlayDevicesW を実行
            if (!UpdateDriverForPlugAndPlayDevicesW(
                    NULL,
                    activeHwId,
                    stagedOemInf,
                    INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE,
                    &needReboot)) {
                DWORD err = GetLastError();
                SetupDiCallClassInstaller(DIF_REMOVE, hDevInfo, &devInfoData);
                SetupDiDestroyDeviceInfoList(hDevInfo);
                printf("{\"ok\":false,\"error\":\"Driver install failed (%lu)\"}\n", err);
                return 1;
            }
        }

        // デバイスノード自体の FriendlyName / DeviceDesc を "VocalGzzio Driver (WDM)" に設定
        SetupDiSetDeviceRegistryPropertyW(
            hDevInfo,
            &devInfoData,
            SPDRP_FRIENDLYNAME,
            (const BYTE*)VOCALGZZIO_WDM_ADAPTER_NAME,
            (DWORD)((wcslen(VOCALGZZIO_WDM_ADAPTER_NAME) + 1) * sizeof(wchar_t))
        );
        SetupDiSetDeviceRegistryPropertyW(
            hDevInfo,
            &devInfoData,
            SPDRP_DEVICEDESC,
            (const BYTE*)VOCALGZZIO_WDM_ADAPTER_NAME,
            (DWORD)((wcslen(VOCALGZZIO_WDM_ADAPTER_NAME) + 1) * sizeof(wchar_t))
        );

        SetupDiDestroyDeviceInfoList(hDevInfo);
    }

    // AudioEndpointBuilder が Render / Capture エンドポイントを生成するのを待機して名称・有効化を揃える
    int updated = 0;
    for (int retry = 0; retry < 15; ++retry) {
        updated = RenameEndpointsForInstance(targetInstanceId, true);
        if (updated >= 2) break;
        Sleep(250);
    }
    Sleep(200);
    RenameEndpointsForInstance(targetInstanceId, true);

    // Windows が新規追加された仮想ドライバを勝手に「既定の再生デバイス（スピーカー）」へ昇格させ、
    // cluster / VRChat / Discord の音が消えてしまう問題を防ぐため、元の実スピーカー・マイクへ即座に復元する
    RestoreRealDefaultEndpoints(&savedDefaults);

    return PrintStatusJson();
}

static int UninstallVocalGzzioWdmDriver() {
    HDEVINFO hDevInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_MEDIA_CUSTOM, L"ROOT\\MEDIA", NULL, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return 0;

    SP_DEVINFO_DATA devData;
    devData.cbSize = sizeof(SP_DEVINFO_DATA);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devData); ++i) {
        wchar_t instId[256] = {0};
        if (!SetupDiGetDeviceInstanceIdW(hDevInfo, &devData, instId, 256, NULL)) continue;

        wchar_t hwId[256] = {0};
        if (!SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devData, SPDRP_HARDWAREID, NULL, (PBYTE)hwId, sizeof(hwId), NULL)) continue;
        const bool isNativeHw = (_wcsicmp(hwId, VOCALGZZIO_NATIVE_HWID) == 0);
        const bool isLegacyHw = (_wcsicmp(hwId, VOCALGZZIO_LEGACY_HWID) == 0);
        if (!isNativeHw && !isLegacyHw) continue;

        wchar_t friendly[256] = {0};
        wchar_t desc[256] = {0};
        SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devData, SPDRP_FRIENDLYNAME, NULL, (PBYTE)friendly, sizeof(friendly), NULL);
        SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devData, SPDRP_DEVICEDESC, NULL, (PBYTE)desc, sizeof(desc), NULL);

        // 自社製ノード (ROOT\VocalGzzioAudio) または名称が VocalGzzio Driver (WDM) と一致するノードのみ削除し、
        // GzzioRoom Driver (WDM) や Yamaha SYNCROOM Driver (WDM) はインスタンス番号に関わらず保護する
        if (isNativeHw ||
            _wcsicmp(friendly, VOCALGZZIO_WDM_ADAPTER_NAME) == 0 ||
            _wcsicmp(desc, VOCALGZZIO_WDM_ADAPTER_NAME) == 0) {
            SetupDiCallClassInstaller(DIF_REMOVE, hDevInfo, &devData);
        }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return PrintStatusJson();
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc >= 2 && _wcsicmp(argv[1], L"--status") == 0) {
        return PrintStatusJson();
    }

    bool wantUninstall = (argc >= 2 && _wcsicmp(argv[1], L"--uninstall") == 0);
    bool noElevate     = (argc >= 3 && _wcsicmp(argv[2], L"--no-elevate") == 0);

    if (!wantUninstall && IsRunningOnArm64Windows() && !HasSignedNativeVocalGzzioDriver(NULL, 0, NULL, 0)) {
        printf("{\"ok\":true,\"installed\":false,\"skippedArm64\":true,\"adapterName\":\"VocalGzzio Driver (WDM)\"}\n");
        return 0;
    }

    if (!IsUserAnAdmin()) {
        if (noElevate) {
            printf("{\"ok\":false,\"error\":\"Administrator privileges required\"}\n");
            return 1;
        }
        // 管理者権限で自身を再起動 (UACプロンプト表示)
        wchar_t exePath[MAX_PATH] = {0};
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        wchar_t params[128] = {0};
        swprintf_s(params, L"%s --no-elevate", wantUninstall ? L"--uninstall" : L"--install");

        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.lpVerb = L"runas";
        sei.lpFile = exePath;
        sei.lpParameters = params;
        sei.nShow = SW_HIDE;
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;

        if (!ShellExecuteExW(&sei)) {
            printf("{\"ok\":false,\"error\":\"UAC elevation cancelled or failed\"}\n");
            return 1;
        }
        if (sei.hProcess) {
            WaitForSingleObject(sei.hProcess, 25000);
            CloseHandle(sei.hProcess);
        }
        return PrintStatusJson();
    }

    if (wantUninstall) {
        return UninstallVocalGzzioWdmDriver();
    }
    return InstallVocalGzzioWdmDriver();
}
