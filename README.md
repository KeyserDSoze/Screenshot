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

The project now also builds `KernelScreenshot.dll`, a native x64 C ABI that exposes:

```text
KS_ListDevicesJson
KS_CaptureBmp
KS_GetLastErrorMessage
```

`KS_ListDevicesJson` returns UTF-8 JSON describing the WDDM adapters and their outputs, including how many outputs are currently attached to the Windows desktop. `KS_CaptureBmp(deviceIndex, ...)` returns the screenshot as an in-memory BMP byte array.

The CLI also supports `-device auto`, and numeric `-device N` is treated as a preferred adapter with automatic fallback to the other adapters if the preferred one cannot capture. This makes HDMI connect/disconnect changes on hybrid laptops much more robust.

Because a DLL is not a command-line executable, `KernelScreenshotCli.exe` provides the shell equivalent:

```bat
KernelScreenshotCli.exe -list
KernelScreenshotCli.exe -device 0 -screenshot > shot.bmp
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

## Current objective

1. Enumerate and select a real WDDM adapter safely.
2. Inspect documented adapter/driver properties through D3DKMT.
3. Capture one frame from the selected adapter through Desktop Duplication and CPU staging readback. **Implemented.**
4. Add output selection, repeated capture, pointer/rotation handling and timing diagnostics.
5. Compare that path with the legacy KMDOD physical-framebuffer experiment.

The KMDOD-derived files retain Microsoft's source headers. The upstream Windows-driver-samples license is copied under `THIRD_PARTY_LICENSES`.
