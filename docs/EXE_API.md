# KernelScreenshotCli.exe API

`KernelScreenshotCli.exe` is the command-line interface intended for third-party applications that prefer to call an executable instead of loading `KernelScreenshot.dll` directly.

The executable is x64 and requires `KernelScreenshot.dll` to be available in the same directory.

## Distribution layout

Keep these files together:

```text
KernelScreenshotCli.exe
KernelScreenshot.dll
```

The repository build places them in:

```text
Native\bin\x64\Debug\
```

or:

```text
Native\bin\x64\Release\
```

These are repository-relative locations. Third-party software should not hard-code a developer-specific absolute path.

The examples below intentionally use only:

```text
KernelScreenshotCli.exe
```

The calling application must resolve where that executable is installed. Common choices are:

- place the executable directory in `PATH`;
- run the third-party process with the executable directory as its working directory;
- store the executable directory in the third-party application's own configuration and resolve `KernelScreenshotCli.exe` from there.

## Exit codes and streams

On success:

```text
exit code = 0
```

On failure:

```text
exit code != 0
```

Errors and diagnostics are written to:

```text
stderr
```

Structured results or screenshot bytes are written to:

```text
stdout
```

unless `-out` is used.

This separation is intentional so third-party programs can safely capture stdout without mixing error text into the result.

## List devices

Command:

```bat
KernelScreenshotCli.exe -list
```

Successful stdout is UTF-8 JSON.

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

The top-level `index` identifies the device that can be passed to `-device`.

## Capture screenshot to stdout

Command:

```bat
KernelScreenshotCli.exe -device 0 -screenshot
```

Successful stdout contains only the raw bytes of one complete 32-bit BMP file.

There is no JSON wrapper around the image bytes.

A third-party process should:

1. start `KernelScreenshotCli.exe`;
2. capture stdout as binary;
3. capture stderr as text;
4. wait for process completion;
5. accept stdout only when the exit code is `0`.

Shell example:

```bat
KernelScreenshotCli.exe -device 0 -screenshot > screenshot.bmp
```

## Capture screenshot directly to a file

Command:

```bat
KernelScreenshotCli.exe -device 0 -screenshot -out screenshot.bmp
```

With `-out`, the BMP is written directly to the requested file.

The caller should check the process exit code before treating the file as valid.

## Typical third-party flow

```text
1. KernelScreenshotCli.exe -list
        |
        v
   parse JSON

2. choose device index
        |
        v
   device = 0 or 1 ...

3. KernelScreenshotCli.exe -device <index> -screenshot
        |
        v
   read BMP bytes from stdout
```

## C# example using the executable name only

This example assumes the executable directory is already resolvable through `PATH`, the current working directory, or the host application's configured executable directory.

```csharp
using System.Diagnostics;
using System.Text;

static async Task<string> ListDevicesAsync()
{
    var psi = new ProcessStartInfo
    {
        FileName = "KernelScreenshotCli.exe",
        UseShellExecute = false,
        RedirectStandardOutput = true,
        RedirectStandardError = true,
        CreateNoWindow = true
    };

    psi.ArgumentList.Add("-list");

    using var process = Process.Start(psi)
        ?? throw new InvalidOperationException("Cannot start KernelScreenshotCli.exe");

    string stdout = await process.StandardOutput.ReadToEndAsync();
    string stderr = await process.StandardError.ReadToEndAsync();

    await process.WaitForExitAsync();

    if (process.ExitCode != 0)
        throw new InvalidOperationException(stderr);

    return stdout;
}
```

For screenshot bytes, use `process.StandardOutput.BaseStream` instead of `ReadToEndAsync()`, because the output is binary BMP data.

## Direct DLL alternative

Applications that do not want to create a child process should use:

```text
KernelScreenshot.dll
```

See:

```text
docs/DLL_API.md
```
