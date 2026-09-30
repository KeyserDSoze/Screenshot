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

## Output modes: bytes or file

There are two equivalent ways to receive the screenshot.

**1. Receive the BMP as bytes on stdout** — omit `-out`:

```bat
KernelScreenshotCli.exe -screenshot
```

A program that launches the EXE should read `stdout` as a **binary stream**. The bytes already contain the complete BMP file header plus pixel data; there is no JSON wrapper and no Base64 conversion.

**2. Ask the EXE to write the BMP file** — use `-out`:

```bat
KernelScreenshotCli.exe -screenshot -out screenshot.bmp
```

Both commands use exactly the same automatic capture logic. `-out` changes only where the resulting BMP bytes are written.

You can combine byte-stream output with a separate JSON report:

```bat
KernelScreenshotCli.exe -screenshot -report capture.json
```

In that case:

```text
stdout       = raw BMP bytes
capture.json = capture report
stderr       = errors only
```

## Simplest command: automatic screenshot

For normal use, choose either byte-stream output:

```bat
KernelScreenshotCli.exe -screenshot
```

or direct file output:

```bat
KernelScreenshotCli.exe -screenshot -out screenshot.bmp
```

Both are recommended. The first is usually better for programmatic integration; the second is convenient for shell/manual use.

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
| `request.strictAdapter` | `true` when the capture was requested through strict numeric-device mode |
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

## Preferred device vs strict device

A numeric device without `-strict` is a **preference**, not a hard lock:

```bat
KernelScreenshotCli.exe -device 0 -screenshot
```

The CLI tries device 0 first, but may fall back to another adapter if needed.

To require only that adapter:

```bat
KernelScreenshotCli.exe -device 0 -screenshot -strict
```

Strict mode means:

```text
selected device only
  -> DuplicateOutput1 / DuplicateOutput only
  -> no other device index
  -> no cross-adapter KMT ownership retry
  -> no Windows Graphics Capture fallback
  -> fail if the selected adapter cannot capture
```

It can still return bytes directly:

```bat
KernelScreenshotCli.exe -device 0 -screenshot -strict
```

or write a file and report:

```bat
KernelScreenshotCli.exe -device 0 -screenshot -strict -out strict.bmp -report strict.json
```

`-strict` is intentionally rejected with `-device auto`, plain `-screenshot`, or `-display DISPLAYn` because those modes have different selection semantics.

## Diagnostic commands

| Command | Purpose |
| --- | --- |
| `KernelScreenshotCli.exe -list` | Lists WDDM adapters and DXGI outputs |
| `KernelScreenshotCli.exe -pipeline` | Shows active Windows CCD / D3DKMT / VidPN display paths |
| `KernelScreenshotCli.exe -vendor-pipeline` | Adds Intel IGCL and NVIDIA NVAPI display diagnostics |
| `KernelScreenshotCli.exe -device 0 -screenshot ...` | Prefers one adapter index, with fallback if needed |
| `KernelScreenshotCli.exe -device 0 -screenshot -strict ...` | Uses only that adapter's Desktop Duplication path; fails instead of falling back |
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
