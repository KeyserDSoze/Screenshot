# Third-party quick start

This document is for applications that want to use KernelScreenshot without knowing the internal D3DKMT, DXGI, or D3D11 implementation.

The release package is Windows x64 and contains:

```text
KernelScreenshotCli.exe
KernelScreenshot.dll
KernelScreenshotApi.h
THIRD_PARTY_QUICKSTART.md
CLI_GUIDE.md
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

`KernelScreenshotCli.exe` exposes automatic one-frame capture plus optional diagnostics and explicit device selection:

```text
-screenshot
-screenshot -out <file.bmp>
-list
-pipeline
-vendor-pipeline
-display <DISPLAYn> -screenshot
-device <index|auto> -screenshot
-report <file.json> (optional on screenshot commands)
```

For ordinary screenshot use, start with `-screenshot`. It automatically resolves the active CCD/VidPN display owner, captures one frame, and returns a complete BMP image. `-list`, `-pipeline`, `-vendor-pipeline`, and numeric `-device N` are available when an integration needs diagnostics or explicit adapter control.

Internally the tool uses the active vendor graphics driver. It does not replace the Intel, NVIDIA, or AMD display driver.

## 1. Capture a screenshot automatically

For the normal one-frame use case, no device enumeration is required:

```bat
KernelScreenshotCli.exe -screenshot
```

Exit code `0` means success. stdout contains only the binary bytes of a complete 32-bit BMP; stderr is reserved for errors.

Automatic mode queries the active Windows CCD/VidPN topology first and tries the adapters that actually own active display paths before any remaining fallback adapter. This is the recommended mode for hybrid Intel/NVIDIA laptops because the adapter that DXGI enumerates first is not always the physical scan-out owner.

To save directly to a file:

```bat
KernelScreenshotCli.exe -screenshot -out screenshot.bmp
```

The compatibility spelling below is equivalent:

```bat
KernelScreenshotCli.exe -device auto -screenshot
```

## 2. Optional device and pipeline diagnostics

Use `-list` only when the application needs to inspect adapters or explicitly prefer one:

```bat
KernelScreenshotCli.exe -list
```

Successful stdout is UTF-8 JSON. Do not assume that Intel is always index 0 or NVIDIA is always index 1. On hybrid laptops, an adapter can remain enumerated while it no longer owns an active desktop path.

For lower-level ownership and signal diagnostics:

```bat
KernelScreenshotCli.exe -pipeline
KernelScreenshotCli.exe -vendor-pipeline
```

`-pipeline` reports the Windows CCD/D3DKMT/VidPN view. `-vendor-pipeline` adds Intel IGCL and NVIDIA NVAPI information when those runtimes are installed.

Numeric selection remains available:

```bat
KernelScreenshotCli.exe -device 0 -screenshot
```

A numeric index is treated as a preferred adapter; if it fails, the CLI retains its fault-tolerant fallback to other enumerated adapters.

### Bytes are the native CLI integration mode

For another application, omitting `-out` is intentional: the EXE returns the screenshot through stdout as raw BMP bytes. No temporary BMP file is required.

The automatic form is:

```text
KernelScreenshotCli.exe -screenshot
        |
        +-- stdout: complete BMP byte stream
        +-- stderr: text errors only
```

The caller can read stdout directly into a `byte[]`, `MemoryStream`, buffer, pipe, or equivalent binary container. The same applies to `-display DISPLAYn -screenshot` and `-device auto -screenshot`.

## 3. Capture a screenshot as bytes

Run:

```bat
KernelScreenshotCli.exe -screenshot
```

Use `-device N` only when you intentionally want to prefer a specific adapter.

When the command succeeds:

- exit code is `0`;
- stdout contains only the binary bytes of a complete 32-bit BMP file;
- stderr contains no screenshot data.

There is no JSON wrapper around the screenshot.

For a command-line test:

```bat
KernelScreenshotCli.exe -screenshot > screenshot.bmp
```

A third-party application should read stdout as a binary stream, not as text.

## 4. Capture directly to a file

For tests or applications that prefer a file:

```bat
KernelScreenshotCli.exe -screenshot -out screenshot.bmp
```

The process exit code must still be checked.

## 5. Capture a specific display

When multiple displays are active, first inspect the active paths:

```bat
KernelScreenshotCli.exe -pipeline
```

Then capture the GDI source you want:

```bat
KernelScreenshotCli.exe -display DISPLAY1 -screenshot -out internal.bmp
KernelScreenshotCli.exe -display DISPLAY5 -screenshot -out external.bmp
```

The exact-display path is owner-aware: CCD/VidPN selects the adapter, DDA is attempted for that display, and WGC is used only as a fallback for the same monitor.

## 6. Write a capture report

Add `-report <file.json>` to any successful screenshot command:

```bat
KernelScreenshotCli.exe -screenshot -out screenshot.bmp -report capture.json
```

The JSON records which GPU/display actually supplied the frame, the VidPN source/target IDs, whether DDA or WGC succeeded, the internal route used, and the captured dimensions. This is useful for support logs on hybrid Intel/NVIDIA machines.

For a concise end-user command reference, read `CLI_GUIDE.md` from the release package.

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
static async Task<byte[]> CaptureAsync()
{
    var psi = new ProcessStartInfo
    {
        FileName = "KernelScreenshotCli.exe",
        UseShellExecute = false,
        RedirectStandardOutput = true,
        RedirectStandardError = true,
        CreateNoWindow = true
    };

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

The recommended C# flow for ordinary one-frame capture is:

```text
KernelScreenshotCli.exe -screenshot
        |
        v
CCD/VidPN owner-first automatic selection
        |
        v
read stdout BaseStream
        |
        v
byte[] containing a complete BMP
```

Device-list parsing is optional and is only needed when the application wants diagnostics or an explicit preferred adapter.

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

static async Task<byte[]> CaptureAsync()
{
    var psi = new ProcessStartInfo
    {
        FileName = "KernelScreenshotCli.exe",
        UseShellExecute = false,
        RedirectStandardOutput = true,
        RedirectStandardError = true,
        CreateNoWindow = true
    };

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
byte[] screenshot = await CaptureAsync();

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

The DLL API exposes automatic capture directly:

```text
KS_CaptureBmpAuto
```

and also exposes `KS_CaptureDisplayBmp` for one exact active monitor, `KS_GetLastCaptureReportJson` for the most recent successful capture, `KS_ListDevicesJson`, `KS_ListDisplayPipelinesJson`, `KS_ListVendorPipelinesJson`, explicit `KS_CaptureBmp`, and `KS_GetLastErrorMessage`.

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
