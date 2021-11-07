/*
 *   Copyright (C) 2021 GaryOderNichts
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "pair_menu.hpp"
#include "ipc.hpp"

#include <unistd.h>
#include <cstring>
#include <coreinit/dynload.h>
#include <nsyshid/hid.h>
#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>

#include <whb/log.h>
#include <whb/log_console.h>

#define DS3_VID 0x054c
#define DS3_PID 0x0268

#define HID_REPORT_FEATURE 3

static uint8_t controller_bda[6]{};

static IOSHandle btrmHandle = -1;

int ds3ReadBDA(uint32_t handle, uint8_t* outBDA)
{
    __attribute__ ((aligned (0x20))) uint8_t buf[18]{};
    int res = HIDGetReport(handle, HID_REPORT_FEATURE, 0xf2, buf, sizeof(buf), nullptr, nullptr);
    if (res >= 0) {
        memcpy(outBDA, buf + 4, 6);
    }

    return res;
}

int ds3WriteMasterBDA(uint32_t handle, uint8_t* bda)
{
    __attribute__ ((aligned (0x20))) uint8_t buf[8]{};
    memcpy(buf + 2, bda, 6);
    return HIDSetReport(handle, HID_REPORT_FEATURE, 0xf5, buf, sizeof(buf), nullptr, nullptr);
}

int32_t hidAttachCallback(HIDClient* client, HIDDevice* device, HIDAttachEvent event)
{
    if (event == HID_DEVICE_ATTACH) {
        if (__builtin_bswap16(device->vid) == DS3_VID && __builtin_bswap16(device->pid) == DS3_PID) {
            uint8_t bda[6]{};
            int res = ds3ReadBDA(device->handle, bda);
            if (res < 0) {
                WHBLogPrintf("Failed to read bda: %x", res);
                WHBLogConsoleDraw();
                return HID_DEVICE_DETACH;
            }

            WHBLogPrintf("DualShock 3 (%02x:%02x:%02x:%02x:%02x:%02x) connected",
                bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

            WHBLogPrintf("Setting master address...");
            WHBLogConsoleDraw();

            res = ds3WriteMasterBDA(device->handle, controller_bda);
            if (res < 0) {
                WHBLogPrintf("Failed write master bda: %x", res);
                WHBLogConsoleDraw();
                return HID_DEVICE_DETACH;
            }

            WHBLogPrintf("Adding pairing...");
            WHBLogConsoleDraw();

            uint8_t link_key[16]{};
            addControllerPairing(btrmHandle, bda, 
                link_key, // we'll bypass security for a ds3 anyways, so just add an empty link key
                "Nintendo RVL-CNT-01-UC", // use the pro controller name
                DS3_VID, DS3_PID
            );

            WHBLogPrintf("Paired!");
            WHBLogConsoleDraw();
        }
    }

    return HID_DEVICE_DETACH;
}

void handle_pairing_menu()
{
    void (*KPADInit)() = nullptr;
    void (*KPADShutdown)() = nullptr;
    void (*WPADEnableURCC)(int32_t enable) = nullptr;
    int32_t (*KPADRead)(KPADChan chan, KPADStatus *data, uint32_t size) = nullptr;

    // we can't link against padscore directly or else the load callback won't be called on acquiring
    OSDynLoad_Module module;
    OSDynLoad_Acquire("padscore.rpl", &module);

    OSDynLoad_FindExport(module, FALSE, "KPADInit", (void**) &KPADInit);
    OSDynLoad_FindExport(module, FALSE, "KPADShutdown", (void**) &KPADShutdown);
    OSDynLoad_FindExport(module, FALSE, "WPADEnableURCC", (void**) &WPADEnableURCC);
    OSDynLoad_FindExport(module, FALSE, "KPADRead", (void**) &KPADRead);

    VPADInit();
    KPADInit();
    WPADEnableURCC(1);

    bool openMenu = false;
    for (int i = 0; i < 5; i++) {
        VPADStatus status{};
        VPADRead(VPAD_CHAN_0, &status, 1, nullptr);

        if (status.hold & VPAD_BUTTON_A) {
            openMenu = true;
            break;
        }

        for (int i = 0; i < 4; i++) {
            KPADStatus kpad_status{};
            KPADRead((KPADChan) i, &kpad_status, 1);

            if (kpad_status.extensionType == WPAD_EXT_CLASSIC ||
                kpad_status.extensionType == WPAD_EXT_PRO_CONTROLLER ||
                kpad_status.extensionType == WPAD_EXT_MPLUS_CLASSIC) {
                
                if (kpad_status.pro.hold & WPAD_PRO_BUTTON_A) {
                    openMenu = true;
                    break;
                }
            }
            else {
                if (kpad_status.hold & WPAD_BUTTON_A) {
                    openMenu = true;
                    break;
                }
            }
        }
    }

    if (!openMenu) {
        VPADShutdown();
        KPADShutdown();
        return;
    }

    WHBLogConsoleInit();
    WHBLogConsoleSetColor(0);
    WHBLogPrintf("=== Bloopair USB Controller pairing ===");
    WHBLogPrintf("Connect a DualShock 3 using a USB cable to pair it");
    WHBLogPrintf("Press B to exit");
    WHBLogConsoleDraw();

    btrmHandle = openBtrm();
    if (btrmHandle < 0) {
        WHBLogPrintf("Failed to open btrm");
        WHBLogConsoleDraw();
        sleep(2);
        VPADShutdown();
        KPADShutdown();
        return;
    }

    if (readControllerBDAddr(btrmHandle, controller_bda) < 0) {
        WHBLogPrintf("Failed to read local bda");
        WHBLogConsoleDraw();
        sleep(2);
        VPADShutdown();
        KPADShutdown();
        return;
    }

    WHBLogPrintf("Local BDA is: %02x:%02x:%02x:%02x:%02x:%02x",
        controller_bda[0], controller_bda[1], controller_bda[2], controller_bda[3], controller_bda[4], controller_bda[5]);
    WHBLogPrintf("");
    WHBLogConsoleDraw();

    if (HIDSetup() < 0) {
        WHBLogPrintf("Failed to setup HID");
        WHBLogConsoleDraw();
        sleep(2);
        VPADShutdown();
        return;
    }

    HIDClient client;
    if (HIDAddClient(&client, hidAttachCallback) < 0) {
        WHBLogPrintf("Failed to add HID client");
        WHBLogConsoleDraw();
        sleep(2);
        VPADShutdown();
        KPADShutdown();
        return;
    }

    bool menuOpened = true;
    while (menuOpened) {
        VPADStatus status{};
        VPADRead(VPAD_CHAN_0, &status, 1, nullptr);

        if (status.hold & VPAD_BUTTON_B) {
            menuOpened = false;
            break;
        }

        for (int i = 0; i < 4; i++) {
            KPADStatus kpad_status{};
            KPADRead((KPADChan) i, &kpad_status, 1);

            if (kpad_status.extensionType == WPAD_EXT_CLASSIC ||
                kpad_status.extensionType == WPAD_EXT_PRO_CONTROLLER ||
                kpad_status.extensionType == WPAD_EXT_MPLUS_CLASSIC) {
                
                if (kpad_status.pro.hold & WPAD_PRO_BUTTON_B) {
                    menuOpened = false;
                    break;
                }
            }
            else {
                if (kpad_status.hold & WPAD_BUTTON_B) {
                    menuOpened = false;
                    break;
                }
            }
        }
    }

    HIDDelClient(&client);
    HIDTeardown();

    closeBtrm(btrmHandle);

    WHBLogConsoleFree();

    VPADShutdown();
    
    WPADEnableURCC(0);
    KPADShutdown();

    // we should release the module too but that crashes for some reason?
}
