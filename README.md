# NoSteamWebHelper

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

A small Windows DLL that automatically disables Steam's CEF (Chromium Embedded Framework) while you're in a game, and re-enables it when you're back at the menu — with a tray icon to see and override the state at a glance.

## Why

Steam used to ship a `-no-browser` launch parameter that disabled its embedded Chromium browser, cutting down on the RAM and CPU it uses in the background. Valve [removed it](https://steamcommunity.com/groups/SteamClientBeta/discussions/3/3710433479207750727/?ctp=42). NoSteamWebHelper restores that behavior automatically, without needing a permanent launch flag.

## How it works

- **If a game is running**, CEF is disabled and Steam's `steamwebhelper.exe` (the Chromium process backing Steam's UI) is shut down, freeing up its memory and CPU for your game.
- **If no game is running**, CEF is left enabled and Steam's browser UI behaves normally.
- Everything is driven by real state changes (registry notifications and Steam's own window events) — there's no polling, so the DLL uses effectively no CPU while idle.

You can also override this from the tray icon:

| Icon | Tray tooltip | Meaning |
|---|---|---|
| 🔵 blue window | CEF Enabled | Automatic, or manually forced on |
| 🔴 red window | CEF Disabled | Automatic, or manually forced off |

Right-click the tray icon for **Automatic** (the default — follows the current game state), **On** (always keep CEF enabled), or **Off** (always keep CEF disabled).

## Installation

1. Download `umpdc.dll` from the [Releases](../../releases) page (or [build it yourself](#building)).
2. Close Steam completely.
3. Copy `umpdc.dll` into your Steam installation folder — the same folder as `steam.exe`.
4. Launch Steam. A tray icon will appear once Steam finishes starting up.

> [!NOTE]
> Pass `-silent` to Steam's shortcut if you don't want its window to pop up automatically when CEF is re-enabled.

To uninstall, close Steam and delete `umpdc.dll` from the Steam folder.

## Building

Requires an x86-64 MinGW-w64 GCC toolchain targeting UCRT (no other dependencies — the source links directly against the Win32 API).

1. Install [MSYS2](https://www.msys2.org) and update it:

    ```bash
    pacman -Syu --noconfirm
    ```

2. Install GCC:

    ```bash
    pacman -S mingw-w64-ucrt-x86_64-gcc --noconfirm
    ```

3. From a UCRT64 shell, in the `src` directory:

    ```bash
    mkdir -p bin
    windres res/icon.rc -O coff -o bin/icon.res
    gcc -Oz -Wall -Wextra \
        -Wl,--gc-sections,--exclude-all-symbols,--dynamicbase,--nxcompat,--high-entropy-va \
        -municode -shared -nostdlib -s \
        Library.c bin/icon.res -lkernel32 -luser32 -ladvapi32 -lshell32 -lgdi32 -o bin/umpdc.dll
    ```

The result is `src/bin/umpdc.dll`.

## Project layout

- `src/Library.c` — the DLL itself.
- `src/res/icon.rc` — resource script embedding the tray icons and version info.
- `src/res/icon_on.ico`, `src/res/icon_off.ico` — tray icons for each state.
- `src/res/make_icon.py` — regenerates the two `.ico` files (requires Python + Pillow); only needed if you want to change the icon artwork.

## Credits

Originally created by [Aetopia](https://github.com/Aetopia) — [Aetopia/NoSteamWebHelper](https://github.com/Aetopia/NoSteamWebHelper). This repository is a maintained continuation of that project.

### Changes from the original

#### v1.0

- **Custom tray icon** reflecting live state (a blue window for enabled, a red "disabled" glyph for off) instead of the generic stock application icon, at every size from 16px up to 256px.
- **Decoupled the manual override from Steam's own state.** The original wrote directly into `HKCU\SOFTWARE\Valve\Steam\RunningAppID` — the same value Steam itself uses to track your running game — so toggling it manually could confuse Steam's own UI (friends status, playtime tracking). The override now lives in a private registry value instead, with a new **Automatic** menu option to return to following Steam's real state.
- **Fixed a 100%-CPU busy-loop**: if opening the Steam registry key ever failed, the original fell into a wait call on a null handle that fails instantly and reissues forever. It now bails out cleanly instead.
- **Fixed a silent state-reset bug**: the original menu used the same ID (`0`) for "On" and for "no selection," so dismissing the tray menu without picking anything silently forced CEF back on. Menu items now have distinct IDs.
- **Fixed a GDI handle leak**: the tray icon was reloaded from the module on every single state change without freeing the previous handle. Icons are now loaded once and cached.
- **Lighter process enumeration**: replaced `WTSEnumerateProcessesW` + a separate `NtQueryInformationProcess` parent-PID lookup with a single `CreateToolhelp32Snapshot` pass, dropping the `ntdll.dll` and `wtsapi32.dll` dependencies entirely.
- **Added an embedded VERSIONINFO resource**, so the DLL shows proper file/product details in Explorer's Properties dialog.
- Menu now shows a checkmark on the currently active state.

#### v1.1

- **Fixed a potential 100%-CPU spin on resume**: `ResumeThread`'s failure code `(DWORD)-1` compares greater than `1` unsigned, so the resume-drain loop would spin forever if the Steam thread handle ever went bad. The loop now breaks out explicitly on failure.
- **The registry watcher runs on its own thread** instead of looping forever inside the WinEvent hook callback. Blocking the hook callback stalled that thread's message pump, so no further window events could ever be delivered — meaning the watcher could never restart after a failure. Now the callback returns immediately and the watcher is restartable.
- **All event/notification setup is error-checked**: `CreateEventW` and every `RegNotifyChangeKeyValue` arm/re-arm are verified, so a failure shuts the watcher down cleanly (resuming Steam's thread) instead of waiting forever on an event that can no longer fire, or erroring in a tight loop.
- **`SuspendThread` failures are no longer recorded as a successful suspension**, keeping the suspend-count bookkeeping honest.
- **Tray menu opens on right-button *up*** (shell convention) and posts the documented `WM_NULL` after `TrackPopupMenu`, fixing the classic quirk where the second right-click on a tray menu doesn't open.
- **Build hardening**: DLL now links with `--dynamicbase`, `--high-entropy-va`, and `--nxcompat` (full ASLR + DEP), and compiles warning-clean under `-Wall -Wextra`.
- **CI**: GitHub Actions now builds the DLL on every push, publishes a SHA-256 checksum, and attaches both to releases on version tags.
- Internal cleanup: descriptive identifiers replace the `_`/`$` placeholder names (`$` in an identifier is a non-standard GCC extension), and the tray window class has a proper name instead of `" "`.

## License

[GPL-3.0](LICENSE).
