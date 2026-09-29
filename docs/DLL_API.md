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

## CLI wrapper

A DLL is not a command-line executable, so the shell syntax is provided by `KernelScreenshotCli.exe`, which calls the DLL.

List devices:

```bat
KernelScreenshotCli.exe -list
```

Write screenshot bytes directly to stdout:

```bat
KernelScreenshotCli.exe -device 0 -screenshot > shot.bmp
```

Or write a file explicitly:

```bat
KernelScreenshotCli.exe -device 0 -screenshot -out shot.bmp
```

For screenshot mode, stdout contains only BMP bytes unless `-out` is used. Diagnostics and errors go to stderr.
