using System.Diagnostics;
using System.Text.Json;

internal static class D3DkmtProbeClient
{
    public static int RunInteractive()
    {
        string? probePath = FindProbe();
        if (probePath == null)
        {
            Console.Error.WriteLine("D3DKMTProbe.exe was not found.");
            Console.Error.WriteLine("Build it first with:");
            Console.Error.WriteLine(
                @"  ""C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"" Native\D3DKMTProbe\D3DKMTProbe.vcxproj /p:Configuration=Debug /p:Platform=x64");
            return 10;
        }

        IReadOnlyList<WddmAdapter> adapters;
        try
        {
            adapters = RunProbe(probePath);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"D3DKMT probe failed: {ex.Message}");
            return 11;
        }

        if (adapters.Count == 0)
        {
            Console.WriteLine("No WDDM adapters were returned by D3DKMTEnumAdapters2.");
            return 0;
        }

        Console.WriteLine("WDDM adapters (D3DKMT -> dxgkrnl -> active vendor driver)");
        Console.WriteLine();

        foreach (WddmAdapter adapter in adapters)
        {
            Console.WriteLine($"[{adapter.Index}] {adapter.Name}");
            Console.WriteLine(
                $"    LUID={FormatLuid(adapter)}, sources={adapter.Sources}, WDDM={DisplayOrUnknown(adapter.Wddm)}");

            if (adapter.Pci is not null)
            {
                Console.WriteLine(
                    $"    PCI={adapter.Pci.Bus}:{adapter.Pci.Device}.{adapter.Pci.Function}");
            }

            if (adapter.Type is not null)
            {
                Console.WriteLine(
                    $"    render={adapter.Type.RenderSupported}, display={adapter.Type.DisplaySupported}, " +
                    $"post={adapter.Type.PostDevice}, hybridIntegrated={adapter.Type.HybridIntegrated}, " +
                    $"hybridDiscrete={adapter.Type.HybridDiscrete}");
            }

            Console.WriteLine();
        }

        Console.Write("Select adapter index (Enter to quit): ");
        string? input = Console.ReadLine();
        if (string.IsNullOrWhiteSpace(input))
            return 0;

        if (!int.TryParse(input, out int selectedIndex))
        {
            Console.Error.WriteLine("Invalid adapter index.");
            return 12;
        }

        WddmAdapter? selected = adapters.FirstOrDefault(a => a.Index == selectedIndex);
        if (selected is null)
        {
            Console.Error.WriteLine("Adapter index not found.");
            return 13;
        }

        Console.WriteLine();
        Console.WriteLine($"Selected: {selected.Name}");
        Console.WriteLine($"LUID: {FormatLuid(selected)}");
        Console.WriteLine(
            "This is the safe WDDM path: the Intel/NVIDIA driver stays installed; " +
            "the helper talks to dxgkrnl through documented D3DKMT calls.");
        Console.WriteLine(
            "Pixel readback is intentionally not enabled yet. The next step is to bind a capture/readback path " +
            "to this selected adapter without replacing its display miniport.");

        return 0;
    }

    private static IReadOnlyList<WddmAdapter> RunProbe(string probePath)
    {
        using Process process = new()
        {
            StartInfo = new ProcessStartInfo
            {
                FileName = probePath,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            }
        };

        if (!process.Start())
            throw new InvalidOperationException("Could not start D3DKMTProbe.exe.");

        string stdout = process.StandardOutput.ReadToEnd();
        string stderr = process.StandardError.ReadToEnd();
        process.WaitForExit();

        if (process.ExitCode != 0)
        {
            string detail = string.IsNullOrWhiteSpace(stderr)
                ? $"exit code {process.ExitCode}"
                : stderr.Trim();
            throw new InvalidOperationException(detail);
        }

        List<WddmAdapter>? adapters = JsonSerializer.Deserialize<List<WddmAdapter>>(
            stdout,
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

        return adapters ?? [];
    }

    private static string? FindProbe()
    {
        string? configured = Environment.GetEnvironmentVariable("D3DKMT_PROBE_PATH");
        if (!string.IsNullOrWhiteSpace(configured) && File.Exists(configured))
            return Path.GetFullPath(configured);

        foreach (string start in new[] { Environment.CurrentDirectory, AppContext.BaseDirectory })
        {
            DirectoryInfo? directory = new(start);
            for (int depth = 0; directory is not null && depth < 8; ++depth, directory = directory.Parent)
            {
                foreach (string configuration in new[] { "Debug", "Release" })
                {
                    string candidate = Path.Combine(
                        directory.FullName,
                        "Native",
                        "D3DKMTProbe",
                        "bin",
                        "x64",
                        configuration,
                        "D3DKMTProbe.exe");

                    if (File.Exists(candidate))
                        return candidate;
                }
            }
        }

        return null;
    }

    private static string FormatLuid(WddmAdapter adapter) =>
        $"{unchecked((uint)adapter.LuidHighPart):X8}:{adapter.LuidLowPart:X8}";

    private static string DisplayOrUnknown(string? value) =>
        string.IsNullOrWhiteSpace(value) ? "unknown" : value;
}

internal sealed class WddmAdapter
{
    public int Index { get; set; }
    public string Name { get; set; } = "";
    public int LuidHighPart { get; set; }
    public uint LuidLowPart { get; set; }
    public uint Sources { get; set; }
    public int WddmRaw { get; set; }
    public string? Wddm { get; set; }
    public WddmPciAddress? Pci { get; set; }
    public WddmAdapterType? Type { get; set; }
}

internal sealed class WddmPciAddress
{
    public uint Bus { get; set; }
    public uint Device { get; set; }
    public uint Function { get; set; }
}

internal sealed class WddmAdapterType
{
    public bool RenderSupported { get; set; }
    public bool DisplaySupported { get; set; }
    public bool SoftwareDevice { get; set; }
    public bool PostDevice { get; set; }
    public bool HybridDiscrete { get; set; }
    public bool HybridIntegrated { get; set; }
    public bool IndirectDisplayDevice { get; set; }
    public bool Paravirtualized { get; set; }
}
