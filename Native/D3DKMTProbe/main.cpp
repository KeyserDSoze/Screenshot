#include <windows.h>
#include <winternl.h>
#include <d3dkmthk.h>

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    bool NtSuccess(NTSTATUS status)
    {
        return status >= 0;
    }

    template <typename T>
    bool QueryAdapter(D3DKMT_HANDLE handle, KMTQUERYADAPTERINFOTYPE type, T& value)
    {
        D3DKMT_QUERYADAPTERINFO query = {};
        query.hAdapter = handle;
        query.Type = type;
        query.pPrivateDriverData = &value;
        query.PrivateDriverDataSize = sizeof(value);
        return NtSuccess(D3DKMTQueryAdapterInfo(&query));
    }

    std::string WideToUtf8(const wchar_t* value)
    {
        if (value == nullptr || *value == L'\0')
            return {};

        const int required = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value,
            -1,
            nullptr,
            0,
            nullptr,
            nullptr);

        if (required <= 1)
            return {};

        std::string result(static_cast<size_t>(required), '\0');
        const int written = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value,
            -1,
            result.data(),
            required,
            nullptr,
            nullptr);

        if (written <= 1)
            return {};

        result.resize(static_cast<size_t>(written - 1));
        return result;
    }

    std::string JsonEscape(const std::string& value)
    {
        std::ostringstream out;
        for (unsigned char ch : value)
        {
            switch (ch)
            {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20)
                {
                    out << "\\u"
                        << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<unsigned int>(ch)
                        << std::dec << std::setfill(' ');
                }
                else
                {
                    out << static_cast<char>(ch);
                }
            }
        }
        return out.str();
    }

    std::string WddmLabel(D3DKMT_DRIVERVERSION version)
    {
        const int raw = static_cast<int>(version);
        if (raw < 1000)
            return "unknown";

        const int major = raw / 1000;
        const int minor = (raw % 1000) / 100;

        std::ostringstream out;
        out << major << "." << minor;
        return out.str();
    }

    struct ProbeResult
    {
        ULONG Index = 0;
        std::string Name;
        LONG LuidHighPart = 0;
        ULONG LuidLowPart = 0;
        ULONG Sources = 0;
        int WddmRaw = 0;
        std::string Wddm;
        bool HasPciAddress = false;
        UINT Bus = 0;
        UINT Device = 0;
        UINT Function = 0;
        bool HasType = false;
        bool RenderSupported = false;
        bool DisplaySupported = false;
        bool SoftwareDevice = false;
        bool PostDevice = false;
        bool HybridDiscrete = false;
        bool HybridIntegrated = false;
        bool IndirectDisplayDevice = false;
        bool Paravirtualized = false;
    };

    ProbeResult InspectAdapter(ULONG index, const D3DKMT_ADAPTERINFO& adapter)
    {
        ProbeResult result;
        result.Index = index;
        result.LuidHighPart = adapter.AdapterLuid.HighPart;
        result.LuidLowPart = adapter.AdapterLuid.LowPart;
        result.Sources = adapter.NumOfSources;

        D3DKMT_ADAPTERREGISTRYINFO registryInfo = {};
        if (QueryAdapter(adapter.hAdapter, KMTQAITYPE_ADAPTERREGISTRYINFO, registryInfo))
        {
            result.Name = WideToUtf8(registryInfo.AdapterString);
        }

        D3DKMT_DRIVERVERSION driverVersion = {};
        if (QueryAdapter(adapter.hAdapter, KMTQAITYPE_DRIVERVERSION, driverVersion))
        {
            result.WddmRaw = static_cast<int>(driverVersion);
            result.Wddm = WddmLabel(driverVersion);
        }

        D3DKMT_ADAPTERADDRESS address = {};
        if (QueryAdapter(adapter.hAdapter, KMTQAITYPE_ADAPTERADDRESS, address))
        {
            result.HasPciAddress = true;
            result.Bus = address.BusNumber;
            result.Device = address.DeviceNumber;
            result.Function = address.FunctionNumber;
        }

        D3DKMT_ADAPTERTYPE adapterType = {};
        if (QueryAdapter(adapter.hAdapter, KMTQAITYPE_ADAPTERTYPE, adapterType))
        {
            result.HasType = true;
            result.RenderSupported = adapterType.RenderSupported != 0;
            result.DisplaySupported = adapterType.DisplaySupported != 0;
            result.SoftwareDevice = adapterType.SoftwareDevice != 0;
            result.PostDevice = adapterType.PostDevice != 0;
            result.HybridDiscrete = adapterType.HybridDiscrete != 0;
            result.HybridIntegrated = adapterType.HybridIntegrated != 0;
            result.IndirectDisplayDevice = adapterType.IndirectDisplayDevice != 0;
            result.Paravirtualized = adapterType.Paravirtualized != 0;
        }

        if (result.Name.empty())
            result.Name = "Unnamed WDDM adapter";

        return result;
    }

    void PrintJson(const std::vector<ProbeResult>& results)
    {
        std::cout << "[\n";
        for (size_t i = 0; i < results.size(); ++i)
        {
            const ProbeResult& item = results[i];

            std::cout
                << "  {"
                << "\"index\":" << item.Index
                << ",\"name\":\"" << JsonEscape(item.Name) << "\""
                << ",\"luidHighPart\":" << item.LuidHighPart
                << ",\"luidLowPart\":" << item.LuidLowPart
                << ",\"sources\":" << item.Sources
                << ",\"wddmRaw\":" << item.WddmRaw
                << ",\"wddm\":\"" << JsonEscape(item.Wddm) << "\"";

            if (item.HasPciAddress)
            {
                std::cout
                    << ",\"pci\":{"
                    << "\"bus\":" << item.Bus
                    << ",\"device\":" << item.Device
                    << ",\"function\":" << item.Function
                    << "}";
            }
            else
            {
                std::cout << ",\"pci\":null";
            }

            if (item.HasType)
            {
                std::cout
                    << ",\"type\":{"
                    << "\"renderSupported\":" << (item.RenderSupported ? "true" : "false")
                    << ",\"displaySupported\":" << (item.DisplaySupported ? "true" : "false")
                    << ",\"softwareDevice\":" << (item.SoftwareDevice ? "true" : "false")
                    << ",\"postDevice\":" << (item.PostDevice ? "true" : "false")
                    << ",\"hybridDiscrete\":" << (item.HybridDiscrete ? "true" : "false")
                    << ",\"hybridIntegrated\":" << (item.HybridIntegrated ? "true" : "false")
                    << ",\"indirectDisplayDevice\":" << (item.IndirectDisplayDevice ? "true" : "false")
                    << ",\"paravirtualized\":" << (item.Paravirtualized ? "true" : "false")
                    << "}";
            }
            else
            {
                std::cout << ",\"type\":null";
            }

            std::cout << "}";
            if (i + 1 != results.size())
                std::cout << ",";
            std::cout << "\n";
        }
        std::cout << "]\n";
    }
}

