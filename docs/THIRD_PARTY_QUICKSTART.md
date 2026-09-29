# Third-party quick start

This document is for applications that want to use KernelScreenshot without knowing the internal D3DKMT, DXGI, or D3D11 implementation.

The release package is Windows x64 and contains:

```text
KernelScreenshotCli.exe
KernelScreenshot.dll
KernelScreenshotApi.h
THIRD_PARTY_QUICKSTART.md
EXE_API.md
DLL_API.md
```

Keep `KernelScreenshotCli.exe` and `KernelScreenshot.dll` in the same directory.

For the simplest C# integration, copy both files next to your application's executable:

```text
MyApplication.exe
KernelScreenshotCli.exe
KernelScreenshot.dll
```

Then the C# application can launch `KernelScreenshotCli.exe` by filename only. No developer-machine absolute path is part of the API contract.

## What the executable does

`KernelScreenshotCli.exe` exposes two operations:

```text
-list
-device <index> -screenshot
```

The first operation discovers the WDDM graphics adapters and outputs available on the machine.

The second operation selects one adapter by the index returned by `-list`, captures one desktop frame, and returns a complete BMP image.

Internally the tool uses the active vendor graphics driver. It does not replace the Intel, NVIDIA, or AMD display driver.

## 1. Get the device list

Run:

```bat
KernelScreenshotCli.exe -list
```

Exit code `0` means success.

Successful stdout is UTF-8 JSON. stderr is reserved for errors.

Example:

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

The important field for screenshot requests is the top-level:

```json
"index": 0
```

That value is the device index.

Do not assume that Intel is always index 0 or NVIDIA is always index 1. Read the list on the machine and select the device you want from the returned JSON.

Also do not treat "adapter exists" as "adapter currently owns an active desktop output". On hybrid laptops, disconnecting HDMI can leave the NVIDIA adapter present while its `attachedOutputCount` becomes `0`; the internal panel may still be on Intel.

For explicit selection, prefer devices where:

```json
"hasAttachedDesktopOutput": true
```

For applications that simply want a screenshot from whichever adapter can currently capture the desktop, use:

```bat
KernelScreenshotCli.exe -device auto -screenshot
```

This automatically falls back across adapters when display topology changes.

## 2. Capture a screenshot as bytes

Run:

```bat
KernelScreenshotCli.exe -device 0 -screenshot
```

When the command succeeds:

- exit code is `0`;
- stdout contains only the binary bytes of a complete 32-bit BMP file;
- stderr contains no screenshot data.

There is no JSON wrapper around the screenshot.

For a command-line test:

```bat
KernelScreenshotCli.exe -device 0 -screenshot > screenshot.bmp
```

A third-party application should read stdout as a binary stream, not as text.

## 3. Capture directly to a file

For tests or applications that prefer a file:

```bat
KernelScreenshotCli.exe -device 0 -screenshot -out screenshot.bmp
```

The process exit code must still be checked.

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

## C# integration

The recommended C# flow is:

```text
KernelScreenshotCli.exe -list
        |
        v
deserialize JSON
        |
        v
choose device.index
        |
        v
KernelScreenshotCli.exe -device <index> -screenshot
        |
        v
read stdout BaseStream
        |
        v
byte[] containing a complete BMP
```

### C# models

```csharp
public sealed class ScreenshotDevice
{
    public int Index { get; set; }
    public string Name { get; set; } = "";
    public string Wddm { get; set; } = "";
    public int OutputCount { get; set; }
    public int AttachedOutputCount { get; set; }
    public bool HasAttachedDesktopOutput { get; set; }
    public List<ScreenshotOutput> Outputs { get; set; } = [];
}

public sealed class ScreenshotOutput
{
    public int Index { get; set; }
    public string Name { get; set; } = "";
    public bool AttachedToDesktop { get; set; }
    public int DesktopLeft { get; set; }
    public int DesktopTop { get; set; }
    public int DesktopRight { get; set; }
    public int DesktopBottom { get; set; }
    public uint Rotation { get; set; }
}
```

Unknown JSON fields can simply be ignored by `System.Text.Json`.

### C# list example

This example expects `KernelScreenshotCli.exe` and `KernelScreenshot.dll` next to the calling C# executable.

```csharp
using System.Diagnostics;
using System.Text.Json;

static async Task<List<ScreenshotDevice>> ListDevicesAsync()
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

    using Process process = Process.Start(psi)
        ?? throw new InvalidOperationException("Cannot start KernelScreenshotCli.exe");

    string stdout = await process.StandardOutput.ReadToEndAsync();
    string stderr = await process.StandardError.ReadToEndAsync();

    await process.WaitForExitAsync();

    if (process.ExitCode != 0)
        throw new InvalidOperationException(stderr);

    return JsonSerializer.Deserialize<List<ScreenshotDevice>>(
        stdout,
        new JsonSerializerOptions
        {
            PropertyNameCaseInsensitive = true
        }) ?? [];
}
```

### C# screenshot byte[] example

```csharp
using System.Diagnostics;

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

    Task copyTask = process.StandardOutput.BaseStream.CopyToAsync(image);
    Task<string> errorTask = process.StandardError.ReadToEndAsync();

    await Task.WhenAll(copyTask, process.WaitForExitAsync());

    string stderr = await errorTask;

    if (process.ExitCode != 0)
        throw new InvalidOperationException(stderr);

    return image.ToArray();
}
```

Usage:

```csharp
List<ScreenshotDevice> devices = await ListDevicesAsync();

ScreenshotDevice device = devices
    .First(d => d.HasAttachedDesktopOutput);

byte[] screenshot = await CaptureAsync(device.Index);

await File.WriteAllBytesAsync("screenshot.bmp", screenshot);
```

The returned `byte[]` already contains the BMP header and pixel data. It can be saved directly or passed to another image-processing component.

## Where the executable must be

The executable name used by integrations is always:

```text
KernelScreenshotCli.exe
```

The DLL name is always:

```text
KernelScreenshot.dll
```

The recommended deployed layout is app-local:

```text
YourAppFolder\
    YourApp.exe
    KernelScreenshotCli.exe
    KernelScreenshot.dll
```

This avoids hard-coded development paths.

If you intentionally install the tool somewhere else, your application is responsible for making that directory resolvable, for example through its own configuration or the process environment.

## Direct DLL integration

If starting a child process is not desirable, load `KernelScreenshot.dll` directly.

The DLL API exposes:

```text
KS_ListDevicesJson
KS_CaptureBmp
KS_GetLastErrorMessage
```

See:

```text
DLL_API.md
```

for the native ABI and buffer contract.

## Detailed executable contract

See:

```text
EXE_API.md
```

for complete command syntax, stdout/stderr behavior, and exit-code rules.

## Release package

GitHub Actions builds and publishes the Windows x64 package automatically on every successful build of `main`.

No manual Git tag is required.

Each build receives a generated version:

```text
v0.1.<GitHub Actions run number>
```

and creates a versioned release containing a ZIP such as:

```text
KernelScreenshot-win-x64-v0.1.7.zip
```

The workflow also updates the release named:

```text
KernelScreenshot latest
```

so integrations that do not need to pin a specific build can always use the newest successful package.

Extract the ZIP and keep `KernelScreenshotCli.exe` and `KernelScreenshot.dll` together.
