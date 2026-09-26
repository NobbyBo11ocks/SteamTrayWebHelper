<div align="center">

# SteamTrayWebHelper

### Automatically reduce Steam WebHelper activity while you play

*A native Windows companion for Steam - built to look and feel like it ships with the client.*

A lightweight native Windows utility that disables Steam's Chromium-based WebHelper when a game is running, restores it when you return to Steam, and keeps manual control one right-click away - styled with Steam's own icon and its own dark UI palette, down to the pixel.

[![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4?logo=windows&logoColor=white)](#requirements)
[![Architecture](https://img.shields.io/badge/Architecture-x86--64-555555)](#requirements)
[![Language](https://img.shields.io/badge/Native-C-00599C?logo=c&logoColor=white)](#building-from-source)
[![Theme](https://img.shields.io/badge/Theme-Steam-1b2838?logo=steam&logoColor=66c0f4)](#tray-icons-and-controls)
[![License](https://img.shields.io/badge/License-GPL--3.0-66c0f4.svg)](LICENSE)
[![Build](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/actions/workflows/build.yml/badge.svg)](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/actions/workflows/build.yml)

<br>

<a href="https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest/download/SteamTrayWebHelper.exe">
  <img src="https://img.shields.io/badge/Download-SteamTrayWebHelper.exe-2ea44f?style=for-the-badge&logo=windows&logoColor=white" alt="Download SteamTrayWebHelper.exe">
</a>
&nbsp;
<a href="https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest/download/umpdc.dll">
  <img src="https://img.shields.io/badge/Manual_Download-umpdc.dll-1b2838?style=for-the-badge&logo=steam&logoColor=66c0f4" alt="Download umpdc.dll">
</a>

<br><br>

**Use the installer for the easiest setup, or download the DLL separately for a completely manual installation.**

</div>

---

## At a glance

Steam uses several `steamwebhelper.exe` processes to power its modern Chromium-based interface. Those processes keep using memory and CPU even while you're heads-down in a game and never looking at Steam's UI. SteamTrayWebHelper watches for that and reacts instantly:

| | |
|---|---|
| 🎮 **Game running** | CEF is disabled and Steam WebHelper processes are closed. |
| 🏠 **Back at Steam** | CEF is restored automatically - full interface, no restart needed. |
| 🖱️ **Manual control** | Right-click the tray icon, or hit **Ctrl+Alt+L**, to force **On** or **Off** yourself. |
| ⚡ **Event-driven** | Reacts to Steam and Windows state changes directly - no polling loop, no idle CPU use. |
| ↩️ **Fully reversible** | Remove one DLL and Steam is back to stock behaviour, no trace left behind. |

> [!IMPORTANT]
> SteamTrayWebHelper is an independent community project. It is not affiliated with, endorsed by, or supported by Valve Corporation. Steam client updates may change internal behaviour.

---

## Download and installation

Both installation methods are provided as separate files on the [Releases page](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest).

| Download | Recommended for | Description |
|---|---|---|
| [`SteamTrayWebHelper.exe`](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest/download/SteamTrayWebHelper.exe) | Most users | Detects the Steam directory, closes Steam when required, installs `umpdc.dll`, and provides an uninstaller. |
| [`umpdc.dll`](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest/download/umpdc.dll) | Advanced or portable use | Manual installation with one file placed beside `steam.exe`. |

### Installer — recommended

1. Download [`SteamTrayWebHelper.exe`](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest/download/SteamTrayWebHelper.exe).
2. Run the installer by double-clicking it. It asks for administrator permission by itself, because it writes into Steam's folder. Avoid right-click → **Run as administrator**: that leaves Setup unable to start Steam as you afterwards, so a Steam it launches would run elevated.
3. Confirm the folder containing `steam.exe`.
4. Complete installation and start Steam normally.

### Manual installation

1. Download [`umpdc.dll`](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest/download/umpdc.dll).
2. Exit Steam completely, including its notification-area icon.
3. Copy `umpdc.dll` into the same folder as `steam.exe`.
4. Launch Steam.

The default Steam folder is usually:

```text
C:\Program Files (x86)\Steam
```

To uninstall manually, fully close Steam and delete `umpdc.dll` from the Steam directory.

---

## Tray icons and controls

The tray icon isn't inspired by Steam's - it *is* Steam's. The glyph is extracted directly from Valve's own `steam.exe` (matching its exact proportions and anti-aliasing, not a redrawn approximation), recoloured to signal status at a glance: black for running normally, red for stepped in and disabled. Both keep transparent backgrounds, so they stay clean in light and dark notification areas alike.

<table>
  <tr>
    <th width="140">Tray icon</th>
    <th width="150">Status</th>
    <th>Meaning</th>
  </tr>
  <tr>
    <td align="center"><img src="docs/images/cef-enabled.png" width="72" alt="CEF enabled icon"></td>
    <td><strong>CEF Enabled</strong></td>
    <td>Steam WebHelper is available and Steam's complete browser-based interface can run normally.</td>
  </tr>
  <tr>
    <td align="center"><img src="docs/images/cef-disabled.png" width="72" alt="CEF disabled icon"></td>
    <td><strong>CEF Disabled</strong></td>
    <td>Steam WebHelper is disabled to reduce unnecessary background activity while gaming.</td>
  </tr>
</table>

Right-click the tray icon to choose a mode. The menu itself is painted with Steam's own palette - pulled straight from the client's own `steam.styles`, not approximated: the same background gradient, the same hover highlight, the same rounded corners, the same accent blue on the active mode.

| Mode | Behaviour |
|---|---|
| **Automatic** | Recommended. Follows Steam's real game-running state. |
| **On** | Forces CEF and Steam WebHelper to remain enabled. |
| **Off** | Forces CEF and Steam WebHelper to remain disabled. |

The active mode is marked with a checkmark in the tray menu.

**Hotkey:** press <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>L</kbd> anywhere to flip CEF between forced **On** and forced **Off** - the same override the tray menu writes, so it's reflected in the menu's checkmark immediately. If another application has already claimed that combination, the hotkey silently has no effect; the tray menu still works normally.

<details>
<summary><strong>Remapping the hotkey</strong></summary>

<br>

Set two values under `HKCU\SOFTWARE\NoSteamWebHelper` and the change takes effect immediately - no Steam restart:

| Value | Type | Meaning |
|---|---|---|
| `Hotkey` | `DWORD` | [Virtual-key code](https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes) of the key, e.g. `0x4C` for `L`, `0x77` for `F8` |
| `HotkeyModifiers` | `DWORD` | Sum of `1` Alt, `2` Ctrl, `4` Shift, `8` Win |

For <kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>F8</kbd>, set `Hotkey` to `0x77` and `HotkeyModifiers` to `6` (`2` + `4`).

At least one modifier is required, so a stray value can't bind a bare keypress system-wide. Anything missing or unusable falls back to <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>L</kbd>, and key auto-repeat never fires the toggle more than once per press.

</details>

---

## Why this version is a major improvement

SteamTrayWebHelper is an independent continuation of [Aetopia's NoSteamWebHelper](https://github.com/Aetopia/NoSteamWebHelper), preserving the original concept while expanding it into a more complete, more polished, and more Steam-native utility.

Aetopia's repository was archived on **10 February 2026** and is marked deprecated. This version modernises the interface, separates its manual override from Steam's own state, improves internal event handling, and provides both installer-based and manual downloads.

| Area | Aetopia's version | SteamTrayWebHelper |
|---|---|---|
| Project status | Archived and deprecated | Maintained continuation |
| Installation | Manual DLL installation | Separate `SteamTrayWebHelper.exe` installer and `umpdc.dll` downloads |
| Tray appearance | Generic Windows application icon | Steam's own icon glyph, extracted from `steam.exe` and recoloured by state |
| Tray menu styling | Standard popup menu | Owner-drawn menu using Steam's real palette, gradient, and rounded corners |
| Tray menu options | Basic **On** and **Off** choices | **Automatic**, **On**, and **Off**, with active-state checkmark |
| Menu dismissal | A dismissed menu could still write an override value | State changes only after an explicit selection |
| Steam registry use | Manual override reused Steam's `RunningAppID` value | Manual override is stored in a private project registry key |
| Event architecture | Registry watching occurred inside the WinEvent callback path | Dedicated registry watcher thread keeps event delivery responsive |
| Process detection | WTS enumeration with native process queries | Single Toolhelp snapshot pass, verified against Steam's own install path |
| Build hardening | Compact release build | Explicit ASLR, high-entropy VA, DEP/NX compatibility, and stripped output |
| Distribution | DLL-focused releases | Installer and DLL supplied separately for user choice |

### Additional improvements

- Tray icon shape extracted directly from Steam's own `steam.exe` resources, not a third-party approximation.
- Tray menu recoloured with Steam's actual client palette - background gradient, hover highlight, divider, and accent blue all sourced from `steam.styles`.
- Rounded tray-menu corners, matching Steam's own menu styling.
- Automatic mode that follows Steam's actual `RunningAppID` state.
- A private override value that does not overwrite Steam's own game-running value.
- Unique command IDs so closing the tray menu cannot silently change modes.
- State-aware tooltips that clearly show whether CEF is enabled or disabled, and say so plainly (`(not applied)`) on the rare occasion Steam rejects the change, rather than reporting a mode that never took effect.
- Automatic tray-icon recovery after Windows Explorer recreates the taskbar.
- Native Win32 implementation with no separate runtime or background service.
- An installer with Steam-folder validation and a built-in uninstaller.
- A global Ctrl+Alt+L hotkey to flip the forced On/Off override without opening the tray menu.
- Suppresses Steam's main window popping back up when CEF auto-restores after a game exits.

> [!NOTE]
> Full credit for the original idea and implementation goes to [Aetopia](https://github.com/Aetopia). SteamTrayWebHelper builds on that GPL-licensed foundation with substantial interface, distribution, and internal implementation changes.

---

## Requirements

- Windows 10 or Windows 11
- 64-bit Steam client
- Administrator permission when installing into `Program Files`

No separate runtime, account, service, or configuration application is required.

---

## Verify a download

GitHub displays a SHA-256 digest beside each uploaded release asset. You can calculate the downloaded file's digest in PowerShell:

```powershell
Get-FileHash .\umpdc.dll -Algorithm SHA256
```

For the installer:

```powershell
Get-FileHash .\SteamTrayWebHelper.exe -Algorithm SHA256
```

Compare the result with the SHA-256 value shown for the matching file on the [latest release page](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest).

Each release also carries a matching `.sha256` file (`umpdc.dll.sha256`, `SteamTrayWebHelper.exe.sha256`), written by the build workflow on the runner that produced the binary. Download it next to the file and check both at once:

```powershell
Get-Content .\umpdc.dll.sha256
```

### Build provenance

Every released binary carries a signed [SLSA build provenance](https://slsa.dev/) attestation tying it to the exact commit and workflow run that produced it. With the [GitHub CLI](https://cli.github.com/) installed:

```powershell
gh attestation verify .\umpdc.dll --repo NobbyBo11ocks/SteamTrayWebHelper
```

That answers a stronger question than a checksum does. A digest only proves the file matches what the release page lists; the attestation proves it was built by this repository's workflow from a specific commit, and was not uploaded by hand.

### Reproduce the build yourself

The build is deterministic: rebuilding the same commit with the same toolchain produces a **byte-identical** `umpdc.dll`, and therefore a byte-identical `SteamTrayWebHelper.exe` as well, since the installer just bundles that DLL. Check out the tag matching the release, follow [Building from source](#building-from-source), and compare:

```powershell
Get-FileHash .\src\bin\umpdc.dll -Algorithm SHA256
```

The digest should match the published `umpdc.dll.sha256` exactly. If it does, the release binary provably contains nothing that isn't in this repository.

This requires the **same toolchain** the release was built with. `ld` stamps its own version into the PE header, so a different binutils changes the file — and its checksum — without changing a single instruction. Every release ships a `toolchain.txt` listing exactly what produced it:

```text
gcc:     gcc.exe (Rev5, Built by MSYS2 project) 16.1.0
ld:      GNU ld (GNU Binutils) 2.46.1
windres: GNU windres (GNU Binutils) 2.46.1
```

If your digest differs by only a byte or two, compare that file first — a binutils mismatch is the usual cause, and it is not a sign that anything is wrong with the binary.

---

## Troubleshooting

### The tray icon does not appear

- Confirm `umpdc.dll` is in the exact same directory as `steam.exe`.
- Fully exit Steam and launch it again.
- Check Windows' hidden notification-area icons.
- Remove any older copy of NoSteamWebHelper before reinstalling.

### Steam's interface does not return

Right-click the tray icon and select **On**. Alternatively, close Steam, remove `umpdc.dll`, and start Steam again.

### Steam's main window pops up when you exit a game

When CEF is restored automatically (a game just closed, mode is **Automatic**), SteamTrayWebHelper suppresses the first window Steam tries to show for about 8 seconds afterwards - the same effect `-silent` has at launch, applied to this mid-session restore instead. It only applies to *automatic* restores; picking **On** yourself always lets Steam's window through, since you asked for it. If Steam is unusually slow to rebuild its UI after a game exits, that window can close before Steam gets there, and the main window will still appear once.

Adding `-silent` to a Steam shortcut additionally stops the main window from opening at Steam's own startup, which the above doesn't cover:

```text
"C:\Program Files (x86)\Steam\Steam.exe"  -silent
```

### Antivirus warning

The project loads a DLL from the Steam installation directory and controls Steam child processes. Some security products may flag that behaviour heuristically. Review the source code, build it yourself, and compare the downloaded file's SHA-256 digest with the value displayed by GitHub.

### A Steam update caused problems

Close Steam, remove `umpdc.dll` to restore normal operation, and check the [Releases page](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases) for an updated build.

---

## Building from source

The project uses the Win32 API and an x86-64 MinGW-w64 UCRT toolchain.

### 1. Install MSYS2

Install [MSYS2](https://www.msys2.org/) and update it:

```bash
pacman -Syu --noconfirm
```

### 2. Install the compiler

Open an **MSYS2 UCRT64** terminal:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc --noconfirm
```

### 3. Build the DLL

Run these commands from the repository's `src` directory:

```bash
mkdir -p bin
windres res/icon.rc -O coff -o bin/icon.res
gcc -Oz -Wall -Wextra -Werror \
  -Wl,--gc-sections,--exclude-all-symbols,--dynamicbase,--nxcompat,--high-entropy-va \
  -Wl,--no-insert-timestamp,--image-base,0x180000000 \
  -municode -shared -nostdlib -s \
  Library.c bin/icon.res \
  -lkernel32 -luser32 -ladvapi32 -lshell32 -lgdi32 \
  -o bin/umpdc.dll
```

The last two linker flags make the build **reproducible**: without them the DLL embeds a link timestamp and MinGW picks a fresh image base on every link, so two builds of identical source never match. Pinning the image base does not weaken ASLR — `--dynamicbase` is still set, so Windows still randomises the real load address; only the preferred base recorded in the file is fixed.

The generated DLL will be located at:

```text
src/bin/umpdc.dll
```

### 4. (Optional) Regenerate the tray icons

The icons are generated, not hand-drawn, from a Steam icon asset already checked into the repo:

```bash
cd src/res
pip install pillow numpy
python make_icon.py
```

This reproduces `icon_on.ico` and `icon_off.ico` byte-for-byte from `steam_logo_source.png` - see [Tray icons and controls](#tray-icons-and-controls) for where that source comes from.

### 5. (Optional) Build the installer

Requires [Inno Setup 6](https://jrsoftware.org/isinfo.php). With `src/bin/umpdc.dll` already built:

```text
iscc installer\NoSteamWebHelper.iss
```

The installer is written to `installer\Output\SteamTrayWebHelper.exe`. It bundles whichever `src\bin\umpdc.dll` is on disk at that moment, so build the DLL first.

---

## Project structure

```text
SteamTrayWebHelper/
├── .github/
│   └── workflows/
│       └── build.yml
├── docs/
│   └── images/
│       ├── cef-enabled.png
│       └── cef-disabled.png
├── installer/
│   ├── NoSteamWebHelper.iss
│   ├── make_wizard_images.py
│   ├── wizard_banner.bmp
│   └── wizard_small.bmp
├── src/
│   ├── Library.c
│   └── res/
│       ├── icon.rc
│       ├── icon_on.ico
│       ├── icon_off.ico
│       ├── make_icon.py
│       └── steam_logo_source.png
├── CHANGELOG.md
├── LICENSE
└── README.md
```

`src/bin/` and `installer/Output/` are generated build-output directories.

---

## Credits

The original NoSteamWebHelper concept and implementation were created by [Aetopia](https://github.com/Aetopia) in [Aetopia/NoSteamWebHelper](https://github.com/Aetopia/NoSteamWebHelper).

SteamTrayWebHelper retains the core idea while adding expanded tray controls, Steam-authentic visuals, safer override storage, revised event handling, hardened build flags, and installer/manual distribution options.

Steam, the Steam logo, and the Steam client's visual style are trademarks of Valve Corporation. This project reads styling information from a locally installed Steam client purely to match its own look and feel; it ships no Valve assets or code.

---

## License

Distributed under the [GNU General Public License v3.0](LICENSE).

<div align="center">

### Reduce unnecessary Steam background activity. Keep control one click away.

[**Download SteamTrayWebHelper.exe**](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest/download/SteamTrayWebHelper.exe) · [**Download umpdc.dll**](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest/download/umpdc.dll) · [**View releases**](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases)

</div>
