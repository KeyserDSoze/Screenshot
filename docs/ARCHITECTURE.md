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

## What this proves

This route reaches the Windows graphics kernel and the currently active Intel/NVIDIA stack without installing a replacement display miniport.

It does **not** yet provide a documented API for reading the final physical HDMI/eDP scan-out pixel by pixel. That remains the next research step.

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
