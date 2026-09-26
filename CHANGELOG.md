# Changelog

Notable changes per release. The section matching a tag becomes that release's
notes on the [Releases page](https://github.com/NobbyBo11ocks/SteamTrayWebHelper/releases),
so keep the heading exactly `## <version>`.

## Unreleased

### Fixed

- The DLL now only runs inside `steam.exe`. Windows searches an application's
  own folder for a DLL before System32, so any other program in Steam's folder
  that loads `umpdc.dll` - `GameOverlayUI.exe` and `steamerrorreporter.exe` live
  there too - would get this DLL as well, and nothing stopped it starting there.
  In a host that created a titled `vguiPopupWindow`, that meant a second tray
  icon and that program's UI thread suspended for as long as a game ran.
- The DLL can no longer be unloaded out from under its own threads. Its
  background thread runs for the life of the process, but nothing kept the DLL
  mapped if whatever loaded `umpdc.dll` freed it again - and the next time that
  thread woke, it would run code that was no longer there and crash Steam.
  Reproduced by loading and freeing the previous build; the module is now
  pinned before the thread starts.
- Setup no longer starts `steam.exe` with administrator rights when it asks a
  running Steam to close. Inno Setup's `Exec` runs a program with Setup's own
  elevated credentials; `ExecAsOriginalUser` is used instead. The uninstaller
  still uses `Exec`, the only option Inno Setup supports there.
- The README no longer says to run the installer as administrator. It already
  asks for elevation itself, and starting it with "Run as administrator" stops
  Setup from launching Steam as the signed-in user afterwards.

## 1.2.4

### Fixed

- Tray icon and menu font are rebuilt when Windows display scaling or icon
  metrics change. They were loaded once at startup, so changing DPI mid-session
  left a blurry, wrongly sized icon until Steam was restarted.

### Added

- The Ctrl+Alt+L hotkey can be remapped. `Hotkey` and `HotkeyModifiers` under
  `HKCU\SOFTWARE\NoSteamWebHelper` override the default, and a change takes
  effect immediately without restarting Steam.
- Release notes are now taken from this changelog instead of being left empty.

## 1.2.3

### Fixed

- **Closed a deadlock that could freeze Steam permanently.** The watcher
  suspended Steam's UI thread and then took a process snapshot to find
  `steamwebhelper.exe`. That snapshot allocates from the process heap, so if the
  suspended thread happened to be holding the heap lock, the allocation waited on
  a lock nothing could release. Process discovery now happens before the suspend;
  only the termination itself runs while the thread is stopped.
- **Recovered automatically when Steam rebuilds its UI thread.** The watcher held
  the thread handle it first saw for the whole session. If Steam replaced that
  thread, every suspend silently failed and the helper stopped working with no
  indication. It now waits on the thread handle and rebuilds against the
  replacement.
- **The tray tooltip no longer reports a mode that was never applied.** It was
  derived only from the stored override, so a rejected suspend still read
  "CEF Disabled" while CEF was plainly running. It now appends `(not applied)`
  when the requested state could not be reached.

### Added

- `toolchain.txt` ships with each release, recording the exact gcc, ld, windres
  and Pillow versions used. `ld` stamps its version into the PE header, so a
  different binutils changes the binary without changing an instruction; this
  makes a reproducibility check actionable rather than mysterious.

## 1.2.2

### Added

- **Reproducible builds.** Rebuilding a tag with the recorded toolchain now
  produces a byte-identical `umpdc.dll`, and therefore an identical installer.
  Previously every build differed: the DLL embedded a link timestamp and MinGW
  chose a fresh image base each time. ASLR is unaffected.
- **Signed build provenance.** Each released binary carries a SLSA attestation
  tying it to the commit and workflow run that produced it, verifiable with
  `gh attestation verify`.
- Dependabot keeps the workflow's actions current.
- An advisory `-fanalyzer` static analysis pass in CI.

## 1.2.1

### Fixed

- **Continuous integration, which had never passed.** The icon check compared
  generated `.ico` files byte-for-byte, but two Pillow builds of the same version
  emit different bytes for pixel-identical images. It now compares decoded
  pixels. No build, artifact or release had run since the check was introduced.
- **Broken installer download links.** Every link pointed at `Setup.exe`, which
  returned 404; the published asset is `SteamTrayWebHelper.exe`. The installer
  now builds under that name so the two cannot drift apart again.
- **The installer launched Steam elevated.** Setup runs as administrator, and
  without `runasoriginaluser` Steam inherited that token, which elevates every
  game it launches.
- Setup no longer pre-creates the override registry key, which under an elevated
  install landed in the administrator's hive rather than the Steam user's.
- The tray menu no longer re-applies its rounded region on every idle tick, which
  span a repaint cycle for as long as the menu was open.
- A failed `RegisterWindowMessageW` could make every `WM_NULL` re-add the tray
  icon.
- One early failure in the tray thread left it permanently disabled with no
  retry for the rest of the session.

### Added

- The installer is built and published by CI; releases no longer need a manual
  upload. Tags of the form `1.2.3` are recognised as well as `v1.2.3`.
- A `.sha256` file accompanies each released binary.

## 1.2.0

### Added

- Tray icon extracted from Steam's own `steam.exe` resources, recoloured black
  and red for enabled and disabled.
- Tray menu repainted with Steam's real client palette from `steam.styles`:
  gradient background, hover highlight, rounded corners, accent-blue active item.
- Global Ctrl+Alt+L hotkey to flip the forced On/Off override.
- Steam's main window no longer pops back up when CEF is restored automatically
  after a game exits.
- An Inno Setup installer with Steam-folder validation and an uninstaller.

### Fixed

- Process termination verifies the child's image actually lives in Steam's
  install directory before terminating it.

## 1.0.0

- Initial release: disables Steam's CEF WebHelper while a game is running and
  restores it afterwards, with a tray icon for manual control.