int main()
{
    D3DKMT_ENUMADAPTERS2 enumeration = {};
    NTSTATUS status = D3DKMTEnumAdapters2(&enumeration);
    if (!NtSuccess(status))
    {
        std::cerr << "D3DKMTEnumAdapters2(size) failed: 0x"
                  << std::hex << static_cast<ULONG>(status) << std::dec << "\n";
        return 1;
    }

    if (enumeration.NumAdapters == 0)
    {
        std::cout << "[]\n";
        return 0;
    }

    std::vector<D3DKMT_ADAPTERINFO> adapters(enumeration.NumAdapters);
    enumeration.pAdapters = adapters.data();

    status = D3DKMTEnumAdapters2(&enumeration);
    if (!NtSuccess(status))
    {
        std::cerr << "D3DKMTEnumAdapters2(data) failed: 0x"
                  << std::hex << static_cast<ULONG>(status) << std::dec << "\n";
        return 2;
    }

    adapters.resize(enumeration.NumAdapters);

    std::vector<ProbeResult> results;
    results.reserve(adapters.size());

    for (ULONG i = 0; i < static_cast<ULONG>(adapters.size()); ++i)
    {
        results.push_back(InspectAdapter(i, adapters[i]));
    }

    PrintJson(results);

    for (const auto& adapter : adapters)
    {
        D3DKMT_CLOSEADAPTER closeAdapter = {};
        closeAdapter.hAdapter = adapter.hAdapter;
        D3DKMTCloseAdapter(&closeAdapter);
    }

    return 0;
}
