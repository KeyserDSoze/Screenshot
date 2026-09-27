# Architecture

## From click to framebuffer

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

## Control device

After `DxgkInitializeDisplayOnlyDriver`, the lab saves the graphics stack's existing CREATE/CLOSE/DEVICE_CONTROL dispatch routines and creates:

```text
\Device\KernelScreenshot
\DosDevices\KernelScreenshot
\\.\KernelScreenshot
```

Our wrappers handle only the control device. Requests targeting dxgkrnl-owned display device objects are forwarded to the original routines.

## IOCTLs

`IOCTL_SCREENSHOT_QUERY` is METHOD_BUFFERED and returns width, height, pitch, output stride, pixel format and byte count.

`IOCTL_SCREENSHOT_CAPTURE` is METHOD_OUT_DIRECT. Windows pins the application's output pages and supplies an MDL; the driver maps the MDL with `MmGetSystemAddressForMdlSafe` and copies the framebuffer.

The C# process never receives a kernel or physical framebuffer address.

## Pixel address

For the supported 32-bpp mode:

```text
pixel(x,y) = FrameBuffer.Ptr + y * SourcePitch + x * 4
```

The screenshot copies only `Width * 4` bytes per row, preserving the distinction between framebuffer pitch and tightly packed output stride.

## Useful WinDbg breakpoints

```text
BddDdiStartDevice
BASIC_DISPLAY_DRIVER::StartDevice
BASIC_DISPLAY_DRIVER::SetSourceModeAndPath
MapFrameBuffer
ScreenshotDeviceControl
BASIC_DISPLAY_DRIVER::GetScreenshotInfo
BASIC_DISPLAY_DRIVER::CopyScreenshotFrame
```

Inspect `DispInfo.PhysicAddress`, `DispInfo.Pitch`, `DispInfo.Width`, `DispInfo.Height` and `FrameBuffer.Ptr`.

## Why this still is not the cable signal

This experiment reads the simple scan-out framebuffer model exposed to KMDOD. A modern GPU can additionally have overlay/cursor planes, color transforms, HDR processing, compression and vendor-specific display engine behavior. There is no universal public Windows kernel API named "read final HDMI/DisplayPort pixel".
