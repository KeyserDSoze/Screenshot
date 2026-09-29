# Kernel Screenshot Lab

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
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" ^
  Native\D3DKMTProbe\D3DKMTProbe.vcxproj ^
  /p:Configuration=Debug ^
  /p:Platform=x64
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

Selecting an adapter currently proves the WDDM communication path only. Pixel readback is the next stage.

## Why this direction

A modern Intel/NVIDIA GPU does not expose a universal CPU-readable linear "final screen framebuffer" to arbitrary kernel clients. Scan-out can involve tiled or compressed resources, multiplane overlays, cursor planes, color transforms, HDR and vendor-specific display-engine state.

D3DKMT is a documented low-level user-mode interface into the Windows graphics kernel. It allows us to inspect and target the adapters while keeping the vendor miniports active.

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
Driver/KMDOD/                legacy experimental display-only miniport
docs/ARCHITECTURE.md         architecture notes
```

## Current objective

1. Enumerate and select a real WDDM adapter safely.
2. Inspect documented adapter/driver properties through D3DKMT.
3. Add a readback/capture path bound to the selected adapter without replacing its vendor driver.
4. Compare that path with the legacy KMDOD physical-framebuffer experiment.

The KMDOD-derived files retain Microsoft's source headers. The upstream Windows-driver-samples license is copied under `THIRD_PARTY_LICENSES`.
