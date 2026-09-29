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

Each device also reports:

```json
"outputCount": 1,
"attachedOutputCount": 0,
"hasAttachedDesktopOutput": false
```

`outputCount` is the number of DXGI outputs enumerated on that adapter.

`attachedOutputCount` counts only outputs currently attached to the Windows desktop.

`hasAttachedDesktopOutput` is the simplest field for third-party code deciding whether an explicitly selected adapter is a reasonable screenshot candidate.

A graphics adapter can remain present in `-list` even when it currently has no attached desktop output. This is common on hybrid laptops when an external display is disconnected.

## Automatic adapter fallback

For third-party applications that do not need to force a specific GPU, use:

```bat
KernelScreenshotCli.exe -device auto -screenshot
```

The CLI tries the currently enumerated adapters in order and returns the first screenshot that succeeds.

Before leaving Desktop Duplication on hybrid systems, the capture engine also resolves each `\\.\DISPLAYn` name through `D3DKMTOpenAdapterFromGdiDisplayName`. If the low-level VidPN/GDI owner LUID differs from the DXGI adapter that first exposed the output, it retries `DuplicateOutput1` / `DuplicateOutput` on that KMT-resolved adapter. Windows Graphics Capture remains only the final fallback after both Desktop Duplication ownership paths fail.

Numeric device selection is also fault-tolerant. For example:

```bat
KernelScreenshotCli.exe -device 0 -screenshot
```

means "prefer device 0". The CLI tries device 0 first; if capture fails because that adapter no longer owns a usable desktop output, it automatically tries the other enumerated adapters before returning an error.

This is useful when display topology changes, for example when an HDMI monitor is connected or disconnected.

The file-output variant is:

```bat
KernelScreenshotCli.exe -device auto -screenshot -out screenshot.bmp
```

For multi-monitor applications that need a specific physical desktop region, explicit adapter/output selection is preferable to `auto`. The current CLI still captures the first attached duplicable output on the chosen adapter.

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

## Important: avoid stdout pipe deadlocks in C#

Screenshot mode writes the entire BMP to `stdout`. A 2560x1600 32-bit BMP is roughly 16 MB, while an redirected process pipe is much smaller.

Do **not** do this:

```csharp
await process.WaitForExitAsync();
byte[] bmp = await ReadStdoutBytesAsync(process);
```

That can deadlock:

```text
KernelScreenshotCli.exe
    -> fills redirected stdout pipe
    -> waits for the parent process to read it

C# parent
    -> waits for KernelScreenshotCli.exe to exit
    -> does not read stdout yet
```

The parent must drain stdout **while the child process is still running**. stderr should also be drained concurrently.

A safe pattern is:

```csharp
static async Task<byte[]> CaptureAsync(int deviceIndex)
{
    var psi = new ProcessStartInfo
    {
        FileName = "KernelScreenshotCli.exe",
        UseShellExecute = false,
        RedirectStandardOutput = true,
        RedirectStandardError = true,
        CreateNoWindow = true
    };

    psi.ArgumentList.Add("-device");
    psi.ArgumentList.Add(deviceIndex.ToString());
    psi.ArgumentList.Add("-screenshot");

    using Process process = Process.Start(psi)
        ?? throw new InvalidOperationException("Cannot start KernelScreenshotCli.exe");

    using var image = new MemoryStream();

    Task stdoutTask =
        process.StandardOutput.BaseStream.CopyToAsync(image);

    Task<string> stderrTask =
        process.StandardError.ReadToEndAsync();

    Task exitTask =
        process.WaitForExitAsync();

    await Task.WhenAll(stdoutTask, stderrTask, exitTask);

    string stderr = await stderrTask;

    if (process.ExitCode != 0)
        throw new InvalidOperationException(
            $"KernelScreenshotCli exited with {process.ExitCode}: {stderr}");

    byte[] bmp = image.ToArray();

    if (bmp.Length < 2 || bmp[0] != (byte)'B' || bmp[1] != (byte)'M')
        throw new InvalidDataException(
            $"KernelScreenshotCli returned {bmp.Length} bytes, but they are not a BMP.");

    return bmp;
}
```

If the client wants to avoid binary stdout entirely, use `-out <file.bmp>` and read the file after the process exits.

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

2. choose an adapter with hasAttachedDesktopOutput=true
   or use -device auto
        |
        v

3. KernelScreenshotCli.exe -device <index|auto> -screenshot
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
