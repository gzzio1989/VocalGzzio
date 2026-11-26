#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

#if JUCE_WINDOWS
namespace gz_win_pad
{
    // windows.h / mmsystem.h の near/far/min/max マクロ汚染を100%回避するABI完全一致Win32定義
    #pragma pack(push, 1)
    struct XInputGamepadRaw
    {
        std::uint16_t wButtons;
        std::uint8_t  bLeftTrigger;
        std::uint8_t  bRightTrigger;
        std::int16_t  sThumbLX;
        std::int16_t  sThumbLY;
        std::int16_t  sThumbRX;
        std::int16_t  sThumbRY;
    };
    struct XInputStateRaw
    {
        std::uint32_t    dwPacketNumber;
        XInputGamepadRaw Gamepad;
    };
    struct JoyInfoExRaw
    {
        std::uint32_t dwSize;
        std::uint32_t dwFlags;
        std::uint32_t dwXpos;
        std::uint32_t dwYpos;
        std::uint32_t dwZpos;
        std::uint32_t dwRpos;
        std::uint32_t dwUpos;
        std::uint32_t dwVpos;
        std::uint32_t dwButtons;
        std::uint32_t dwButtonNumber;
        std::uint32_t dwPOV;
        std::uint32_t dwReserved1;
        std::uint32_t dwReserved2;
    };
    struct JoyCapsWRaw
    {
        std::uint16_t wMid;
        std::uint16_t wPid;
        wchar_t       szPname[32];
        std::uint32_t wXmin, wXmax, wYmin, wYmax, wZmin, wZmax;
        std::uint32_t wNumButtons, wPeriodMin, wPeriodMax;
        std::uint32_t wRmin, wRmax, wUmin, wUmax, wVmin, wVmax;
        std::uint32_t wCaps, wMaxAxes, wNumAxes, wMaxButtons;
        wchar_t       szRegKey[32];
        wchar_t       szOEMVxD[260];
    };
    #pragma pack(pop)

    static_assert (sizeof (JoyInfoExRaw) == 52,  "WinMM JOYINFOEX ABI");
    static_assert (sizeof (JoyCapsWRaw)  == 728, "WinMM JOYCAPSW ABI");

    using PFN_XInputGetState = std::uint32_t (__stdcall*) (std::uint32_t, XInputStateRaw*);
    extern "C" __declspec(dllimport) void* __stdcall LoadLibraryW (const wchar_t* lpLibFileName);
    extern "C" __declspec(dllimport) void* __stdcall GetProcAddress (void* hModule, const char* lpProcName);
    extern "C" __declspec(dllimport) int   __stdcall FreeLibrary (void* hLibModule);
    extern "C" __declspec(dllimport) unsigned int __stdcall joyGetNumDevs();
    extern "C" __declspec(dllimport) unsigned int __stdcall joyGetPosEx (unsigned int uJoyID, JoyInfoExRaw* pji);
    extern "C" __declspec(dllimport) unsigned int __stdcall joyGetDevCapsW (std::uintptr_t uJoyID, JoyCapsWRaw* pjc, unsigned int cbjc);
}
#endif

namespace gz
{
// PlayStation (DualSense/DS4) / Nintendo Switch Pro / Xbox / 汎用ワイヤレスコントローラー統合入力状態
struct GamepadState
{
    bool         connected { false };
    int          brand     { 0 }; // 0=汎用, 1=PlayStation(△○×□), 2=Switch(XYAB), 3=Xbox(YXBA)
    juce::String deviceName;

    float lx { 0.0f }, ly { 0.0f }; // 左スティック (-1..+1, 右+, 下+) -> Argon声の質感パッド
    float rx { 0.0f }, ry { 0.0f }; // 右スティック (-1..+1) -> 音色・プリセット選択
    float l2 { 0.0f }, r2 { 0.0f }; // L2 / R2 アナログトリガー (0..1, 遊びデッドゾーン処理済)

    bool l1     { false }, r1     { false }; // L1 / R1 バンパー -> 操作対象スライド切替
    bool dUp    { false }, dDown  { false }; // 十字キー↑(アンビエント) / ↓(プリセット順送り)
    bool dLeft  { false }, dRight { false }; // 十字キー←(ピンポンディレイ) / →(リライト風刻みディレイ)

