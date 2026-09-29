using Microsoft.Win32.SafeHandles;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

internal static class Program
{
    private const int WH_MOUSE_LL = 14;
    private const int WM_LBUTTONDOWN = 0x0201;
    private const uint WM_QUIT = 0x0012;

    private static readonly NativeMethods.LowLevelMouseProc MouseProc = MouseHook;
    private static IntPtr _mouseHook;
    private static DriverClient? _driver;
    private static int _captureInProgress;
    private static uint _messageThreadId;

    [STAThread]
    private static void Main()
    {
        string[] args = Environment.GetCommandLineArgs().Skip(1).ToArray();
        bool legacyKmdod = args.Any(arg => string.Equals(arg, "--legacy-kmdod", StringComparison.OrdinalIgnoreCase));

        if (!legacyKmdod)
        {
            Console.WriteLine("Kernel Screenshot Lab - safe WDDM probe");
            Console.WriteLine("No display driver replacement is performed in this mode.");
            Console.WriteLine();
            Environment.ExitCode = D3DkmtProbeClient.RunInteractive();
            return;
        }

        Console.WriteLine("Kernel Screenshot Lab");
        Console.WriteLine("Left click anywhere to capture; Ctrl+C exits.");

        try
        {
            _driver = DriverClient.Open();
            FrameInfo info = _driver.Query();
            Console.WriteLine($"Driver ready: {info.Width}x{info.Height}, pitch={info.SourcePitch}, {info.BitsPerPixel} bpp");
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(@"Cannot open \\.\KernelScreenshot (driver control device not present).");
            Console.Error.WriteLine(ex.Message);
            Environment.ExitCode = 2;
            return;
        }

        Directory.CreateDirectory(OutputDirectory);
        _messageThreadId = NativeMethods.GetCurrentThreadId();

        Console.CancelKeyPress += (_, e) =>
        {
            e.Cancel = true;
            NativeMethods.PostThreadMessage(_messageThreadId, WM_QUIT, UIntPtr.Zero, IntPtr.Zero);
        };

        using Process current = Process.GetCurrentProcess();
        using ProcessModule? module = current.MainModule;
        IntPtr moduleHandle = NativeMethods.GetModuleHandle(module?.ModuleName);

        _mouseHook = NativeMethods.SetWindowsHookEx(WH_MOUSE_LL, MouseProc, moduleHandle, 0);
        if (_mouseHook == IntPtr.Zero)
            throw new Win32Exception(Marshal.GetLastWin32Error(), "SetWindowsHookEx failed.");

        try
        {
            while (NativeMethods.GetMessage(out NativeMethods.MSG msg, IntPtr.Zero, 0, 0) > 0)
            {
                NativeMethods.TranslateMessage(ref msg);
                NativeMethods.DispatchMessage(ref msg);
            }
        }
        finally
        {
            if (_mouseHook != IntPtr.Zero)
                NativeMethods.UnhookWindowsHookEx(_mouseHook);

            _driver.Dispose();
        }
    }

    private static string OutputDirectory => Path.Combine(Environment.CurrentDirectory, "Screenshots");

    private static IntPtr MouseHook(int nCode, IntPtr wParam, IntPtr lParam)
    {
        if (nCode >= 0 && wParam == (IntPtr)WM_LBUTTONDOWN &&
            Interlocked.Exchange(ref _captureInProgress, 1) == 0)
        {
            ThreadPool.QueueUserWorkItem(_ =>
            {
                try { CaptureOne(); }
                catch (Exception ex) { Console.Error.WriteLine($"Capture failed: {ex.Message}"); }
                finally { Volatile.Write(ref _captureInProgress, 0); }
            });
        }

        return NativeMethods.CallNextHookEx(_mouseHook, nCode, wParam, lParam);
    }

    private static void CaptureOne()
    {
        DriverClient driver = _driver ?? throw new InvalidOperationException("Driver not initialized.");
        (FrameInfo info, byte[] raw) = driver.Capture();

        string file = Path.Combine(
            OutputDirectory,
            $"kernel-{DateTime.Now:yyyyMMdd-HHmmss-fff}.png");

        SaveBgrx32AsPng(info, raw, file);
        Console.WriteLine($"[{DateTime.Now:HH:mm:ss.fff}] {file}");
    }

    private static void SaveBgrx32AsPng(FrameInfo info, byte[] raw, string path)
    {
        if (info.BitsPerPixel != 32 || info.PixelFormat != DriverClient.PixelFormatBgrx8)
            throw new NotSupportedException("Only 32-bpp BGRX is currently supported.");

        using Bitmap bitmap = new((int)info.Width, (int)info.Height, PixelFormat.Format32bppRgb);
        Rectangle rect = new(0, 0, bitmap.Width, bitmap.Height);
        BitmapData data = bitmap.LockBits(rect, ImageLockMode.WriteOnly, PixelFormat.Format32bppRgb);

        try
        {
            int sourceStride = checked((int)info.OutputStride);
            int destinationStride = Math.Abs(data.Stride);

            for (int y = 0; y < bitmap.Height; y++)
            {
                IntPtr row = data.Stride >= 0
                    ? IntPtr.Add(data.Scan0, y * data.Stride)
                    : IntPtr.Add(data.Scan0, (bitmap.Height - 1 - y) * destinationStride);

                Marshal.Copy(raw, y * sourceStride, row, sourceStride);
            }
        }
        finally
        {
            bitmap.UnlockBits(data);
        }

        bitmap.Save(path, ImageFormat.Png);
    }
}

