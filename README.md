# Kernel Screenshot Lab

[![Build third-party release](https://github.com/KeyserDSoze/Screenshot/actions/workflows/release.yml/badge.svg)](https://github.com/KeyserDSoze/Screenshot/actions/workflows/release.yml)

Educational Windows graphics project for studying the path from a C# application down to WDDM, `dxgkrnl`, and the active Intel/NVIDIA display drivers.

## Safe default path: D3DKMT

The default app mode no longer replaces the machine's display miniport. It launches a small native helper that calls the documented WDDM kernel-thunk APIs:

```text
C# console app
  -> D3DKMTProbe.exe
  -> D3DKMTEnumAdapters2 / D3DKMTQueryAdapterInfo
  -> Gdi32.dll
  -> dxgkrnl.sys
  -> active Intel/NVIDIA WDDM driver
```

The helper reports each WDDM adapter's LUID, present-source count, WDDM version, PCI location and adapter-type flags. The C# frontend then lets you select an adapter.

This mode does **not** install, replace, stop, or reconfigure the Intel/NVIDIA drivers.

## Build the safe probe

From the repository root:

```bat
msbuild Native\D3DKMTProbe\D3DKMTProbe.vcxproj /p:Configuration=Debug /p:Platform=x64
```

Then run:

```bat
dotnet run --project .\App\KernelScreenshot.App.csproj
```

Expected shape:

```text
Kernel Screenshot Lab - safe WDDM probe
No display driver replacement is performed in this mode.

WDDM adapters (D3DKMT -> dxgkrnl -> active vendor driver)

[0] Intel(R) UHD Graphics
    LUID=..., sources=..., WDDM=...
    PCI=...
    render=True, display=True, ...

[1] NVIDIA GeForce RTX 4060 Laptop GPU
    ...
```

After selecting an adapter, the app now binds DXGI/D3D11 to the same LUID, duplicates the first attached desktop output, copies the GPU texture into a CPU-readable staging texture, and writes one BMP under `Screenshots\wddm-*.bmp`.

```text
selected D3DKMT LUID
  -> matching IDXGIAdapter1
  -> D3D11CreateDevice
  -> IDXGIOutput1::DuplicateOutput
  -> AcquireNextFrame
  -> ID3D11Texture2D
  -> staging texture
  -> Map
  -> BGRA pixels
  -> BMP
```

The vendor Intel/NVIDIA miniport remains installed and active throughout this path.

## Why this direction

A modern Intel/NVIDIA GPU does not expose a universal CPU-readable linear "final screen framebuffer" to arbitrary kernel clients. Scan-out can involve tiled or compressed resources, multiplane overlays, cursor planes, color transforms, HDR and vendor-specific display-engine state.

D3DKMT is a documented low-level user-mode interface into the Windows graphics kernel. It allows us to inspect and target the adapters while keeping the vendor miniports active.

## Reusable DLL

The project now also builds `KernelScreenshot.dll`, a native x64 C ABI. The primary capture entry points are:

```text
KS_CaptureBmpAuto
KS_CaptureDisplayBmp
KS_CaptureBmp
KS_CaptureBmpStrict
KS_GetLastCaptureReportJson
```

The DLL returns screenshots as in-memory BMP bytes. The CLI exposes the same model: when `-out` is omitted, the complete BMP is written as raw binary bytes to stdout, so another process can consume it directly without creating a file.

The CLI also supports `-device auto`, and numeric `-device N` is treated as a preferred adapter with automatic fallback to the other adapters if the preferred one cannot capture. This makes HDMI connect/disconnect changes on hybrid laptops much more robust.

`-pipeline` prints the active Windows display paths as JSON and correlates CCD source/target timing with the lower-level D3DKMT adapter LUID, VidPN source ID, current KMT display mode and current Desktop Duplication client count. This is intended for diagnosing hybrid Intel/NVIDIA display ownership and scan-out configuration.

Because a DLL is not a command-line executable, `KernelScreenshotCli.exe` provides the shell equivalent:

```bat
KernelScreenshotCli.exe -list
KernelScreenshotCli.exe -pipeline
KernelScreenshotCli.exe -vendor-pipeline
KernelScreenshotCli.exe -screenshot -out shot.bmp
KernelScreenshotCli.exe -device auto -screenshot > shot.bmp
KernelScreenshotCli.exe -device 0 -screenshot -out shot.bmp
```

Build both DLL and CLI with:

```bat
msbuild Native\KernelScreenshotCli\KernelScreenshotCli.vcxproj /p:Configuration=Debug /p:Platform=x64
```

Both outputs are placed under:

```text
Native\bin\x64\Debug\
```

Third-party integration docs are:

- `docs/THIRD_PARTY_QUICKSTART.md` — start here for C# and other third-party integrations.
- `docs/DLL_API.md` — direct `KernelScreenshot.dll` ABI and buffer contract.
- `docs/EXE_API.md` — `KernelScreenshotCli.exe` commands, stdout/stderr contract, exit codes, and executable-location rules.

## Downloadable release

The repository includes:

```text
.github/workflows/release.yml
```

Every successful build of `main` is versioned automatically by GitHub Actions. No manual tag is required.

The workflow builds the x64 Release version of `KernelScreenshotCli.exe` and `KernelScreenshot.dll`, packages the public header and third-party documentation, uploads the workflow artifact, creates a new immutable build release, and refreshes the moving `latest` release.

The automatic version format is:

```text
v0.1.<GitHub Actions run number>
```

For example:

```text
KernelScreenshot v0.1.7
  KernelScreenshot-win-x64-v0.1.7.zip
```

At the same time the workflow recreates:

```text
KernelScreenshot latest
```

with the ZIP from the newest successful `main` build.

Therefore a third-party project can either pin a specific build release or always download the `latest` package. No local `git tag` or manual release step is required.

The package is designed so a third-party C# project can copy `KernelScreenshotCli.exe` and `KernelScreenshot.dll` next to its own executable and invoke the CLI by filename.

## Legacy KMDOD framebuffer experiment

The repository still contains the modified Microsoft KMDOD sample under `Driver/KMDOD`. That experiment reads a CPU-mapped VESA/UEFI-style framebuffer through:

```text
DeviceIoControl("\\.\KernelScreenshot")
  -> BASIC_DISPLAY_DRIVER::CopyScreenshotFrame
  -> CURRENT_BDD_MODE::FrameBuffer.Ptr
```

Run that old client path only with:

```bat
dotnet run --project .\App\KernelScreenshot.App.csproj -- --legacy-kmdod
```

Do not force the KMDOD sample onto a normal Intel/NVIDIA production adapter. A display miniport mismatch can black-screen or bugcheck the machine. Use KMDOD only in a suitable VM or disposable test machine with the framebuffer model the sample expects.

## Projects

```text
App/                         C# frontend
Native/D3DKMTProbe/          safe native D3DKMT adapter probe
Native/KernelScreenshotApi/  reusable DLL
Native/KernelScreenshotCli/  third-party CLI wrapper
Driver/KMDOD/                legacy experimental display-only miniport
docs/ARCHITECTURE.md         architecture notes
docs/THIRD_PARTY_QUICKSTART.md
docs/EXE_API.md
docs/DLL_API.md
```

## Current status

The user-mode capture path is complete for the current one-frame scope:

1. WDDM adapters and active display ownership are enumerated through D3DKMT and CCD.
2. `-pipeline` exposes source/target timing, adapter LUID, VidPN source ID and current display mode.
3. `-vendor-pipeline` adds Intel IGCL and NVIDIA NVAPI diagnostics when those vendor runtimes are installed.
4. Automatic capture selects active CCD/VidPN display owners first, then keeps a compatibility fallback across remaining adapters.
5. Desktop Duplication remains the primary capture backend; exact-monitor Windows Graphics Capture is used only as the final hybrid fallback.
6. The public DLL and CLI support both automatic owner-first capture and exact active-display capture by GDI name.
7. The release workflow publishes versioned and `latest` x64 packages from `main`.

Repeated/high-rate capture, cursor composition and advanced rotation/HDR conversion remain outside the current one-frame API and can be added later without changing the automatic owner-selection contract.

The KMDOD-derived files retain Microsoft's source headers. The upstream Windows-driver-samples license is copied under `THIRD_PARTY_LICENSES`.

### Vendor display probe

`-vendor-pipeline` optionally loads the Intel IGCL runtime already shipped with supported Intel graphics drivers (`ControlLib.dll`) and reports adapter/display timing, output type, mux type, display flags, feature flags and current wire color model/depth. NVIDIA NVAPI is also probed dynamically from the installed driver: physical GPUs, PCI/bus identity, connected display IDs, connection state, output type and current SDR/HDR output mode are reported when the interfaces are available. No driver is installed or replaced.

### Byte-stream capture

`-out` is optional. Without it, the executable writes the complete 32-bit BMP directly to stdout as binary bytes:

```bat
KernelScreenshotCli.exe -screenshot
```

A parent process can redirect/read stdout as a binary stream and receive the BMP entirely in memory. For example:

```bat
KernelScreenshotCli.exe -screenshot > screenshot.bmp
```

The same automatic CCD/VidPN owner selection is used whether the image goes to stdout or to `-out`. A capture report can be written separately while the BMP remains on stdout:

```bat
KernelScreenshotCli.exe -screenshot -report capture.json
```

In that form, stdout is BMP bytes, `capture.json` is the report, and stderr remains reserved for errors.

### Automatic capture selection

The recommended one-frame command is now:

```bat
KernelScreenshotCli.exe -screenshot -out screenshot.bmp
```

`-screenshot` with no `-device` is equivalent to `-device auto`. Auto mode queries the active Windows CCD/VidPN topology first, maps each active source adapter LUID back to the enumerated WDDM devices, and tries those active display owners before any remaining adapter fallback. This makes hybrid Intel/NVIDIA capture deterministic when DXGI enumeration disagrees with the physical display owner. Explicit `-device N` remains available for diagnostics and vendor-specific testing.

The DLL exposes the same behavior through `KS_CaptureBmpAuto`.

### Capture report

Any screenshot command can optionally write a JSON sidecar describing the path that actually succeeded:

```bat
KernelScreenshotCli.exe -screenshot -out screenshot.bmp -report capture.json
```

The report includes request mode (`auto`, `display`, or `device`), the actual adapter LUID/name, GDI display, VidPN source/target IDs, strict-adapter flag, backend (`DuplicateOutput1`, `DuplicateOutput`, or `WindowsGraphicsCapture`), route, dimensions and BMP byte count. The DLL equivalent is `KS_GetLastCaptureReportJson`.

For an end-user command guide, see `docs/CLI_GUIDE.md`.

### Strict adapter mode

Numeric `-device N` remains a **preferred adapter** by default: if that choice cannot capture, the CLI may try other adapters for compatibility. Add `-strict` when the selected adapter must be the only capture adapter:

```bat
KernelScreenshotCli.exe -device 0 -screenshot -strict
KernelScreenshotCli.exe -device 1 -screenshot -strict -out strict.bmp -report strict.json
```

Strict mode does not try other device indexes, does not perform a cross-adapter KMT ownership retry, and does not use the Windows Graphics Capture fallback. It succeeds only through Desktop Duplication (`DuplicateOutput1` or `DuplicateOutput`) on the selected adapter; otherwise it fails. `-strict` is valid only with a numeric `-device <index>`, not with `auto` or `-display`.

The DLL equivalent is `KS_CaptureBmpStrict`.

### Exact display capture

Multi-monitor callers can now select one active Windows display explicitly by its GDI name from `-pipeline`:

```bat
KernelScreenshotCli.exe -display DISPLAY1 -screenshot -out display1.bmp
KernelScreenshotCli.exe -display DISPLAY5 -screenshot -out display5.bmp
```

The short form `DISPLAYn` and the full `\\.\DISPLAYn` form are both accepted. The selected CCD/VidPN path determines the real owning adapter LUID. The engine then tries Desktop Duplication on that exact DXGI output; if the hybrid stack does not expose that output through DDA, it falls back only for that exact `HMONITOR` through Windows Graphics Capture. The DLL equivalent is `KS_CaptureDisplayBmp`.

### Hybrid capture ownership

The capture engine now treats the active CCD/VidPN source adapter LUID as the authoritative display owner when DXGI output enumeration disagrees on hybrid laptops. Desktop Duplication is still attempted first. If the selected adapter owns an active CCD path but DXGI exposes no duplicable output, the engine resolves the exact `\\.\DISPLAYn` source to its `HMONITOR` and uses the existing monitor-scoped Windows Graphics Capture backend only as the final fallback.
