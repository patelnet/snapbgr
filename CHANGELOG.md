# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[SemVer](https://semver.org/).

## [1.4.0] - 2026-07-04

### Changed
- **App renamed to SnapBGR** (was BackgroundRemover): new executable name
  (`SnapBGR.exe`), product name, Start Menu entry, install folders, and
  repository (`patelnet/snapbgr` — old links redirect).
- Settings and drop-in models now live in `%LOCALAPPDATA%\SnapBGR`. On
  first run, existing settings and models are migrated automatically from
  the old `%LOCALAPPDATA%\BackgroundRemover` folder (the old folder is
  left untouched).
- Release assets now carry the version in the filename
  (`SnapBGR-<version>.msi`, `SnapBGR-<version>-portable.zip`); CI names
  the MSI from the version in `Product.wxs`.
- README restructured as a product-first document: highlights,
  installation, getting started, AI models, privacy, and FAQ up front;
  developer/build documentation moved to a dedicated section.

### Upgrade note
- The MSI upgrades existing BackgroundRemover installs in place (same
  upgrade code). Old versions' entries below retain the historical
  BackgroundRemover naming.

## [1.3.0] - 2026-07-04

### Added
- Installer: **install scope choice** — the setup wizard (now
  WixUI_Advanced) asks whether to install *just for you* (no elevation,
  `%LOCALAPPDATA%\Programs`) or *for all users* (elevated,
  `Program Files`), in addition to the install-location page.

## [1.2.0] - 2026-07-04

### Added
- **Any model filename works** — no more renaming to `modnet.onnx`. The
  app scans for `*.onnx` in `%LOCALAPPDATA%\BackgroundRemover\models\`
  and next to the EXE, in addition to the explicitly selected model.
- **Multi-family model support**: preprocessing (input resolution,
  normalization, sigmoid postprocess) now auto-adapts per model family —
  MODNet, U²-Net/U²-Netp/Silueta (320×320, ImageNet norm), IS-Net/DIS and
  RMBG-1.4 (1024×1024), BiRefNet and RMBG-2.0 (1024×1024 + sigmoid),
  InSPyReNet (1280×1024). The input size is also read from the ONNX graph
  when statically declared.
- **Model download catalog**: `models/README.md` (linked from the tray's
  *Get Compatible Models*) rewritten as a full catalog of 15+ verified
  background-removal ONNX models with direct download links, file sizes,
  licenses (non-commercial models flagged), and per-family recipes.
- Tray status now shows the detected model family and resolution next to
  the model name.

## [1.1.1] - 2026-07-04

### Changed
- Setup wizard now uses branded artwork (banner and welcome/finish
  background generated from the app icon) instead of the default WiX
  images.
- All links updated for the repository rename to `rmbg-tray`.

## [1.1.0] - 2026-07-04

### Added
- Tray menu: **Select Watched Folder…** and **Select Output Folder…**
  (modern folder pickers; the watcher restarts on the new folder
  automatically).
- Tray menu: live **status header** — watch state, file currently being
  processed, queue depth, processed/failed counts, and the active model.
- Tray menu: **Select Model…** to pick an `.onnx` matting model (persisted
  in settings, hot-reloaded) and **Get Compatible Models** linking to the
  model download & license guide.
- Installer: setup wizard with a **choose-install-location** page
  (WixUI_InstallDir) and license agreement.
- New app icon, embedded in the EXE and used for the tray icon.

## [1.0.3] - 2026-07-03

### Fixed
- The MSI now packages a **real system-tray application** (`rmbg_tray`)
  instead of the console smoke-test stand-in. Installing and launching
  BackgroundRemover now shows a tray icon, watches `Pictures` out of the
  box, and writes transparent PNGs to `Pictures\BackgroundRemoved`.
- MSI `Package Version` was stuck at 1.0.0, so upgrades silently kept the
  old files installed. Bumped to 1.0.3 — uninstall any previous version
  once, then future upgrades work via MajorUpgrade.

### Added
- `rmbg_tray` CMake target: Win32 message-loop app reusing TrayController,
  DirectoryWatcher, BackgroundRemovalService, and Settings (no WinUI
  dependency, so it builds in CI).

## [1.0.2] - 2026-07-03

### Fixed
- Installer now deploys the Visual C++ runtime app-locally (`msvcp140.dll`,
  `vcruntime140*.dll`, etc.), fixing "MSVCP140.dll / VCRUNTIME140.dll was
  not found" on machines without the VC++ Redistributable installed.
- CI actions bumped to Node 24 majors (checkout v7, cache v6,
  upload-artifact v7), silencing the Node 20 deprecation warning.

## [1.0.1] - 2026-07-03

### Fixed
- Installer now bundles all runtime DLLs (OpenCV, ONNX Runtime, and image
  codec dependencies) alongside the executable, fixing
  "opencv_imgproc4.dll was not found" on machines without a vcpkg build
  tree. CI artifacts likewise include the DLLs so the standalone
  executable is runnable.

## [1.0.0] - 2026-07-02

### Added
- Background-removal pipeline (`BackgroundRemovalService`): OpenCV
  preprocess (512×512, RGB, CHW float32), ONNX Runtime inference
  (MODNet-style 1×3×512×512 → 1×1×512×512), postprocess to BGRA PNG with
  soft alpha edges.
- Deterministic synthetic-mask fallback when no model is present, keeping
  the full pipeline testable without shipping model binaries.
- Directory watching via overlapped `ReadDirectoryChangesW`
  (`DirectoryWatcher`) with safe stop semantics.
- WinUI 3 (Windows App SDK 2.2.0) app: NavigationView shell, HomePage
  (start/stop, status, log, drag-and-drop), SettingsPage.
- System tray icon with Start/Stop/Open Output Folder/Exit menu, tooltip
  status, and balloon notifications (`TrayController`).
- JSON settings persisted to `%LOCALAPPDATA%\BackgroundRemover\settings.json`
  (`Settings`), loaded at startup and saved on change.
- Safe output naming: `<name>_nobg_<timestamp>.png`, never overwrites.
- Build tooling: vcpkg manifest (onnxruntime 1.23.2, opencv 4.12.0,
  nlohmann-json 3.12.0, vcpkg 2026.06.24), CMake core + console test,
  `build.ps1` bootstrap script.
- WiX 5.0.2 per-user MSI installer (no elevation, Start Menu shortcut,
  Add/Remove Programs entry, major-upgrade support).
- GitHub Actions CI: pinned-vcpkg build, synthetic-fallback smoke test,
  MSI packaging, artifact upload, optional code signing.

### Notes
- No third-party model binaries are included; see `models/README.md` for
  download and license guidance.
- The MSI packages the console test binary as a stand-in executable; the
  packaged WinUI app is built from Visual Studio (see README).

[1.1.1]: https://github.com/patelnet/rmbg-tray/releases/tag/v1.1.1
[1.1.0]: https://github.com/patelnet/rmbg-tray/releases/tag/v1.1.0
[1.0.3]: https://github.com/patelnet/rmbg-tray/releases/tag/v1.0.3
[1.0.2]: https://github.com/patelnet/rmbg-tray/releases/tag/v1.0.2
[1.0.1]: https://github.com/patelnet/rmbg-tray/releases/tag/v1.0.1
[1.0.0]: https://github.com/patelnet/rmbg-tray/releases/tag/v1.0.0
