# Architecture

## Safe path used by default

```text
C# frontend
    |
    | starts native probe
    v
D3DKMTProbe.exe
    |
    | D3DKMTEnumAdapters2
    | D3DKMTQueryAdapterInfo
    v
Gdi32.dll / Windows graphics kernel thunk
    |
    v
dxgkrnl.sys
    |
    +---- Intel KMD
    |
    +---- NVIDIA KMD
```

The native helper uses documented D3DKMT calls and leaves the installed vendor drivers in place.

For each returned `D3DKMT_ADAPTERINFO`, the probe currently queries:

- `KMTQAITYPE_ADAPTERREGISTRYINFO`
- `KMTQAITYPE_DRIVERVERSION`
- `KMTQAITYPE_ADAPTERADDRESS`
- `KMTQAITYPE_ADAPTERTYPE`

It then closes every D3DKMT adapter handle with `D3DKMTCloseAdapter`.

The C# side parses the helper's JSON output and lets the user select an adapter by index/LUID. The LUID is the identity we can carry into the next capture/readback stage.

## Selected-adapter pixel readback

After the C# frontend selects a D3DKMT adapter, it passes the adapter LUID back to the native helper.

The helper then follows this path:

```text
selected D3DKMT LUID
    |
    v
CreateDXGIFactory1
    |
    v
IDXGIFactory1::EnumAdapters1
    |
    | match DXGI_ADAPTER_DESC1::AdapterLuid
    v
IDXGIAdapter1
    |
    v
D3D11CreateDevice
    |
    v
IDXGIAdapter::EnumOutputs
    |
    v
IDXGIOutput1::DuplicateOutput
    |
    v
IDXGIOutputDuplication::AcquireNextFrame
    |
    v
ID3D11Texture2D (desktop image)
    |
    | CopyResource
    v
D3D11_USAGE_STAGING texture
    |
    | ID3D11DeviceContext::Map
    v
CPU-readable BGRA bytes
    |
    v
BMP file
```

This is a real pixel readback path on the active Intel/NVIDIA WDDM stack. The vendor display miniport remains installed and active.

The current implementation captures the first attached desktop output on the selected adapter and writes a single BMP. It does not yet apply output rotation to the saved image, merge multiple outputs, or continuously capture frames.

## What this proves

The D3DKMT phase proves which real WDDM adapter we selected and gives us its stable LUID.

The DXGI/D3D11 phase then binds to that exact adapter and copies one desktop frame from a GPU resource into CPU-readable memory without replacing the Intel/NVIDIA miniport.

This still is not a raw read of a universal physical "final cable framebuffer". Desktop Duplication is a WDDM/DXGI capture interface that exposes a desktop image resource managed by Windows and the active graphics stack.

## Legacy KMDOD experiment

The original learning path is still present and is now explicitly opt-in:

```text
C# WH_MOUSE_LL hook
       |
       v
CreateFile("\\.\KernelScreenshot")
DeviceIoControl
       |
------- user/kernel boundary -------
       |
       v
I/O Manager
       |
       v
ScreenshotDeviceControl
       |
       v
BASIC_DISPLAY_DRIVER::CopyScreenshotFrame
       |
       v
m_CurrentModes[0].FrameBuffer.Ptr
       |
       v
CPU mapping of physical display framebuffer
```

The upstream KMDOD obtains display information through `dxgkrnl`. When a mode is activated it maps `DispInfo.PhysicAddress`; the sample's `MapFrameBuffer` uses `MmMapIoSpaceEx`.

This model is appropriate only when the target adapter exposes the simple POST/VESA/UEFI framebuffer model expected by KMDOD. It must not be treated as a generic Intel/NVIDIA scan-out reader.

## Legacy control device

After `DxgkInitializeDisplayOnlyDriver`, the lab creates:

```text
\Device\KernelScreenshot
\DosDevices\KernelScreenshot
\\.\KernelScreenshot
```

`IOCTL_SCREENSHOT_QUERY` is METHOD_BUFFERED.

`IOCTL_SCREENSHOT_CAPTURE` is METHOD_OUT_DIRECT. Windows pins the application's output pages and supplies an MDL; the driver maps it with `MmGetSystemAddressForMdlSafe` and copies from the KMDOD framebuffer.

For the supported 32-bpp legacy mode:

```text
pixel(x,y) = FrameBuffer.Ptr + y * SourcePitch + x * 4
```

## Why the two paths are different

KMDOD owns a deliberately simple display adapter and can map a firmware-style physical framebuffer.

A normal Intel/NVIDIA WDDM driver can use GPU virtual memory, tiled/compressed allocations, overlays, hardware cursors, color processing and vendor-specific scan-out state. Therefore there is no universal public kernel routine equivalent to `GetFinalMonitorFramebuffer()`.

The safe D3DKMT path is the right starting point for studying the real machine. The KMDOD path remains useful as a controlled experiment for understanding the older/simple framebuffer model.
