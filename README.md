# Lite PDF Presentation

Ultra-fast, lightweight, and lag-free PDF presentation tool for Windows with multi-monitor / projector support.

## Features:
- 🚀 **Native C++ Performance**: Built with pure Win32 API and Google's Chromium PDFium engine.
- ⚡ **Ultra-Lightweight**: Only ~5-15 MB RAM usage, instantaneous startup (<0.1s), no heavy interpreter or runtime.
- 📺 **Multi-Monitor Detection**: Automatically targets your secondary display / projector in full screen.
- 🧑‍🏫 **Presenter Console**: Current slide preview, next slide preview, presentation timer, and slide status.
- 🎬 **Smooth Finish Flow**:
  - Advancing past the final slide transitions to a clean black screen.
  - Advancing once more exits the presentation.
- ⌨ **Keyboard Shortcuts**:
  - `F5`: Start presentation on selected display.
  - `Space` / `Right Arrow` / `Page Down`: Next slide.
  - `Left Arrow` / `Page Up`: Previous slide.
  - `B`: Toggle black screen.
  - `Esc`: Exit presentation.

## Usage:
1. **Run directly**: Double-click `run.bat` (or `Lite PDF Presentation.exe`)
2. **Build from source**: Double-click `build.bat` (compiles in ~2 seconds using MinGW g++)

## License:
Distributed under the MIT License. See [LICENSE](LICENSE) for more information.
