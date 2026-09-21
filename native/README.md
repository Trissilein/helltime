# Helltime Native Win32 Preview

Minimal native baseline for the Win32 port. Uses C++20, Win32, Direct2D and DirectWrite only. No WebView, .NET or third-party runtime.

## Build

Run from a **Developer PowerShell for Visual Studio** (CMake finds installed Visual Studio/MSVC; no SDK or IDE install is performed):

```powershell
cmake -S native -B native/build -G "Visual Studio 17 2022" -A x64
cmake --build native/build --config Release
```

Output:

```text
native/build/bin/Release/helltime.exe
```

The build tree is separate from `dist/`; PDB files stay beside the build output and are not release artifacts.

## Current scope

The executable opens a resizable dark Helltime-like window and renders a title, event cards, and footer with Direct2D/DirectWrite. It is the rendering/windowing baseline only. Schedule logic, settings, tray, overlay, audio, TTS, persistence and full interaction remain later port tasks.
