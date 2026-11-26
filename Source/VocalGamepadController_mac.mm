#include "VocalGamepadController.h"

#if JUCE_MAC
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

namespace gz
{
bool pollMacGameController (GamepadState& out) noexcept
{
    @autoreleasepool
    {
        NSArray<GCController*>* controllers = [GCController controllers];
        if (controllers == nil || [controllers count] == 0)
            return false;

        for (GCController* ctrl in controllers)
        {
            GCExtendedGamepad* gp = ctrl.extendedGamepad;
            if (gp == nil)
                continue;

            out.connected = true;
            NSString* cat = ctrl.productCategory ?: @"";
            NSString* vnd = ctrl.vendorName ?: @"Wireless Controller";
            out.deviceName = juce::String::fromUTF8 ([vnd UTF8String]);

            if ([cat containsString:@"DualSense"] || [cat containsString:@"DualShock"] || [vnd containsString:@"Sony"] || [vnd containsString:@"PlayStation"])
                out.brand = 1; // PlayStation
            else if ([cat containsString:@"Switch"] || [cat containsString:@"Nintendo"] || [vnd containsString:@"Nintendo"])
                out.brand = 2; // Nintendo Switch
            else if ([cat containsString:@"Xbox"] || [vnd containsString:@"Xbox"])
                out.brand = 3; // Xbox
            else
                out.brand = 0;

            out.lx = VocalGamepadPoller::applyDeadzone (gp.leftThumbstick.xAxis.value,  0.16f);
            out.ly = VocalGamepadPoller::applyDeadzone (-gp.leftThumbstick.yAxis.value, 0.16f);
            out.rx = VocalGamepadPoller::applyDeadzone (gp.rightThumbstick.xAxis.value, 0.22f);
            out.ry = VocalGamepadPoller::applyDeadzone (-gp.rightThumbstick.yAxis.value, 0.22f);

            out.l2 = VocalGamepadPoller::applyDeadzone (gp.leftTrigger.value,  0.12f);
            out.r2 = VocalGamepadPoller::applyDeadzone (gp.rightTrigger.value, 0.12f);

            out.l1     = gp.leftShoulder.isPressed;
            out.r1     = gp.rightShoulder.isPressed;
            out.dUp    = gp.dpad.up.isPressed;
            out.dDown  = gp.dpad.down.isPressed;
            out.dLeft  = gp.dpad.left.isPressed;
            out.dRight = gp.dpad.right.isPressed;

            // GCExtendedGamepad は物理配置（buttonX=左/□, buttonY=上/△, buttonB=右/○, buttonA=下/×）で統一規格化されている
            out.btnWest  = gp.buttonX.isPressed; // □ / Y / X (左 -> ミュート)
            out.btnNorth = gp.buttonY.isPressed; // △ / X / Y (上 -> ハモリ)
            out.btnEast  = gp.buttonB.isPressed; // ○ / A / B (右 -> サビ演出)
            out.btnSouth = gp.buttonA.isPressed; // × / B / A (下 -> 原音比較/返しミュート)

            if (@available (macOS 10.14.1, *))
            {
                if (gp.leftThumbstickButton != nil)  out.l3 = gp.leftThumbstickButton.isPressed;
                if (gp.rightThumbstickButton != nil) out.r3 = gp.rightThumbstickButton.isPressed;
            }
            if (@available (macOS 10.15, *))
            {
                if (gp.buttonMenu != nil)    out.start  = gp.buttonMenu.isPressed;
                if (gp.buttonOptions != nil) out.select = gp.buttonOptions.isPressed;
            }
            return true;
        }
    }
    return false;
}
} // namespace gz
#endif
