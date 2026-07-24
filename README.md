<div align="center">

<img src="docs/images/cef-enabled.png" width="112" alt="SteamTrayWebHelper icon">

# SteamTrayWebHelper

### A lighter, smarter way to manage Steam WebHelper while gaming

Automatically disables Steam's Chromium-based WebHelper when a game is running, restores it when you return to Steam, and gives you complete control from a clear system-tray interface.

[![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4?logo=windows&logoColor=white)](#requirements)
[![Architecture](https://img.shields.io/badge/Architecture-x86--64-555555)](#requirements)
[![License](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](LICENSE)
[![Open Source](https://img.shields.io/badge/Open%20Source-Yes-success)](#building-from-source)

## [Download the latest release](../../releases/latest)

**Choose the installer `.exe` for the easiest setup, or download `umpdc.dll` separately for a completely manual installation.**

</div>

---

## What it does

Steam uses multiple `steamwebhelper.exe` processes to power its modern interface. Those Chromium-based processes can continue consuming memory and CPU while a game is running.

SteamTrayWebHelper automatically manages that behaviour:

- **Game running:** disables CEF and closes Steam WebHelper processes.
- **No game running:** restores CEF so the Steam interface works normally.
- **Manual control:** switch between **Automatic**, **On**, and **Off** from the tray menu.
- **Event-driven operation:** reacts to Steam and Windows state changes without constant background polling.

The result is a small, native Windows utility focused on reducing unnecessary Steam background activity during gameplay without permanently breaking access to the Steam client.

> [!IMPORTANT]
> SteamTrayWebHelper is not affiliated with, endorsed by, or supported by Valve Corporation. Steam updates can change internal client behaviour, so always keep a copy of the latest release available.

---

## Download options

Both installation methods are provided as **separate assets on the Releases page**.

| Download | Best for | What it does |
|---|---|---|
| **Installer `.exe`** | Most users | Detects your Steam folder, closes Steam when required, installs the DLL, and provides an uninstaller. |
| **`umpdc.dll`** | Advanced and portable users | Lets you install the project manually by placing one file beside `steam.exe`. |

### Recommended: installer

1. Open the [latest release](../../releases/latest).
2. Download the installer `.exe`.
3. Run it as administrator.
4. Confirm your Steam installation directory.
5. Start Steam normally.

### Manual installation

1. Open the [latest release](../../releases/latest).
2. Download **`umpdc.dll`**.
3. Exit Steam completely, including its notification-area icon.
4. Copy `umpdc.dll` into the folder containing `steam.exe`.
5. Launch Steam.

The default Steam location is usually:

```text
C:\Program Files (x86)\Steam
```

To uninstall manually, close Steam and delete `umpdc.dll` from the Steam directory.

---

## Tray icons and controls

The included icon artwork uses **transparent backgrounds**, so it remains clean in both light and dark Windows notification areas.

<table>
  <tr>
    <th width="150">Tray icon</th>
    <th>Status</th>
    <th>Meaning</th>
  </tr>
  <tr>
    <td align="center"><img src="docs/images/cef-enabled.png" width="82" alt="CEF enabled icon"></td>
    <td><strong>CEF Enabled</strong></td>
    <td>Steam WebHelper is available and Steam's full browser-based interface can run normally.</td>
  </tr>
  <tr>
    <td align="center"><img src="docs/images/cef-disabled.png" width="82" alt="CEF disabled icon"></td>
    <td><strong>CEF Disabled</strong></td>
    <td>Steam WebHelper is disabled to reduce unnecessary background activity while gaming.</td>
  </tr>
</table>

Right-click the tray icon to select:

| Mode | Behaviour |
|---|---|
| **Automatic** | Recommended. Follows Steam's real game-running state. |
| **On** | Forces CEF and Steam WebHelper to remain enabled. |
| **Off** | Forces CEF and Steam WebHelper to remain disabled. |

The active mode is marked directly in the tray menu.

---

## Why this version is significantly improved

SteamTrayWebHelper is a modern continuation of [Aetopia's NoSteamWebHelper](https://github.com/Aetopia/NoSteamWebHelper), not a simple rename or repack.

The original repository is now **archived, read-only, and marked deprecated**. This version keeps the useful core idea while addressing reliability issues, improving control, modernising the user experience, and adding a proper release workflow.

| Area | Aetopia version | SteamTrayWebHelper |
|---|---|---|
| Project status | Archived and deprecated | Actively improved continuation |
| Downloads | Manual DLL-focused installation | Separate installer `.exe` and manual `umpdc.dll` assets |
| Tray appearance | Generic application icon | Purpose-built live status icons with transparent backgrounds |
| User controls | Basic manual toggle | **Automatic**, **On**, and **Off** modes with active-state checkmark |
| Registry handling | Manual override reused Steam's own `RunningAppID` value | Override stored in a private project registry key, avoiding interference with Steam state |
| Menu reliability | Dismissing the menu could silently change state | Unique command IDs prevent accidental state changes |
| CPU failure handling | Registry and resume failures could cause tight loops | Failure paths are checked and exit cleanly |
| Resource management | Tray icons could leak GDI handles | Icons are loaded once and reused |
| Event handling | Watcher logic could block event delivery | Dedicated, restartable registry watcher thread |
| Process detection | Heavier multi-API enumeration | Single Toolhelp snapshot pass with fewer dependencies |
| Explorer restarts | Tray icon could disappear | Icon is restored after taskbar/Explorer recreation |
| Binary metadata | Minimal file information | Embedded product and version information |
| Build security | Basic linker configuration | ASLR, high-entropy VA, DEP/NX compatibility, stripped release binary |
| Release verification | No automated checksum workflow | CI-built DLL plus SHA-256 checksum on tagged releases |

### Reliability fixes included

- Fixed a possible **100% CPU loop** when the Steam registry key cannot be opened.
- Fixed a second possible **100% CPU loop** if resuming Steam's thread fails.
- Fixed accidental mode changes when the tray menu is dismissed without a selection.
- Fixed tray icon GDI handle leakage across repeated state changes.
- Added validation for events and registry notification setup.
- Prevented failed thread suspensions from being recorded as successful.
- Moved the registry watcher out of the WinEvent callback so event delivery remains responsive.
- Corrected notification-area menu behaviour using right-button-up handling and the documented `WM_NULL` follow-up.
- Added automatic tray-icon recovery after Windows Explorer restarts.

---

## Design goals

- **Small:** one native DLL for manual installation.
- **Low overhead:** no runtime framework and no constant polling loop.
- **Transparent:** open-source C implementation with a reproducible build process.
- **Controllable:** automatic operation with immediate manual override.
- **Recoverable:** remove one DLL to return Steam to its normal state.
- **Verifiable:** tagged releases include a SHA-256 checksum for the DLL.

---

## Requirements

- Windows 10 or Windows 11
- 64-bit Steam client
- Administrator permission when installing into `Program Files`

No separate runtime, service, account, or configuration application is required.

---

## Troubleshooting

### The tray icon does not appear

- Confirm `umpdc.dll` is in the exact same directory as `steam.exe`.
- Fully exit Steam, then launch it again.
- Check Windows' hidden notification-area icons.
- Remove any older copy of the original NoSteamWebHelper DLL before reinstalling.

### Steam's interface does not return

Right-click the tray icon and select **On**, or close Steam, remove `umpdc.dll`, and start Steam again.

Adding `-silent` to your Steam shortcut can prevent Steam's main window from opening automatically when CEF is restored.

### Antivirus warning

This project loads a DLL from the Steam installation directory and controls Steam processes, which can resemble behaviour used by unwanted software. Review the source, build it yourself, and verify the release checksum when uncertain.

### Steam updated and the tool stopped working

Steam client updates can change implementation details. Remove the DLL to restore normal Steam operation, then check the [Releases](../../releases) page for an updated build.

---

## Verify the manual DLL

Tagged releases include `umpdc.dll.sha256` alongside `umpdc.dll`.

In PowerShell:

```powershell
Get-FileHash .\umpdc.dll -Algorithm SHA256
```

Compare the result with the value supplied in the release checksum file.

---

## Building from source

The project uses the native Win32 API and an x86-64 MinGW-w64 UCRT toolchain.

### 1. Install MSYS2

Download and install [MSYS2](https://www.msys2.org/), then update it:

```bash
pacman -Syu --noconfirm
```

### 2. Install the compiler

Open an **MSYS2 UCRT64** terminal and run:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc --noconfirm
```

### 3. Build the DLL

From the repository's `src` directory:

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

The compiled file will be created at:

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
│   ├── bin/
│   │   └── umpdc.dll
│   └── res/
│       ├── icon.rc
│       ├── icon_on.ico
│       ├── icon_off.ico
│       └── make_icon.py
├── LICENSE
└── README.md
```

---

## Credits

The original concept and implementation were created by [Aetopia](https://github.com/Aetopia) in [Aetopia/NoSteamWebHelper](https://github.com/Aetopia/NoSteamWebHelper).

SteamTrayWebHelper preserves that project's core idea while providing substantial reliability fixes, improved state handling, custom status visuals, hardened builds, automated releases, checksums, and both installer-based and manual distribution options.

---

## License

Distributed under the [GNU General Public License v3.0](LICENSE).

<div align="center">

**Reduce unnecessary Steam background activity. Keep control one click away.**

[Download the latest release](../../releases/latest)

</div>
