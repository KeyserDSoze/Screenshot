# KernelScreenshot.dll API

`KernelScreenshot.dll` is the reusable native x64 API for adapter discovery and one-frame desktop capture.

The DLL exports a C ABI so it can be consumed from C, C++, C#, Rust, Python ctypes, and other FFI-capable runtimes. Microsoft recommends C linkage for C++ DLL exports when the functions need to be easy to consume from other languages.

## Exports

```c
int __cdecl KS_ListDevicesJson(
    char* buffer,
    uint32_t* bufferBytes);

int __cdecl KS_ListDisplayPipelinesJson(
    char* buffer,
    uint32_t* bufferBytes);

int __cdecl KS_ListVendorPipelinesJson(
    char* buffer,
    uint32_t* bufferBytes);

int __cdecl KS_CaptureBmp(
    uint32_t deviceIndex,
    uint8_t* buffer,
    uint32_t* bufferBytes);

int __cdecl KS_CaptureBmpAuto(
    uint8_t* buffer,
    uint32_t* bufferBytes);

int __cdecl KS_CaptureDisplayBmp(
    const char* displayName,
    uint8_t* buffer,
    uint32_t* bufferBytes);

int __cdecl KS_GetLastCaptureReportJson(
    char* buffer,
    uint32_t* bufferBytes);

int __cdecl KS_GetLastErrorMessage(
    char* buffer,
    uint32_t* bufferBytes);
```

All functions return one of the `KS_STATUS` values declared in `KernelScreenshotApi.h`.

`KS_CaptureBmpAuto` is the recommended one-frame capture entry point for callers that do not need to force a specific adapter.

## Buffer pattern

The caller owns memory.

First call with a null data pointer:

```c
uint32_t bytes = 0;
KS_ListDevicesJson(NULL, &bytes);
```

Allocate `bytes`, then call again:

```c
char* json = malloc(bytes);
KS_ListDevicesJson(json, &bytes);
```

The same pattern is used for screenshots.

## Device list

`KS_ListDevicesJson` returns UTF-8 JSON.

Example shape:

```json
[
  {
    "index": 0,
    "name": "NVIDIA GeForce RTX 4060 Laptop GPU",
    "luidHighPart": 0,
    "luidLowPart": 16855171,
    "sources": 4,
    "wddm": "3.2",
    "pci": {
      "bus": 1,
      "device": 0,
      "function": 0
    },
    "type": {
      "renderSupported": true,
      "displaySupported": true,
      "softwareDevice": false,
      "postDevice": false,
      "hybridDiscrete": true,
      "hybridIntegrated": false,
      "indirectDisplayDevice": false,
      "paravirtualized": false
    },
    "outputCount": 1,
    "attachedOutputCount": 1,
    "hasAttachedDesktopOutput": true,
    "outputs": [
      {
        "index": 0,
        "name": "\\\\.\\DISPLAY1",
        "attachedToDesktop": true,
        "desktopLeft": 0,
        "desktopTop": 0,
        "desktopRight": 1920,
        "desktopBottom": 1080,
        "rotation": 1
      }
    ]
  }
]
```

The `index` field is the value passed to `KS_CaptureBmp`.

`outputCount` is the number of DXGI outputs enumerated on the adapter. `attachedOutputCount` counts only outputs currently attached to the Windows desktop, and `hasAttachedDesktopOutput` is true when that count is non-zero.

An adapter can remain present in the device list even when it has no currently attached desktop output, so callers should not use adapter presence alone as the test for screenshot eligibility.

## Screenshot

### Recommended: automatic owner selection

`KS_CaptureBmpAuto(...)` queries the active CCD/VidPN topology, maps active source adapter LUIDs to WDDM devices, tries those active display owners first, and only then considers remaining adapters as a compatibility fallback. Desktop Duplication stays the primary backend; the exact monitor-scoped Windows Graphics Capture path is used only when hybrid ownership prevents DDA from exposing the active output.

Example:

```c
uint32_t bytes = 0;
int status = KS_CaptureBmpAuto(NULL, &bytes);

uint8_t* bmp = malloc(bytes);
status = KS_CaptureBmpAuto(bmp, &bytes);

// bmp[0..bytes-1] is a complete BMP file.
```

The two calls are paired on the same thread: the DLL captures once during the size query and retains that frame for the following copy call, avoiding a second wait for a new desktop present.

### Exact active display

`KS_CaptureDisplayBmp(displayName, ...)` captures one exact active GDI display such as `DISPLAY1` or `\\.\DISPLAY1`. The function looks up that display in the active CCD/VidPN topology, uses its adapter LUID as the owner, attempts Desktop Duplication on that exact output, and falls back only to the matching `HMONITOR` through Windows Graphics Capture.

```c
uint32_t bytes = 0;
int status = KS_CaptureDisplayBmp("DISPLAY1", NULL, &bytes);

uint8_t* bmp = malloc(bytes);
status = KS_CaptureDisplayBmp("DISPLAY1", bmp, &bytes);
```

The same two-call cached-frame contract used by `KS_CaptureBmpAuto` applies here.

### Explicit adapter selection

`KS_CaptureBmp(deviceIndex, ...)` keeps explicit adapter selection for diagnostics and integrations that already choose a WDDM device. It resolves the selected D3DKMT adapter, matches its LUID to DXGI, attempts Desktop Duplication, applies the D3DKMT ownership retry, and uses the CCD-resolved monitor fallback when needed.

```c
uint32_t bytes = 0;
int status = KS_CaptureBmp(1, NULL, &bytes);

uint8_t* bmp = malloc(bytes);
status = KS_CaptureBmp(1, bmp, &bytes);
```

## Capture report

After a successful capture, call `KS_GetLastCaptureReportJson` on the **same thread**. The report belongs to the most recent successful capture on that thread and follows the same two-call text-buffer pattern as the other JSON APIs.

```c
uint32_t reportBytes = 0;
int status = KS_GetLastCaptureReportJson(NULL, &reportBytes);

char* report = malloc(reportBytes);
status = KS_GetLastCaptureReportJson(report, &reportBytes);
```

The JSON records the request mode, selected/attempted adapter information, actual display owner LUID, GDI display name, VidPN source/target IDs, backend, capture route and output dimensions. A new capture request clears the previous report before it begins, so a failed new capture does not leave an apparently current report behind.

## DLL location for third-party applications

The DLL file is:

```text
KernelScreenshot.dll
```

Repository build outputs are placed in:

```text
Native\bin\x64\Debug\
```

or:

```text
Native\bin\x64\Release\
```

A third-party application should not hard-code a developer-specific absolute path.

The simplest deployment layout is:

```text
ThirdPartyApp.exe
KernelScreenshot.dll
```

with the DLL next to the third-party executable.

Consumers should reference the library by filename, for example:

```csharp
[DllImport("KernelScreenshot.dll", CallingConvention = CallingConvention.Cdecl)]
```

The host process must be x64.

## Command-line alternative

For applications that prefer to launch an executable instead of loading the DLL directly, use:

```text
KernelScreenshotCli.exe
```

See:

```text
docs/EXE_API.md
```
