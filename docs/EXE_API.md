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

## Inspect the active display pipeline

Command:

```bat
KernelScreenshotCli.exe -pipeline
```

Successful stdout is UTF-8 JSON describing each active CCD display path. It correlates the Windows source and target with:

- source/target adapter LUID and IDs;
- GDI display name such as `\\\\.\\DISPLAY1`;
- monitor friendly name and monitor device path;
- CCD source desktop dimensions and position;
- target signal pixel rate, sync frequencies, active/total size and scan-line ordering;
- the D3DKMT adapter LUID resolved from the GDI display name;
- the VidPN source ID used by the KMD;
- the current D3DKMT display mode;
- the current number of Desktop Duplication clients on that VidPN source.

This command does not capture pixels and does not install or replace any display driver. It is diagnostic output for understanding the Windows/WDDM scan-out path before adding optional Intel IGCL or NVIDIA NVAPI probes.

## Automatic display-owner selection

The recommended capture command is:

```bat
KernelScreenshotCli.exe -screenshot
```

or, equivalently:

```bat
KernelScreenshotCli.exe -device auto -screenshot
```

Automatic mode no longer starts by blindly trying adapter indexes. It first queries the active Windows CCD topology, takes the adapter LUIDs that own active VidPN display paths, maps those LUIDs to the enumerated WDDM devices, removes duplicates while preserving active-path order, and tries those active display owners first. Only if every active owner fails does it try any remaining adapters as a compatibility fallback.

Inside each selected adapter, Desktop Duplication remains the primary backend. The capture engine also resolves `\\.\DISPLAYn` through D3DKMT; if DXGI cannot expose a duplicable output even though CCD says that adapter owns an active path, the exact monitor is resolved to `HMONITOR` and Windows Graphics Capture is used as the final monitor-scoped fallback.

This owner-first behavior is designed for hybrid Intel/NVIDIA systems where DXGI adapter/output enumeration may not match the physical scan-out owner.

The direct-to-file form is:

```bat
KernelScreenshotCli.exe -screenshot -out screenshot.bmp
```

`-device auto` remains supported for compatibility and behaves the same way.

Numeric device selection is still available for diagnostics:

```bat
KernelScreenshotCli.exe -device 0 -screenshot
```

A numeric device is treated as the preferred adapter; if it cannot capture, the CLI keeps its previous fault-tolerant behavior and tries the other enumerated adapters before failing.

For multi-monitor applications that need a specific active monitor, use `-display DISPLAYn`. Automatic mode still returns the first successful one-frame capture from the active owner ordering.

## Capture report

All screenshot forms accept an optional report path:

```bat
KernelScreenshotCli.exe -screenshot -out screenshot.bmp -report capture.json
KernelScreenshotCli.exe -display DISPLAY1 -screenshot -out display1.bmp -report display1.json
KernelScreenshotCli.exe -device auto -screenshot -out screenshot.bmp -report capture.json
```

`-out` and `-report` may be given in either order. `-report` never writes JSON to stdout, so it is safe to use when stdout is carrying binary BMP bytes.

The JSON describes the successful capture route. It includes the request mode, strict-adapter flag, auto candidate classification, actual adapter LUID/name, GDI display name, VidPN source/target IDs, backend, route, width, height and BMP byte count.

Backend values currently include `DuplicateOutput1`, `DuplicateOutput`, and `WindowsGraphicsCapture`. Route values distinguish direct DXGI capture from KMT ownership retry and monitor-scoped fallback paths.

## Capture one exact display

Use the GDI source name reported by `-pipeline` when a multi-monitor caller needs one specific active display:

```bat
KernelScreenshotCli.exe -display DISPLAY1 -screenshot
KernelScreenshotCli.exe -display DISPLAY5 -screenshot -out hdmi.bmp
```

Both `DISPLAYn` and the full `\\.\DISPLAYn` spelling are accepted. The requested name must correspond to an active CCD/VidPN path.

The engine resolves the selected path's adapter LUID from CCD, attempts Desktop Duplication only on that exact DXGI output, and uses monitor-scoped Windows Graphics Capture only if DDA cannot expose/capture that same display. It does not silently switch to a different monitor.