    bool btnWest  { false }; // 左ボタン (PS: □ / Xbox: X / Switch: Y) -> ミュート
    bool btnNorth { false }; // 上ボタン (PS: △ / Xbox: Y / Switch: X) -> 美しいハモリ
    bool btnEast  { false }; // 右ボタン (PS: ○ / Xbox: B / Switch: A) -> サビ演出リフト
    bool btnSouth { false }; // 下ボタン (PS: × / Xbox: A / Switch: B) -> 原音比較 / 返しミュート

    bool l3     { false }, r3    { false }; // L3押し込み(質感パッド位置確定ロック) / R3押し込み
    bool select { false }, start { false }; // SELECT長押し(ノイズ学習) / START長押し(うた自動)
};

#if JUCE_MAC
bool pollMacGameController (GamepadState& outState) noexcept;
#endif

class VocalGamepadPoller
{
public:
    VocalGamepadPoller() = default;
    ~VocalGamepadPoller()
    {
       #if JUCE_WINDOWS
        if (xinputDll != nullptr)
            gz_win_pad::FreeLibrary (xinputDll);
       #endif
    }

    static float applyDeadzone (float v, float dz) noexcept
    {
        const float a = std::abs (v);
        if (a <= dz) return 0.0f;
        const float norm = (a - dz) / std::max (0.01f, 1.0f - dz);
        return std::copysign (std::min (1.0f, norm), v);
    }

