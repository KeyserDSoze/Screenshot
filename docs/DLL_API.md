# KernelScreenshot.dll API

`KernelScreenshot.dll` is the reusable native x64 API for adapter discovery and one-frame desktop capture.

The DLL exports a C ABI so it can be consumed from C, C++, C#, Rust, Python ctypes, and other FFI-capable runtimes. Microsoft recommends C linkage for C++ DLL exports when the functions need to be easy to consume from other languages.

## Exports

```c
int __cdecl KS_ListDevicesJson(
    char* buffer,
    uint32_t* bufferBytes);

int __cdecl KS_CaptureBmp(
    uint32_t deviceIndex,
    uint8_t* buffer,
    uint32_t* bufferBytes);

int __cdecl KS_GetLastErrorMessage(
    char* buffer,
    uint32_t* bufferBytes);
```

All functions return one of the `KS_STATUS` values declared in `KernelScreenshotApi.h`.

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

`KS_CaptureBmp(deviceIndex, ...)`:

1. resolves the selected D3DKMT adapter;
2. matches the same LUID to a DXGI adapter;
3. creates a D3D11 device on that adapter;
4. uses Desktop Duplication on the first attached duplicable output;
5. copies the desktop texture to a CPU-readable staging texture;
6. returns a complete 32-bit BMP file as bytes.

Example:

```c
uint32_t bytes = 0;
int status = KS_CaptureBmp(1, NULL, &bytes);

uint8_t* bmp = malloc(bytes);
status = KS_CaptureBmp(1, bmp, &bytes);

// bmp[0..bytes-1] is a complete BMP file.
```

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
