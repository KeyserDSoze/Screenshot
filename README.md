# Kernel Screenshot Lab

Windows educational project that captures the display from the CPU-mapped framebuffer owned by a WDDM kernel-mode display-only miniport driver.

## Capture path

```text
left click
  -> C# console app
  -> DeviceIoControl("\\.\KernelScreenshot")
  -> kernel control device
  -> BASIC_DISPLAY_DRIVER::CopyScreenshotFrame
  -> CURRENT_BDD_MODE::FrameBuffer.Ptr
  -> PNG
```

No GDI `GetPixel`, `BitBlt`, Desktop Duplication, Windows Graphics Capture, or D3D readback is used in the screenshot path.

The base driver is Microsoft's KMDOD sample. Its framebuffer mapping ultimately uses `MmMapIoSpaceEx` on the physical framebuffer exposed to the display miniport.

## Important limitation

This is not a generic way to read the final scan-out of any NVIDIA/AMD/Intel WDDM driver. The modified KMDOD must be the active display driver on a suitable VESA/UEFI-style test adapter where a CPU-accessible linear framebuffer exists.

Modern vendor drivers can use tiled/compressed allocations, overlays, cursor planes, HDR/color transforms and vendor-specific display-engine state.

## Use a VM or disposable test machine

A display miniport bug can black-screen or bugcheck Windows. Do not start on your primary machine.

## Requirements

- Windows 11 test VM/machine
- Visual Studio 2022
- WDK 11
- .NET 8 SDK
- WinDbg recommended

## Build

Open `Screenshot.sln` and build **Debug | x64**.

Driver: `Driver/KMDOD/Sample/SampleDisplay.vcxproj`  
Client: `App/KernelScreenshot.App.csproj`

The driver output name is `KernelScreenshotDisplay.sys`.

## Install

Follow the upstream Microsoft KMDOD test-driver procedure, including test signing, on the test machine:

https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/kernel-mode-display-only-miniport-driver-kmdod-sample/

After the modified KMDOD is active, run the C# app elevated:

```powershell
dotnet run --project .\App\KernelScreenshot.App.csproj
```

Every global left-click creates:

```text
.\Screenshots\kernel-YYYYMMDD-HHMMSS-fff.png
```

Ctrl+C exits.

## What the kernel actually reads

For each row:

```cpp
source = FrameBuffer.Ptr + y * SourcePitch;
destination = output + y * (Width * 4);
RtlCopyMemory(destination, source, Width * 4);
```

The current version supports the 32-bpp KMDOD framebuffer and intentionally does not synchronize the read to vertical blank/present, so tearing is possible.

See `docs/ARCHITECTURE.md` for the full path and the WinDbg checkpoints.

The KMDOD-derived files retain Microsoft's source headers. The upstream Windows-driver-samples license is copied under `THIRD_PARTY_LICENSES`.