    bool poll (GamepadState& out) noexcept
    {
        out = GamepadState{};

       #if JUCE_MAC
        return pollMacGameController (out);
       #elif JUCE_WINDOWS
        ensureXInputLoaded();

        // 1. XInput (Xbox Series / One / XInput互換ワイヤレスパッド)
        if (fnXInputGetState != nullptr)
        {
            gz_win_pad::XInputStateRaw xs {};
            for (std::uint32_t idx = 0; idx < 4u; ++idx)
            {
                if (fnXInputGetState (idx, &xs) == 0u)
                {
                    out.connected  = true;
                    out.brand      = 3;
                    out.deviceName = "Xbox / XInput Controller";
                    const std::uint16_t b = xs.Gamepad.wButtons;
                    out.dUp        = (b & 0x0001u) != 0;
                    out.dDown      = (b & 0x0002u) != 0;
                    out.dLeft      = (b & 0x0004u) != 0;
                    out.dRight     = (b & 0x0008u) != 0;
                    out.start      = (b & 0x0010u) != 0;
                    out.select     = (b & 0x0020u) != 0;
                    out.l3         = (b & 0x0040u) != 0;
                    out.r3         = (b & 0x0080u) != 0;
                    out.l1         = (b & 0x0100u) != 0;
                    out.r1         = (b & 0x0200u) != 0;
                    out.btnSouth   = (b & 0x1000u) != 0; // A
                    out.btnEast    = (b & 0x2000u) != 0; // B
                    out.btnWest    = (b & 0x4000u) != 0; // X
                    out.btnNorth   = (b & 0x8000u) != 0; // Y

                    out.l2 = applyDeadzone ((float) xs.Gamepad.bLeftTrigger  / 255.0f, 0.12f);
                    out.r2 = applyDeadzone ((float) xs.Gamepad.bRightTrigger / 255.0f, 0.12f);
                    out.lx = applyDeadzone ((float) xs.Gamepad.sThumbLX / 32767.0f, 0.16f);
                    out.ly = applyDeadzone (-(float) xs.Gamepad.sThumbLY / 32767.0f, 0.16f);
                    out.rx = applyDeadzone ((float) xs.Gamepad.sThumbRX / 32767.0f, 0.22f);
                    out.ry = applyDeadzone (-(float) xs.Gamepad.sThumbRY / 32767.0f, 0.22f);
                    return true;
                }
            }
        }

        // 2. WinMM / HID (PlayStation DualSense / DualShock 4 / Nintendo Switch Pro / 汎用パッド)
        const unsigned int numDevs = gz_win_pad::joyGetNumDevs();
        for (unsigned int id = 0; id < std::min (numDevs, 8u); ++id)
        {
            gz_win_pad::JoyInfoExRaw ji {};
            ji.dwSize  = sizeof (gz_win_pad::JoyInfoExRaw);
            ji.dwFlags = 0x000000FFu; // JOY_RETURNALL
            if (gz_win_pad::joyGetPosEx (id, &ji) != 0u)
                continue;

            gz_win_pad::JoyCapsWRaw caps {};
            int brand = 0;
            juce::String devName = "Wireless Controller";
            if (gz_win_pad::joyGetDevCapsW ((std::uintptr_t) id, &caps, sizeof (caps)) == 0u)
            {
                devName = juce::String (caps.szPname);
                if (caps.wMid == 0x054Cu || devName.containsIgnoreCase ("Wireless Controller") || devName.containsIgnoreCase ("DualSense") || devName.containsIgnoreCase ("DUALSHOCK"))
                {
                    brand = 1; // Sony PlayStation
                    if (! devName.containsIgnoreCase ("Dual"))
                        devName = "PlayStation Controller";
                }
                else if (caps.wMid == 0x057Eu || devName.containsIgnoreCase ("Pro Controller") || devName.containsIgnoreCase ("Joy-Con"))
                {
                    brand = 2; // Nintendo Switch
                    devName = "Switch Controller";
                }
                else if (caps.wMid == 0x045Eu)
                {
                    brand = 3; // Xbox
                }
            }

            out.connected  = true;
            out.brand      = brand;
            out.deviceName = devName;

            auto normAxis = [] (std::uint32_t raw, std::uint32_t lo, std::uint32_t hi) noexcept -> float
            {
                if (hi <= lo) return 0.0f;
                const float u = ((float) raw - (float) lo) / (float) (hi - lo);
                return std::clamp (u * 2.0f - 1.0f, -1.0f, 1.0f);
            };
            const std::uint32_t xLo = (caps.wXmax > caps.wXmin) ? caps.wXmin : 0u;
            const std::uint32_t xHi = (caps.wXmax > caps.wXmin) ? caps.wXmax : 65535u;
            const std::uint32_t yLo = (caps.wYmax > caps.wYmin) ? caps.wYmin : 0u;
            const std::uint32_t yHi = (caps.wYmax > caps.wYmin) ? caps.wYmax : 65535u;
            const std::uint32_t zLo = (caps.wZmax > caps.wZmin) ? caps.wZmin : 0u;
            const std::uint32_t zHi = (caps.wZmax > caps.wZmin) ? caps.wZmax : 65535u;
            const std::uint32_t rLo = (caps.wRmax > caps.wRmin) ? caps.wRmin : 0u;
            const std::uint32_t rHi = (caps.wRmax > caps.wRmin) ? caps.wRmax : 65535u;
            const std::uint32_t uLo = (caps.wUmax > caps.wUmin) ? caps.wUmin : 0u;
            const std::uint32_t uHi = (caps.wUmax > caps.wUmin) ? caps.wUmax : 65535u;
            const std::uint32_t vLo = (caps.wVmax > caps.wVmin) ? caps.wVmin : 0u;
            const std::uint32_t vHi = (caps.wVmax > caps.wVmin) ? caps.wVmax : 65535u;

            out.lx = applyDeadzone (normAxis (ji.dwXpos, xLo, xHi), 0.16f);
            out.ly = applyDeadzone (normAxis (ji.dwYpos, yLo, yHi), 0.16f);

            const std::uint32_t b = ji.dwButtons;
            if (brand == 1) // PlayStation DualSense / DualShock 4
            {
                out.btnWest  = (b & (1u << 0)) != 0; // □
                out.btnSouth = (b & (1u << 1)) != 0; // ×
                out.btnEast  = (b & (1u << 2)) != 0; // ○
                out.btnNorth = (b & (1u << 3)) != 0; // △
                out.l1       = (b & (1u << 4)) != 0;
                out.r1       = (b & (1u << 5)) != 0;
                out.select   = (b & (1u << 8)) != 0;
                out.start    = (b & (1u << 9)) != 0;
                out.l3       = (b & (1u << 10)) != 0;
                out.r3       = (b & (1u << 11)) != 0;

                out.rx = applyDeadzone (normAxis (ji.dwZpos, zLo, zHi), 0.22f);
                out.ry = applyDeadzone (normAxis (ji.dwRpos, rLo, rHi), 0.22f);
                const float rawL2 = (caps.wCaps & 0x0004u) ? ((float) (ji.dwUpos - uLo) / (float) std::max (1u, uHi - uLo)) : 0.0f; // JOYCAPS_HASU
                const float rawR2 = (caps.wCaps & 0x0008u) ? ((float) (ji.dwVpos - vLo) / (float) std::max (1u, vHi - vLo)) : 0.0f; // JOYCAPS_HASV
                out.l2 = std::max ((b & (1u << 6)) ? 1.0f : 0.0f, applyDeadzone (rawL2, 0.12f));
                out.r2 = std::max ((b & (1u << 7)) ? 1.0f : 0.0f, applyDeadzone (rawR2, 0.12f));
            }
            else if (brand == 2) // Nintendo Switch Pro Controller
            {
                out.btnSouth = (b & (1u << 0)) != 0; // B
                out.btnEast  = (b & (1u << 1)) != 0; // A
                out.btnWest  = (b & (1u << 2)) != 0; // Y
                out.btnNorth = (b & (1u << 3)) != 0; // X
                out.l1       = (b & (1u << 4)) != 0;
                out.r1       = (b & (1u << 5)) != 0;
                out.l2       = (b & (1u << 6)) ? 1.0f : 0.0f;
                out.r2       = (b & (1u << 7)) ? 1.0f : 0.0f;
                out.select   = (b & (1u << 8)) != 0;
                out.start    = (b & (1u << 9)) != 0;
                out.l3       = (b & (1u << 10)) != 0;
                out.r3       = (b & (1u << 11)) != 0;
                out.rx       = applyDeadzone (normAxis (ji.dwUpos, uLo, uHi), 0.22f);
                out.ry       = applyDeadzone (normAxis (ji.dwRpos, rLo, rHi), 0.22f);
            }
            else
            {
                out.btnWest  = (b & (1u << 0)) != 0;
                out.btnSouth = (b & (1u << 1)) != 0;
                out.btnEast  = (b & (1u << 2)) != 0;
                out.btnNorth = (b & (1u << 3)) != 0;
                out.l1       = (b & (1u << 4)) != 0;
                out.r1       = (b & (1u << 5)) != 0;
                out.l2       = (b & (1u << 6)) ? 1.0f : 0.0f;
                out.r2       = (b & (1u << 7)) ? 1.0f : 0.0f;
                out.select   = (b & (1u << 8)) != 0;
                out.start    = (b & (1u << 9)) != 0;
                out.l3       = (b & (1u << 10)) != 0;
                out.r3       = (b & (1u << 11)) != 0;
                out.rx       = applyDeadzone (normAxis (ji.dwZpos, zLo, zHi), 0.22f);
                out.ry       = applyDeadzone (normAxis (ji.dwRpos, rLo, rHi), 0.22f);
            }

            if (ji.dwPOV != 65535u && ji.dwPOV <= 36000u)
            {
                const std::uint32_t deg = ji.dwPOV / 100u;
                out.dUp    = (deg >= 315u || deg <= 45u);
                out.dRight = (deg >= 45u  && deg <= 135u);
                out.dDown  = (deg >= 135u && deg <= 225u);
                out.dLeft  = (deg >= 225u && deg <= 315u);
            }
            return true;
        }
        return false;
       #else
        return false;
       #endif
    }

private:
   #if JUCE_WINDOWS
    void*                         xinputDll        { nullptr };
    gz_win_pad::PFN_XInputGetState fnXInputGetState { nullptr };
    bool                          xinputChecked    { false };

    void ensureXInputLoaded() noexcept
    {
        if (xinputChecked) return;
        xinputChecked = true;
        const wchar_t* dlls[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
        for (auto* name : dlls)
        {
            if (void* h = gz_win_pad::LoadLibraryW (name))
            {
                auto fn = reinterpret_cast<gz_win_pad::PFN_XInputGetState> (gz_win_pad::GetProcAddress (h, "XInputGetState"));
                if (fn != nullptr)
                {
                    xinputDll        = h;
                    fnXInputGetState = fn;
                    return;
                }
                gz_win_pad::FreeLibrary (h);
            }
        }
    }
   #endif
};
} // namespace gz
