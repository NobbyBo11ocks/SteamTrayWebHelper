<div align="center">

# SteamTrayWebHelper

### Automatically reduce Steam WebHelper activity while you play

A lightweight native Windows utility that disables Steam's Chromium-based WebHelper when a game is running, restores it when you return to Steam, and keeps manual control available from the system tray.

[![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4?logo=windows&logoColor=white)](#requirements)
[![Architecture](https://img.shields.io/badge/Architecture-x86--64-555555)](#requirements)
[![Language](https://img.shields.io/badge/Native-C-00599C?logo=c&logoColor=white)](#building-from-source)
[![License](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](LICENSE)

<br>

<a href="https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest/download/Setup.exe">
  <img src="https://img.shields.io/badge/Download-Setup.exe-2ea44f?style=for-the-badge&logo=windows&logoColor=white" alt="Download Setup.exe">
</a>
&nbsp;
<a href="https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest/download/umpdc.dll">
  <img src="https://img.shields.io/badge/Manual_Download-umpdc.dll-0969da?style=for-the-badge" alt="Download umpdc.dll">
</a>

<br><br>

**Use the installer for the easiest setup, or download the DLL separately for a completely manual installation.**

</div>

---

## What it does

Steam uses several `steamwebhelper.exe` processes to power its modern Chromium-based interface. Those processes can continue using memory and CPU while a game is running.

SteamTrayWebHelper manages that behaviour automatically:

- **Game running:** disables CEF and closes Steam WebHelper processes.
- **No game running:** restores CEF so Steam's full interface is available again.
- **Manual control:** choose **Automatic**, **On**, or **Off** from the tray menu.
- **Event-driven operation:** reacts to Steam and Windows state changes without a constant polling loop.
- **Easy recovery:** remove one DLL to return Steam to its normal behaviour.

> [!IMPORTANT]
> SteamTrayWebHelper is an independent community project. It is not affiliated with, endorsed by, or supported by Valve Corporation. Steam client updates may change internal behaviour.

---

## Download and installation

Both installation methods are provided as separate files on the [Releases page](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest).

| Download | Recommended for | Description |
|---|---|---|
| [`Setup.exe`](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest/download/Setup.exe) | Most users | Detects the Steam directory, closes Steam when required, installs `umpdc.dll`, and provides an uninstaller. |
| [`umpdc.dll`](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest/download/umpdc.dll) | Advanced or portable use | Manual installation with one file placed beside `steam.exe`. |

### Installer — recommended

1. Download [`Setup.exe`](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest/download/Setup.exe).
2. Run the installer as administrator.
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

The status artwork uses transparent backgrounds, allowing the icons to remain clean in both light and dark Windows notification areas.

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

Right-click the tray icon to choose a mode:

| Mode | Behaviour |
|---|---|
| **Automatic** | Recommended. Follows Steam's real game-running state. |
| **On** | Forces CEF and Steam WebHelper to remain enabled. |
| **Off** | Forces CEF and Steam WebHelper to remain disabled. |

The active mode is marked with a checkmark in the tray menu.

---

## Why this version is a major improvement

SteamTrayWebHelper is an independent continuation of [Aetopia's NoSteamWebHelper](https://github.com/Aetopia/NoSteamWebHelper), preserving the original concept while expanding it into a more complete and user-friendly utility.

Aetopia's repository was archived on **10 February 2026** and is marked deprecated. This version modernises the interface, separates its manual override from Steam's own state, improves internal event handling, and provides both installer-based and manual downloads.

| Area | Aetopia's version | SteamTrayWebHelper |
|---|---|---|
| Project status | Archived and deprecated | Maintained continuation |
| Installation | Manual DLL installation | Separate `Setup.exe` and `umpdc.dll` downloads |
| Tray appearance | Generic Windows application icon | Live CEF enabled/disabled status icons |
| Tray menu | Basic **On** and **Off** choices | **Automatic**, **On**, and **Off**, with active-state checkmark |
| Menu dismissal | A dismissed menu could still write an override value | State changes only after an explicit selection |
| Steam registry use | Manual override reused Steam's `RunningAppID` value | Manual override is stored in a private project registry key |
| Menu design | Standard popup menu | Dark owner-drawn tray menu |
| Event architecture | Registry watching occurred inside the WinEvent callback path | Dedicated registry watcher thread keeps event delivery responsive |
| Process detection | WTS enumeration with native process queries | Single Toolhelp snapshot pass with fewer runtime dependencies |
| Build hardening | Compact release build | Explicit ASLR, high-entropy VA, DEP/NX compatibility, and stripped output |
| Distribution | DLL-focused releases | Installer and DLL supplied separately for user choice |

### Additional improvements

- Custom enabled and disabled tray icons with transparent backgrounds.
- Automatic mode that follows Steam's actual `RunningAppID` state.
- A private override value that does not overwrite Steam's own game-running value.
- Unique command IDs so closing the tray menu cannot silently change modes.
- State-aware tooltips that clearly show whether CEF is enabled or disabled.
- Automatic tray-icon recovery after Windows Explorer recreates the taskbar.
- Native Win32 implementation with no separate runtime or background service.
- An installer with Steam-folder validation and a built-in uninstaller.

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
Get-FileHash .\Setup.exe -Algorithm SHA256
```

Compare the result with the SHA-256 value shown for the matching file on the [latest release page](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest).

---

## Troubleshooting

### The tray icon does not appear

- Confirm `umpdc.dll` is in the exact same directory as `steam.exe`.
- Fully exit Steam and launch it again.
- Check Windows' hidden notification-area icons.
- Remove any older copy of NoSteamWebHelper before reinstalling.

### Steam's interface does not return

Right-click the tray icon and select **On**. Alternatively, close Steam, remove `umpdc.dll`, and start Steam again.

Adding `-silent` to a Steam shortcut can prevent the main Steam window from opening automatically when CEF is restored.

"C:\Program Files (x86)\Steam\Steam.exe"  -silent

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
  -municode -shared -nostdlib -s \
  Library.c bin/icon.res \
  -lkernel32 -luser32 -ladvapi32 -lshell32 -lgdi32 \
  -o bin/umpdc.dll
```

The generated DLL will be located at:

```text
src/bin/umpdc.dll
```

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
│   └── NoSteamWebHelper.iss
├── src/
│   ├── Library.c
│   └── res/
│       ├── icon.rc
│       ├── icon_on.ico
│       ├── icon_off.ico
│       └── make_icon.py
├── LICENSE
└── README.md
```

`src/bin/` and `installer/Output/` are generated build-output directories.

---

## Credits

The original NoSteamWebHelper concept and implementation were created by [Aetopia](https://github.com/Aetopia) in [Aetopia/NoSteamWebHelper](https://github.com/Aetopia/NoSteamWebHelper).

SteamTrayWebHelper retains the core idea while adding expanded tray controls, custom status visuals, safer override storage, revised event handling, hardened build flags, and installer/manual distribution options.

---

## License

Distributed under the [GNU General Public License v3.0](LICENSE).

<div align="center">

### Reduce unnecessary Steam background activity. Keep control one click away.

[**Download Setup.exe**](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest/download/Setup.exe) · [**Download umpdc.dll**](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases/latest/download/umpdc.dll) · [**View releases**](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases)

</div>