This is the preferred command when the machine has multiple active displays and the caller needs deterministic monitor selection.

## Strict adapter selection

By default, numeric `-device N` means **preferred adapter**. The CLI tries that index first and may try other adapters if the preferred one cannot capture.

Use `-strict` to disable every cross-adapter / non-DDA fallback:

```bat
KernelScreenshotCli.exe -device 0 -screenshot -strict
KernelScreenshotCli.exe -device 0 -screenshot -strict -out strict.bmp
KernelScreenshotCli.exe -device 0 -screenshot -strict -report strict.json
```

In strict mode the selected adapter is the only permitted adapter. The engine may use `DuplicateOutput1` or `DuplicateOutput` on that adapter, but it will not try another device index, will not retry a different KMT owner, and will not use Windows Graphics Capture. If the selected adapter cannot provide Desktop Duplication, the command fails with a non-zero exit code.

`-strict` is valid only with numeric `-device <index>`. It is rejected with `-device auto`, plain `-screenshot`, and `-display DISPLAYn`.

## Binary screenshot contract

For every screenshot form, `-out` is optional. If it is omitted, stdout is the complete binary BMP:

```bat
KernelScreenshotCli.exe -screenshot
KernelScreenshotCli.exe -device auto -screenshot
KernelScreenshotCli.exe -display DISPLAY1 -screenshot
```

These commands do **not** print a filename, JSON, Base64, or textual wrapper to stdout. The caller receives the BMP bytes directly and may keep them in memory, save them, decode them, hash them, or pass them to another component.

Using `-out file.bmp` changes only the destination of those bytes. Capture selection and backend behavior are otherwise the same.

`-report file.json` is independent of the image destination. Therefore this is valid:

```bat
KernelScreenshotCli.exe -screenshot -report capture.json
```

where stdout remains binary BMP data and the JSON report goes to `capture.json`.

## Capture screenshot to stdout

Command:

```bat
KernelScreenshotCli.exe -screenshot
```

Explicit adapter selection remains available with `-device N`.

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
KernelScreenshotCli.exe -screenshot > screenshot.bmp
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

## Capture screenshot directly to a file

Command:

```bat
KernelScreenshotCli.exe -screenshot -out screenshot.bmp
```

With `-out`, the BMP is written directly to the requested file.

The caller should check the process exit code before treating the file as valid.

## Typical third-party flow

For applications that only need one desktop screenshot, device enumeration is no longer required:

```text
1. KernelScreenshotCli.exe -screenshot
        |
        v
   auto-resolve active CCD/VidPN owner
        |
        v
   DDA first, monitor-scoped WGC only as final fallback
        |
        v
   read BMP bytes from stdout
```

Use `-list`, `-pipeline`, `-vendor-pipeline`, or explicit `-device N` only when the caller needs diagnostics, topology inspection, or a preferred adapter.

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

## Vendor display pipeline

Command:

```bat
KernelScreenshotCli.exe -vendor-pipeline
```

The command is diagnostic and read-only. It dynamically loads vendor runtime components already installed by the GPU driver. The first implementation probes Intel IGCL through `ControlLib.dll` and reports Intel adapter LUID/PCI identity, enumerated display encoders, applied timing, output/mux type, active/attached/dithering flags, display feature flags, and the current wire color model/depth when supported. It also probes NVIDIA NVAPI dynamically from `nvapi64.dll` and reports physical GPU identity, PCI/bus information, connected display IDs and their active/connected/OS-visible state, output type, and current SDR/HDR output mode when supported.

The command does not install a kernel driver and does not modify display settings.

## CCD/VidPN ownership fallback

On hybrid systems, DXGI output enumeration can disagree with the active Windows display path. Capture now queries the active CCD topology and matches each source by adapter LUID. If Desktop Duplication cannot expose a usable output on the selected adapter but that adapter owns an active CCD/VidPN source, the exact GDI display name is resolved to an `HMONITOR` and the monitor-scoped Windows Graphics Capture backend is attempted. DDA remains the primary backend.
