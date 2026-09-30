# KernelScreenshot CLI guide

This guide is for people who only need to run `KernelScreenshotCli.exe`. You do not need to understand D3DKMT, DXGI, VidPN, Intel IGCL, or NVIDIA NVAPI to use the executable.

## Installation / deployment

There is no driver to install.

Extract the release ZIP and keep these two files together in the same directory:

```text
KernelScreenshotCli.exe
KernelScreenshot.dll
```

Open Command Prompt or PowerShell in that directory and run the commands below.

The tool is Windows x64. It uses the graphics drivers already installed by Windows / Intel / NVIDIA. It does not replace them.

## Simplest command: automatic screenshot

For normal use:

```bat
KernelScreenshotCli.exe -screenshot -out screenshot.bmp
```

This is the recommended command.

You do not need to run `-list` first and you do not need to choose Intel or NVIDIA manually.

Automatic mode does the selection itself:

```text
active Windows display topology (CCD / VidPN)
        |
        v
find the adapter that really owns an active display
        |
        v
try Desktop Duplication on that owner
        |
        v
if hybrid DXGI ownership is inconsistent:
use the exact monitor-scoped WGC fallback
```

The compatibility spelling is:

```bat
KernelScreenshotCli.exe -device auto -screenshot -out screenshot.bmp
```

It has the same automatic behavior as plain `-screenshot`.

## Capture report

Add `-report <file.json>` to any screenshot command:

```bat
KernelScreenshotCli.exe -screenshot -out screenshot.bmp -report capture.json
```

The screenshot is written to `screenshot.bmp` and the JSON report is written to `capture.json`.

Example report shape:

```json
{
  "available": true,
  "request": {
    "mode": "auto",
    "requestedDeviceIndex": null,
    "requestedDisplayName": null,
    "autoCandidateKind": "activeCcdOwner"
  },
  "result": {
    "attemptedDeviceIndex": 0,
    "adapter": {
      "name": "Intel(R) UHD Graphics",
      "luidHighPart": 0,
      "luidLowPart": 88627
    },
    "display": {
      "gdiDeviceName": "\\\\.\\DISPLAY1",
      "vidPnSourceId": 0,
      "targetId": 8388688
    },
    "backend": "WindowsGraphicsCapture",
    "route": "ccd-monitor-fallback",
    "width": 2560,
    "height": 1600,
    "bmpBytes": 16384054
  }
}
```

| Field | Meaning |
| --- | --- |
| `request.mode` | `auto`, `display`, or `device` |
| `autoCandidateKind` | whether auto succeeded on an active CCD owner or on a compatibility fallback adapter |
| `adapter.name` / LUID | GPU that actually owns the captured display path |
| `gdiDeviceName` | Windows display source such as `\\.\DISPLAY1` |
| `vidPnSourceId` / `targetId` | Windows display-path identifiers |
| `backend` | `DuplicateOutput1`, `DuplicateOutput`, or `WindowsGraphicsCapture` |
| `route` | exact path used, for example `dxgi-output`, `kmt-owner-retry`, or `ccd-monitor-fallback` |
| `width` / `height` | captured frame dimensions |

The report is optional. If `-report` is omitted, screenshot behavior is unchanged.

## Two or more monitors: capture one exact display

First inspect the active Windows display paths:

```bat
KernelScreenshotCli.exe -pipeline
```

Look for `source.gdiDeviceName`, for example `\\.\DISPLAY1` and `\\.\DISPLAY5`.

Then select the one you want:

```bat
KernelScreenshotCli.exe -display DISPLAY1 -screenshot -out internal.bmp -report internal.json
```

```bat
KernelScreenshotCli.exe -display DISPLAY5 -screenshot -out external.bmp -report external.json
```

Both `DISPLAY1` and `\\.\DISPLAY1` are accepted.

Exact-display mode never silently switches to another monitor. CCD/VidPN identifies the owner for that display, Desktop Duplication is tried on that exact output, and WGC can only fall back to the same `HMONITOR`.

## Diagnostic commands

| Command | Purpose |
| --- | --- |
| `KernelScreenshotCli.exe -list` | Lists WDDM adapters and DXGI outputs |
| `KernelScreenshotCli.exe -pipeline` | Shows active Windows CCD / D3DKMT / VidPN display paths |
| `KernelScreenshotCli.exe -vendor-pipeline` | Adds Intel IGCL and NVIDIA NVAPI display diagnostics |
| `KernelScreenshotCli.exe -device 0 -screenshot ...` | Prefers one adapter index; mainly useful for diagnostics |
| `KernelScreenshotCli.exe -device auto -screenshot ...` | Explicit spelling of automatic owner-first mode |
| `KernelScreenshotCli.exe -display DISPLAY1 -screenshot ...` | Captures one exact active monitor |

Do not assume that adapter 0 is always Intel or adapter 1 is always NVIDIA. On hybrid laptops, the render GPU and the physical scan-out owner can be different.

## BMP on stdout instead of a file

Without `-out`, screenshot bytes go to stdout:

```bat
KernelScreenshotCli.exe -screenshot > screenshot.bmp
```

You can still write a report at the same time:

```bat
KernelScreenshotCli.exe -screenshot -report capture.json > screenshot.bmp
```

Programs that redirect stdout must read it as binary data. In C#, drain stdout while the process is running; do not wait for process exit before reading a large BMP pipe.

## Exit codes and errors

Exit code `0` means the command succeeded.

Errors are written to stderr. A caller should always check the process exit code before using the BMP or JSON report.

The report file is only produced when the screenshot itself succeeds and the report can be retrieved.

## Recommended commands

Single-monitor or "just take a screenshot":

```bat
KernelScreenshotCli.exe -screenshot -out screenshot.bmp -report capture.json
```

Multi-monitor, select one display:

```bat
KernelScreenshotCli.exe -pipeline
```

```bat
KernelScreenshotCli.exe -display DISPLAY1 -screenshot -out display1.bmp -report display1.json
```

Low-level troubleshooting:

```bat
KernelScreenshotCli.exe -list
```

```bat
KernelScreenshotCli.exe -pipeline
```

```bat
KernelScreenshotCli.exe -vendor-pipeline
```
