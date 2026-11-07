# Turn Off Display Without Sleep

A [Windhawk](https://windhawk.net/) mod for Windows laptops and tablets with **Modern Standby (S0 Low Power Idle / S0ix)**. It turns the physical display off cleanly on demand while keeping background audio playback, downloads, and mobile hotspots running uninterrupted.

---

## 1. Background & The Problem

On modern Windows laptops running **[Modern Standby (S0ix)](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby-states)**, turning off the screen does not always behave the same way:

| Scenario | Trigger / Reason | PDC Modern Standby Behavior | Background Audio & Hotspot |
| :--- | :--- | :--- | :--- |
| **User-Initiated Sleep / Display Off** | Clicking "Sleep", closing lid (sleep action), or calling `SC_MONITORPOWER 2` / `SetSuspendState` | Windows Power Dependency Coordinator (PDC) enters an explicit sleep transition. **[Desktop Activity Moderator (DAM)](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/prepare-software-for-modern-standby)** immediately suspends Win32 processes. | **Terminated / Paused** ([Network teardown](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby-network-connectivity)) |
| **Natural Display Timeout** | Inactivity timeout (`Video Idle Timeout`) | Windows enters the *Screen Off (No-CS)* phase. Active audio streams (`PowerRequestAudio`) and mobile hotspot (`PowerRequestSystem`) **[block the transition to sleep indefinitely](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby-states)**. | **Continues Running** |

Traditionally, users who want to turn off the screen while listening to music or sharing Wi-Fi had to wait for their full display timeout (e.g. 5–10 minutes) or set clumsy lock-screen workarounds that force PIN / Windows Hello re-authentication every time.

**Turn Off Display Without Sleep** solves this by programmatically triggering the natural idle path on demand.

---

## 2. How It Works (Architecture & Principles)

The mod acts as a state machine managing display timeouts and visual overlays:

```
[ User Action ]
  ├── Global Hotkey (Win+Alt+L)
  ├── Hardware Power Button (Surface / 2-in-1 HID)
  ├── Laptop Lid Close (LIDACTION = 0)
  └── Window Message (WindhawkScreenOffNoSleep_Trigger)
         │
         ▼
[ Step 1: 0 ms Instant Blackout Overlay ]
  • Displays a topmost, black, frameless window covering all monitors (SM_XVIRTUALSCREEN).
  • Provides instant visual feedback with zero perceptible latency.
         │
         ▼
[ Step 2: Dynamic 1-Second Timeout Injection ]
  • Saves the active scheme GUID and current AC/DC timeouts to mod storage.
  • Dynamically writes VIDEOIDLE = 1 s to the active Windows power scheme.
  • Because the user is not touching mouse/keyboard, Windows PDC marks the system
    as qualifying for a natural "Video Idle Timeout" ([firmware notification](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby-firmware-notifications)).
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
   Earlier approaches relied on `GUID_VIDEO_CONSOLE_LOCK_TIMEOUT_LOCAL` (`VIDEOCONLOCK`), which required locking the workstation first. This mod targets `GUID_VIDEO_IDLE_TIMEOUT_LOCAL` (`VIDEOIDLE`), allowing the user to turn off the display and wake straight back to their working desktop with no password or PIN delay.
2. **0 ms Visual Blackout**:
   Display controllers, TCONs, and GPU driver pipes often take 1–2 seconds to complete an idle power-down sequence. The mod instantly displays a pitch-black `WS_EX_TOPMOST` window so the display appears turned off immediately upon user input.
3. **Failsafe Watchdog**:
   If an unexpected user action (e.g. accidental mouse shake) interrupts the idle state before hardware display-off arrives, a 3-second watchdog timer cleans up the overlay and restores original power timeouts automatically.
4. **Crash-Resilient State Restoration**:
   Before injecting the 1-second timeout, the mod snapshots the active power plan GUID and current AC/DC timeouts into mod storage (`Wh_SetBinaryValue`). The original values are restored and cleared once hardware display-off is confirmed. If the system or tool process terminates mid-flow (e.g. power loss or crash), the original timeout is automatically recovered upon the next startup.

---

## 3. Supported Triggers

The mod supports multiple activation methods:

* **Global Hotkey** *(Default: `Win+Alt+L`)*:
  Global keyboard shortcut to immediately turn off the display. Fully customizable in settings.
* **Laptop Lid Close**:
  Closing the laptop lid triggers display-off instantly while keeping tasks alive (requires native lid action set to "Do nothing").
* **Hardware Power Button (Surface & Modern 2-in-1s)**:
  Directly intercepts hardware HID power button reports, bypassing Windows sleep transitions (requires native power button action set to "Do nothing").
* **External Window Message (IPC / Automation)**:
  Third-party applications, Stream Deck, macro tools, or scripts can trigger the display-off flow by broadcasting or posting the registered window message:
  ```cpp
  // Registered message string
  "WindhawkScreenOffNoSleep_Trigger"
  ```
  The mod configures `ChangeWindowMessageFilterEx` to accept this message across UIPI integrity boundaries (including standard user processes).

  **Automation Examples**:

  * **PowerShell** (taskbar shortcut or script):
    ```powershell
    Add-Type @"
    using System;
    using System.Runtime.InteropServices;
    public class ScreenOff {
        [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
        public static extern uint RegisterWindowMessage(string lpString);
        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
        public static void Trigger() {
            uint msg = RegisterWindowMessage("WindhawkScreenOffNoSleep_Trigger");
            if (msg != 0) {
                PostMessage((IntPtr)0xffff, msg, IntPtr.Zero, IntPtr.Zero);
            }
        }
    }
    "@
    [ScreenOff]::Trigger()
    ```

  * **AutoHotkey (v2)**:
    ```ahk
    msg := DllCall("RegisterWindowMessage", "Str", "WindhawkScreenOffNoSleep_Trigger", "UInt")
    DllCall("PostMessage", "Ptr", 0xFFFF, "UInt", msg, "Ptr", 0, "Ptr", 0)
    ```

---

## 4. Hardware Button Handling & Privilege Isolation (UIPI)

### The Privilege Challenge (UIPI & UIAccess)
By default, Windhawk tool mods run at the interactive user's standard integrity level. When an elevated application (such as an Administrator Command Prompt, PowerShell, or Task Manager) holds foreground focus, Windows **UIPI (User Interface Privilege Isolation)** in `win32k.sys` drops background raw input sink (`RIDEV_INPUTSINK`) messages to prevent unauthorized input monitoring.

This mod solves this by targeting Windhawk's **`windhawk-mod-uiaccess.exe`** execution host:
```cpp
// @include         windhawk-mod-uiaccess.exe
// @include         windhawk-mod-elevated.exe
// @include         windhawk-mod.exe
```
With the `UIAccess` privilege, Windows permits the mod's background input sink to receive hardware button events even when an elevated administrator window is focused.

### Hardware Button Implementations (Surface / 2-in-1s vs Clamshells)

| Device Type | Power Button Mechanism | Mod Behavior |
| :--- | :--- | :--- |
| **Microsoft Surface Family** (Pro, Go, Laptop Studio, Book) | **Virtual HID Framework (`VHF.sys`)** + **`buttonconverter.sys`** reporting under `UsagePage 0x01, Usage 0x0D` (Portable Device Control) | **Fully Supported** via HID Raw Input. |
| **Windows 2-in-1 / Slate Devices** (Dell XPS 2-in-1, HP Spectre x360, ThinkPad Yoga, ROG Flow) | **Intel HID Event Filter (`INT33D5`)** / Slate Button Array implementing Microsoft's `System.Client.Tablet.Buttons` specification | **Fully Supported** via HID Raw Input. |
| **External USB / Bluetooth Keyboards** | Standard HID Consumer (`0x0C/0x01`) or System Control (`0x01/0x80`) Power/Sleep keys | **Fully Supported** via HID Raw Input. |
| **Traditional Clamshell Laptops & Desktops** | ACPI Control Method Button (`ACPI\PNP0C0C`) routed to motherboard EC/PCH | When native button action is "Do nothing" (`0`), Windows kernel power manager (`PopDispatchButtonEvent`) discards the interrupt before user mode sees it. **Use the Trigger Hotkey (`Win+Alt+L`) or Lid Close instead.** |

---

## 5. Prerequisites

To allow this mod to take control of lid-close or power button events, Windows native actions must be set to **"Do nothing" (`0`)** so Windows does not trigger its built-in sleep transition.

Open an elevated Command Prompt or PowerShell and execute:

```cmd
powercfg /setacvalueindex SCHEME_CURRENT SUB_BUTTONS PBUTTONACTION 0
powercfg /setdcvalueindex SCHEME_CURRENT SUB_BUTTONS PBUTTONACTION 0
powercfg /setacvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION 0
powercfg /setdcvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION 0
powercfg /setactive SCHEME_CURRENT
```

---

## 6. Configuration & Settings

The mod provides configurable options in the Windhawk Mod Settings UI:

* **Lock workstation after display turns off (`LockOnScreenOff`)** *(Default: `false`)*:
  * `false`: Display powers down while keeping desktop unlocked. Waking up immediately returns to your open applications.
  * `true`: Silently locks the workstation in the dark once the display powers down, requiring PIN/Windows Hello on wake.
* **Trigger hotkey (`TriggerHotkey`)** *(Default: `Win+Alt+L`)*:
  * Global system-wide shortcut to turn off the display instantly.
* **Turn off display on lid close (`HandleLidClose`)** *(Default: `true`)*:
  * Triggers display-off immediately when the laptop lid is closed (requires native lid action set to "Do nothing").
* **Turn off display on power button (`HandlePowerButton`)** *(Default: `true`)*:
  * Listens for HID button events on Surface and compatible 2-in-1 devices.

---

## 7. Build & Installation

### Requirements
* [Windhawk v2.0+](https://windhawk.net/)
* Windows 10 / Windows 11 (with Modern Standby S0ix support recommended)

### Compiler Options
The mod requires the following Windows runtime libraries:
```
-lpowrprof -luser32 -lhid -lgdi32
```

### Installation
1. In Windhawk, navigate to **Mods** -> **Create Local Mod**.
2. Copy the contents of `windhawk-mod/screen-off-no-sleep.wh.cpp` into the editor.
3. Click **Compile and run**.

---

## 8. License

MIT License. See [LICENSE](LICENSE) for details.

---

## 9. References & Microsoft Technical Documentation

For deeper understanding of Windows power management and Modern Standby internals, refer to Microsoft's official hardware design guides:

* **[Modern Standby states](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby-states)**: Explains the lifecycle and state transitions within Modern Standby, detailing the *Screen Off* (No-CS) phase versus the *Sleep* phase, and how active power requests block deep sleep transitions.
* **[Prepare software for modern standby](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/prepare-software-for-modern-standby)**: Details the Desktop Activity Moderator (DAM), process suspension mechanics, and how audio streams suppress sleep entry.
* **[Network connectivity in Modern Standby](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby-network-connectivity)**: Covers network power policies, Wi-Fi sleep behavior, and why mobile hotspots stay alive during idle screen-off but terminate upon user-initiated sleep.
* **[Modern Standby firmware notifications](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby-firmware-notifications)**: Details ACPI `_DSM` notifications, display power notifications, and SleepStudy entry reasons (`Video Idle Timeout` vs `Screen Off Request`).
