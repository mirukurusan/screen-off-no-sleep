# Turn Off Display Without Sleep

A lightweight Windows utility and [Windhawk](https://windhawk.net/) mod for laptops and tablets with **Modern Standby (S0 Low Power Idle / S0ix)**. It turns the physical display off cleanly on demand while keeping background audio playback, downloads, and mobile hotspots running uninterrupted.

Available as:
* **Standalone Executable (`screen-off-no-sleep.exe`)**: A dedicated system tray application with an interactive hotkey dialog, registry persistence, light/dark taskbar adaptive icons, and Per-Monitor V2 High-DPI support.
* **Windhawk Mod ([screen-off-no-sleep](https://windhawk.net/mods/screen-off-no-sleep))**: A module available on the official Windhawk repository for users managing their tweaks via the Windhawk mod manager (source in `windhawk-mod/screen-off-no-sleep.wh.cpp`).

---

## 1. Background & The Problem

On modern Windows laptops running **[Modern Standby (S0ix)](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby-states)**, turning off the screen does not always behave the same way:

| Scenario | Trigger / Reason | PDC Modern Standby Behavior | Background Audio & Hotspot |
| :--- | :--- | :--- | :--- |
| **User-Initiated Sleep / Display Off** | Clicking "Sleep", closing lid (sleep action), or calling `SC_MONITORPOWER 2` / `SetSuspendState` | Windows Power Dependency Coordinator (PDC) enters an explicit sleep transition. **[Desktop Activity Moderator (DAM)](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/prepare-software-for-modern-standby)** immediately suspends Win32 processes. | **Terminated / Paused** ([Network teardown](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby-network-connectivity)) |
| **Natural Display Timeout** | Inactivity timeout (`Video Idle Timeout`) | Windows enters the *Screen Off (No-CS)* phase. Active audio streams (`PowerRequestAudio`) and mobile hotspot (`PowerRequestSystem`) **[block the transition to sleep indefinitely](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby-states)**. | **Continues Running** |

Traditionally, users who want to turn off the screen while listening to music or sharing Wi-Fi had to wait for their full display timeout (e.g. 5–10 minutes) or set lock-screen workarounds that force PIN / Windows Hello re-authentication every time.

**Turn Off Display Without Sleep** triggers this natural idle path programmatically on demand.

---

## 2. How It Works (Architecture & Principles)

The application operates as a state machine managing display timeouts and visual overlays:

```
[ User Action ]
  ├── Global Hotkey (Default: Win+Alt+L)
  ├── System Tray Icon (Left-click / Double-click)
  ├── Duplicate Launch (Double-click screen-off-no-sleep.exe)
  ├── Hardware Power Button (Surface / 2-in-1 HID)
  ├── Laptop Lid Close (LIDACTION = 0)
  └── Window Message (ScreenOffNoSleep_Trigger)
         │
         ▼
[ Step 1: Instant Blackout Overlay ]
  • Displays a topmost, black, frameless window covering all monitors (SM_XVIRTUALSCREEN).
  • Provides instant visual feedback with zero perceptible latency.
         │
         ▼
[ Step 2: Dynamic 1-Second Timeout Injection ]
  • Saves the active scheme GUID and current AC/DC timeouts to persistent storage.
  • Dynamically writes VIDEOIDLE = 1 s to the active Windows power scheme.
  • Because the user is not touching mouse/keyboard, Windows PDC marks the system
    as qualifying for a natural "Video Idle Timeout" (firmware notification).
         │
         ▼
[ Step 3: Hardware Monitor Power-Down ]
  • Display hardware powers down naturally (~1–2 seconds).
  • Windows sends GUID_CONSOLE_DISPLAY_STATE == 0 notification.
         │
         ▼
[ Step 4: State Restoration & Optional Silent Lock ]
  • Restores the exact captured AC/DC timeouts back into the power scheme and clears storage.
  • Dismisses the blackout overlay window.
  • Optional: Silently locks workstation in the dark (LockOnScreenOff = true),
    or keeps active desktop for immediate return upon wake (LockOnScreenOff = false).
```

### Key Technical Highlights

1. **Active Desktop Compatibility (No Forced Lock Screen)**:
   Targets `GUID_VIDEO_IDLE_TIMEOUT` (`VIDEOIDLE`), allowing the user to turn off the display and wake straight back to their working desktop with no password or PIN prompt.
2. **Instant Visual Blackout**:
   Displays a pitch-black `WS_EX_TOPMOST` window so the display appears turned off immediately upon user input while the GPU pipeline finishes its hardware power-down sequence.
3. **Failsafe Watchdog**:
   If an unexpected user action (e.g. accidental mouse shake) or an active application holding `ES_DISPLAY_REQUIRED` prevents the idle transition, a 3-second watchdog timer cleans up the overlay and restores the original power timeout automatically.
4. **Crash-Resilient State Restoration**:
   Before injecting the 1-second timeout, the application snapshots the active power plan GUID and current AC/DC timeouts into registry storage (`SavedPowerState`). If the process terminates unexpectedly mid-flow (e.g. power loss), the original timeout is automatically recovered upon the next startup.

---

## 3. Standalone Application Features

The standalone executable (`screen-off-no-sleep.exe`) provides a complete native Windows experience:

* **System Tray Resident**:
  Runs quietly in the notification area. Left-clicking or double-clicking the tray icon immediately triggers display off. Right-clicking opens the full context menu.
* **Hotkey Configuration**:
  Allows customizing the global hotkey via modifier checkboxes (`Win`, `Alt`, `Ctrl`, `Shift`), a key dropdown, live preview, and a one-click "Disable Hotkey" option. Validates hotkey conflicts before applying.
* **Single-Instance Duplicate Trigger**:
  Running a second instance of `screen-off-no-sleep.exe` (e.g. from a desktop shortcut, Stream Deck, or batch file) signals the running background instance to turn off the display immediately and exits cleanly.
* **Windows Registry Persistence**:
  Settings are stored in `HKEY_CURRENT_USER\Software\ScreenOffNoSleep`.
* **Run at Startup**:
  Includes a toggle in the tray menu to automatically register or unregister the app in Windows Task Scheduler, ensuring it launches with elevated privileges at user logon without UAC prompts.

---

## 4. Supported Triggers

* **Global Hotkey** *(Default: `Win+Alt+L`)*:
  Global keyboard shortcut to immediately turn off the display. Fully customizable or can be disabled.
* **Tray Icon**:
  Left-click the notification icon or select **Turn Off Display Now** from the right-click menu.
* **Laptop Lid Close**:
  Closing the laptop lid triggers display-off instantly while keeping background tasks alive (requires native lid action set to "Do nothing").
* **Hardware Power Button (Surface & 2-in-1s)**:
  Intercepts HID power button reports, bypassing Windows sleep transitions (requires native power button action set to "Do nothing").
* **External Window Message (IPC / Automation)**:
  Broadcast or post a registered window message from external scripts or automation tools. The message name and the required sender privileges depend on the delivery form:
  * **Standalone executable** — message `ScreenOffNoSleep_Trigger`:
    The application requires and runs with Administrator privileges, and it does not relax User Interface Privilege Isolation (UIPI). The triggering script or tool must therefore also run as **Administrator**; broadcasts from a non-elevated process are silently discarded.
  * **Windhawk mod** — message `WindhawkScreenOffNoSleep_Trigger`:
    The mod explicitly allows the message through UIPI (`ChangeWindowMessageFilterEx`), so the triggering script or tool can run as a **standard user**.

  The message carries no payload; `wParam` and `lParam` are ignored.

  * **PowerShell**:
    ```powershell
    # Standalone executable (run this script as Administrator):
    $MessageName = "ScreenOffNoSleep_Trigger"
    # Windhawk mod (standard user):
    # $MessageName = "WindhawkScreenOffNoSleep_Trigger"

    Add-Type @"
    using System;
    using System.Runtime.InteropServices;
    public class ScreenOff {
        [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
        public static extern uint RegisterWindowMessage(string lpString);
        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
        public static void Trigger(string name) {
            uint msg = RegisterWindowMessage(name);
            if (msg != 0) {
                PostMessage((IntPtr)0xffff, msg, IntPtr.Zero, IntPtr.Zero);
            }
        }
    }
    "@
    [ScreenOff]::Trigger($MessageName)
    ```
  * **AutoHotkey (v2)**:
    ```ahk
    ; Standalone executable (run the script as Administrator):
    ; name := "ScreenOffNoSleep_Trigger"
    ; Windhawk mod (standard user):
    name := "WindhawkScreenOffNoSleep_Trigger"
    msg := DllCall("RegisterWindowMessage", "Str", name, "UInt")
    DllCall("PostMessage", "Ptr", 0xFFFF, "UInt", msg, "Ptr", 0, "Ptr", 0)
    ```

---

## 5. Prerequisites

To allow this tool to take control of lid-close or power button events, Windows native actions must be set to **"Do nothing" (`0`)** so Windows does not trigger its built-in sleep transition.

Open an elevated Command Prompt or PowerShell and execute:

```cmd
powercfg /setacvalueindex SCHEME_CURRENT SUB_BUTTONS PBUTTONACTION 0
powercfg /setdcvalueindex SCHEME_CURRENT SUB_BUTTONS PBUTTONACTION 0
powercfg /setacvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION 0
powercfg /setdcvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION 0
powercfg /setactive SCHEME_CURRENT
```

> **Warning**: With the lid action set to "Do nothing", closing the lid and putting the laptop into a bag will keep the system running (until the regular system sleep timeout). Explicitly sleep or shut down your laptop before bagging it.

### Reverting Prerequisites
To restore native Windows sleep actions:
```cmd
powercfg /setacvalueindex SCHEME_CURRENT SUB_BUTTONS PBUTTONACTION 1
powercfg /setdcvalueindex SCHEME_CURRENT SUB_BUTTONS PBUTTONACTION 1
powercfg /setacvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION 1
powercfg /setdcvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION 1
powercfg /setactive SCHEME_CURRENT
```

---

## 6. Build Instructions

### Building the Standalone Application
The standalone application supports **x64**, **x86**, and **ARM64** architectures using [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw).

#### Batch Script (Recommended)
Run `build.bat` to build all architectures, or specify a target architecture:

```cmd
:: Build all architectures (x64, x86, arm64)
build.bat

:: Or build a specific architecture
build.bat x64
build.bat x86
build.bat arm64
```

Generated binaries:
* `screen-off-no-sleep-x64.exe`
* `screen-off-no-sleep-x86.exe`
* `screen-off-no-sleep-arm64.exe`

#### Manual Build Commands
Compile with the corresponding LLVM-MinGW toolchain:

* **x64**:
  ```cmd
  x86_64-w64-mingw32-windres -I res -i res\resource.rc -o res\resource-x64.o
  x86_64-w64-mingw32-clang++ -O2 -std=c++17 -Wall -mwindows -municode -static src\*.cpp res\resource-x64.o -o screen-off-no-sleep-x64.exe -lpowrprof -luser32 -lhid -lgdi32 -ladvapi32 -lshell32 -lcomctl32
  ```

* **x86**:
  ```cmd
  i686-w64-mingw32-windres -I res -i res\resource.rc -o res\resource-x86.o
  i686-w64-mingw32-clang++ -O2 -std=c++17 -Wall -mwindows -municode -static src\*.cpp res\resource-x86.o -o screen-off-no-sleep-x86.exe -lpowrprof -luser32 -lhid -lgdi32 -ladvapi32 -lshell32 -lcomctl32
  ```

* **ARM64**:
  ```cmd
  aarch64-w64-mingw32-windres -I res -i res\resource.rc -o res\resource-arm64.o
  aarch64-w64-mingw32-clang++ -O2 -std=c++17 -Wall -mwindows -municode -static src\*.cpp res\resource-arm64.o -o screen-off-no-sleep-arm64.exe -lpowrprof -luser32 -lhid -lgdi32 -ladvapi32 -lshell32 -lcomctl32
  ```

### Installing / Building the Windhawk Mod
* **Direct Install**: Install directly from the official Windhawk catalog at [windhawk.net/mods/screen-off-no-sleep](https://windhawk.net/mods/screen-off-no-sleep).
* **Manual Local Mod**:
  1. In [Windhawk](https://windhawk.net/), navigate to **Mods** -> **Create Local Mod**.
  2. Copy the contents of `windhawk-mod/screen-off-no-sleep.wh.cpp` into the editor.
  3. Click **Compile and run**.

---

## 7. License

MIT License. See [LICENSE](LICENSE) for details.

---

## 8. References & Microsoft Technical Documentation

For deeper understanding of Windows power management and Modern Standby internals, refer to Microsoft's official hardware design guides:

* **[Modern Standby states](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby-states)**: Explains the lifecycle and state transitions within Modern Standby, detailing the *Screen Off* (No-CS) phase versus the *Sleep* phase, and how active power requests block deep sleep transitions.
* **[Prepare software for modern standby](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/prepare-software-for-modern-standby)**: Details the Desktop Activity Moderator (DAM), process suspension mechanics, and how audio streams suppress sleep entry.
* **[Network connectivity in Modern Standby](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby-network-connectivity)**: Covers network power policies, Wi-Fi sleep behavior, and why mobile hotspots stay alive during idle screen-off but terminate upon user-initiated sleep.
* **[Modern Standby firmware notifications](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby-firmware-notifications)**: Details ACPI `_DSM` notifications, display power notifications, and SleepStudy entry reasons (`Video Idle Timeout` vs `Screen Off Request`).