[StructLayout(LayoutKind.Sequential, Pack = 4)]
internal struct FrameInfo
{
    public uint Version, Width, Height, SourcePitch, OutputStride, BitsPerPixel, PixelFormat, FrameBytes;
}

internal sealed class DriverClient : IDisposable
{
    public const uint PixelFormatBgrx8 = 1;

    private const uint FileDeviceUnknown = 0x22;
    private const uint MethodBuffered = 0;
    private const uint MethodOutDirect = 2;
    private const uint FileReadAccess = 1;

    private static readonly uint IoctlQuery = CtlCode(FileDeviceUnknown, 0x800, MethodBuffered, FileReadAccess);
    private static readonly uint IoctlCapture = CtlCode(FileDeviceUnknown, 0x801, MethodOutDirect, FileReadAccess);

    private readonly SafeFileHandle _handle;
    private DriverClient(SafeFileHandle handle) => _handle = handle;

    public static DriverClient Open()
    {
        SafeFileHandle handle = NativeMethods.CreateFile(
            @"\\.\KernelScreenshot",
            NativeMethods.GENERIC_READ,
            NativeMethods.FILE_SHARE_READ | NativeMethods.FILE_SHARE_WRITE,
            IntPtr.Zero,
            NativeMethods.OPEN_EXISTING,
            NativeMethods.FILE_ATTRIBUTE_NORMAL,
            IntPtr.Zero);

        if (handle.IsInvalid)
            throw new Win32Exception(Marshal.GetLastWin32Error());

        return new DriverClient(handle);
    }

    public FrameInfo Query()
    {
        int size = Marshal.SizeOf<FrameInfo>();
        IntPtr buffer = Marshal.AllocHGlobal(size);
        try
        {
            if (!NativeMethods.DeviceIoControl(_handle, IoctlQuery, IntPtr.Zero, 0, buffer, (uint)size, out uint returned, IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "IOCTL_SCREENSHOT_QUERY failed.");

            if (returned < size)
                throw new InvalidDataException("FrameInfo was truncated.");

            FrameInfo info = Marshal.PtrToStructure<FrameInfo>(buffer);
            if (info.Version != 1 || info.Width == 0 || info.Height == 0 || info.FrameBytes == 0)
                throw new InvalidDataException("Driver returned invalid framebuffer metadata.");

            return info;
        }
        finally { Marshal.FreeHGlobal(buffer); }
    }

    public (FrameInfo Info, byte[] Data) Capture()
    {
        FrameInfo info = Query();
        byte[] bytes = GC.AllocateUninitializedArray<byte>(checked((int)info.FrameBytes));
        GCHandle pinned = GCHandle.Alloc(bytes, GCHandleType.Pinned);

        try
        {
            if (!NativeMethods.DeviceIoControl(_handle, IoctlCapture, IntPtr.Zero, 0,
                    pinned.AddrOfPinnedObject(), info.FrameBytes, out uint returned, IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "IOCTL_SCREENSHOT_CAPTURE failed.");

            if (returned != info.FrameBytes)
                throw new InvalidDataException($"Expected {info.FrameBytes} bytes, got {returned}.");
        }
        finally { pinned.Free(); }

        return (info, bytes);
    }

    private static uint CtlCode(uint deviceType, uint function, uint method, uint access) =>
        (deviceType << 16) | (access << 14) | (function << 2) | method;

    public void Dispose() => _handle.Dispose();
}

internal static class NativeMethods
{
    internal const uint GENERIC_READ = 0x80000000;
    internal const uint FILE_SHARE_READ = 1;
    internal const uint FILE_SHARE_WRITE = 2;
    internal const uint OPEN_EXISTING = 3;
    internal const uint FILE_ATTRIBUTE_NORMAL = 0x80;

    internal delegate IntPtr LowLevelMouseProc(int nCode, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)] internal struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)]
    internal struct MSG
    {
        public IntPtr hwnd; public uint message; public UIntPtr wParam; public IntPtr lParam;
        public uint time; public POINT pt; public uint lPrivate;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    internal static extern SafeFileHandle CreateFile(string fileName, uint desiredAccess, uint shareMode,
        IntPtr securityAttributes, uint creationDisposition, uint flagsAndAttributes, IntPtr templateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool DeviceIoControl(SafeFileHandle device, uint code, IntPtr inBuffer,
        uint inBufferSize, IntPtr outBuffer, uint outBufferSize, out uint bytesReturned, IntPtr overlapped);

    [DllImport("user32.dll", SetLastError = true)]
    internal static extern IntPtr SetWindowsHookEx(int idHook, LowLevelMouseProc callback, IntPtr module, uint threadId);
    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool UnhookWindowsHookEx(IntPtr hook);
    [DllImport("user32.dll")] internal static extern IntPtr CallNextHookEx(IntPtr hook, int nCode, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] internal static extern int GetMessage(out MSG msg, IntPtr window, uint minFilter, uint maxFilter);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool TranslateMessage(ref MSG msg);
    [DllImport("user32.dll")] internal static extern IntPtr DispatchMessage(ref MSG msg);
    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool PostThreadMessage(uint threadId, uint msg, UIntPtr wParam, IntPtr lParam);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] internal static extern IntPtr GetModuleHandle(string? moduleName);
    [DllImport("kernel32.dll")] internal static extern uint GetCurrentThreadId();
}
