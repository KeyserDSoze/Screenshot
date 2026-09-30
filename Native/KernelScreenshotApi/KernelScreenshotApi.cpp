#include "KernelScreenshotApi.h"

#include <windows.h>
#include <winternl.h>
#include <d3dkmthk.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_5.h>
#include <roapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    thread_local std::string g_LastError;
    thread_local std::vector<uint8_t> g_PendingBmp;
    thread_local uint32_t g_PendingBmpDeviceIndex = 0;
    thread_local bool g_PendingBmpAuto = false;
    thread_local std::string g_PendingBmpDisplayName;
    thread_local bool g_HasPendingBmp = false;


    struct CaptureReport
    {
        bool Available = false;
        std::string RequestMode;
        bool HasRequestedDeviceIndex = false;
        uint32_t RequestedDeviceIndex = 0;
        std::string RequestedDisplayName;
        std::string AutoCandidateKind;

        bool HasAttemptedDeviceIndex = false;
        uint32_t AttemptedDeviceIndex = 0;

        LUID AdapterLuid = {};
        bool HasAdapterLuid = false;
        std::string AdapterName;

        std::string GdiDeviceName;
        bool HasVidPnSourceId = false;
        uint32_t VidPnSourceId = 0;
        bool HasTargetId = false;
        uint32_t TargetId = 0;

        std::string Backend;
        std::string Route;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint64_t BmpBytes = 0;
    };

    thread_local CaptureReport g_LastCaptureReport;

    void ResetCaptureReport()
    {
        g_LastCaptureReport = {};
    }


    namespace igcl_abi
    {
        using result_t = uint32_t;
        using api_handle_t = void*;
        using device_handle_t = void*;
        using display_handle_t = void*;

        constexpr result_t RESULT_SUCCESS = 0;
        constexpr uint32_t IMPL_VERSION = (1u << 16) | 1u;
        constexpr uint32_t ADAPTER_FLAG_INTEGRATED = 1u << 0;
        constexpr uint32_t DISPLAY_CONFIG_ACTIVE = 1u << 0;
        constexpr uint32_t DISPLAY_CONFIG_ATTACHED = 1u << 1;
        constexpr uint32_t DISPLAY_CONFIG_DITHERING = 1u << 3;

        struct application_id_t
        {
            uint32_t Data1;
            uint16_t Data2;
            uint16_t Data3;
            uint8_t Data4[8];
        };

        struct init_args_t
        {
            uint32_t Size;
            uint8_t Version;
            uint32_t AppVersion;
            uint32_t flags;
            uint32_t SupportedVersion;
            application_id_t ApplicationUID;
        };

        struct firmware_version_t
        {
            uint64_t major_version;
            uint64_t minor_version;
            uint64_t build_number;
        };

        struct adapter_bdf_t
        {
            uint8_t bus;
            uint8_t device;
            uint8_t function;
        };

        struct device_adapter_properties_t
        {
            uint32_t Size;
            uint8_t Version;
            void* pDeviceID;
            uint32_t device_id_size;
            uint32_t device_type;
            uint32_t supported_subfunction_flags;
            uint64_t driver_version;
            firmware_version_t firmware_version;
            uint32_t pci_vendor_id;
            uint32_t pci_device_id;
            uint32_t rev_id;
            uint32_t num_eus_per_sub_slice;
            uint32_t num_sub_slices_per_slice;
            uint32_t num_slices;
            char name[100];
            uint32_t graphics_adapter_properties;
            uint32_t Frequency;
            uint16_t pci_subsys_id;
            uint16_t pci_subsys_vendor_id;
            adapter_bdf_t adapter_bdf;
            uint32_t num_xe_cores;
            char reserved[108];
        };

        struct generic_void_datatype_t
        {
            void* pData;
            uint32_t size;
        };

        union os_display_encoder_identifier_t
        {
            uint32_t WindowsDisplayEncoderID;
            generic_void_datatype_t DisplayEncoderID;
        };

        struct revision_datatype_t
        {
            uint8_t major_version;
            uint8_t minor_version;
            uint8_t revision_version;
        };

        struct display_timing_t
        {
            uint32_t Size;
            uint8_t Version;
            uint64_t PixelClock;
            uint32_t HActive;
            uint32_t VActive;
            uint32_t HTotal;
            uint32_t VTotal;
            uint32_t HBlank;
            uint32_t VBlank;
            uint32_t HSync;
            uint32_t VSync;
            float RefreshRate;
            uint32_t SignalStandard;
            uint8_t VicId;
        };

        struct display_properties_t
        {
            uint32_t Size;
            uint8_t Version;
            os_display_encoder_identifier_t Os_display_encoder_handle;
            uint32_t Type;
            uint32_t AttachedDisplayMuxType;
            uint32_t ProtocolConverterOutput;
            revision_datatype_t SupportedSpec;
            uint32_t SupportedOutputBPCFlags;
            uint32_t ProtocolConverterType;
            uint32_t DisplayConfigFlags;
            uint32_t FeatureEnabledFlags;
            uint32_t FeatureSupportedFlags;
            uint32_t AdvancedFeatureEnabledFlags;
            uint32_t AdvancedFeatureSupportedFlags;
            display_timing_t Display_Timing_Info;
            uint32_t ReservedFields[16];
        };

        struct wire_format_t
        {
            uint32_t Size;
            uint8_t Version;
            uint32_t ColorModel;
            uint32_t ColorDepth;
        };

        struct get_set_wire_format_config_t
        {
            uint32_t Size;
            uint8_t Version;
            uint32_t Operation;
            wire_format_t SupportedWireFormat[4];
            wire_format_t WireFormat;
        };

        using pfn_init_t =
            result_t (__cdecl*)(init_args_t*, api_handle_t*);
        using pfn_close_t =
            result_t (__cdecl*)(api_handle_t);
        using pfn_enumerate_devices_t =
            result_t (__cdecl*)(api_handle_t, uint32_t*, device_handle_t*);
        using pfn_get_device_properties_t =
            result_t (__cdecl*)(device_handle_t, device_adapter_properties_t*);
        using pfn_enumerate_display_outputs_t =
            result_t (__cdecl*)(device_handle_t, uint32_t*, display_handle_t*);
        using pfn_get_display_properties_t =
            result_t (__cdecl*)(display_handle_t, display_properties_t*);
        using pfn_get_set_wire_format_t =
            result_t (__cdecl*)(display_handle_t, get_set_wire_format_config_t*);
    }


    namespace nvapi_abi
    {
        using status_t = int32_t;
        using physical_gpu_handle_t = void*;
        using query_interface_t = void* (__cdecl*)(uint32_t);

        constexpr status_t OK = 0;
        constexpr uint32_t MAX_PHYSICAL_GPUS = 64;
        constexpr uint32_t SHORT_STRING_MAX = 64;

        constexpr uint32_t ID_INITIALIZE = 0x0150e828;
        constexpr uint32_t ID_UNLOAD = 0xd22bdd7e;
        constexpr uint32_t ID_GET_ERROR_MESSAGE = 0x6c2d048c;
        constexpr uint32_t ID_ENUM_PHYSICAL_GPUS = 0xe5ac921f;
        constexpr uint32_t ID_GPU_GET_FULL_NAME = 0xceee8e9f;
        constexpr uint32_t ID_GPU_GET_PCI_IDENTIFIERS = 0x2ddfb66e;
        constexpr uint32_t ID_GPU_GET_BUS_ID = 0x1be0b8e5;
        constexpr uint32_t ID_GPU_GET_BUS_SLOT_ID = 0x2a0a350f;
        constexpr uint32_t ID_GPU_GET_CONNECTED_DISPLAY_IDS = 0x0078dba2;
        constexpr uint32_t ID_GPU_GET_OUTPUT_TYPE = 0x40a505e4;
        constexpr uint32_t ID_DISP_GET_OUTPUT_MODE = 0x81fed88d;

        struct gpu_display_ids_t
        {
            uint32_t version;
            int32_t connectorType;
            uint32_t displayId;
            uint32_t flags;
        };

        constexpr uint32_t GPU_DISPLAYIDS_VER =
            static_cast<uint32_t>(
                sizeof(gpu_display_ids_t)) |
            (3u << 16);

        using pfn_initialize_t =
            status_t (__cdecl*)();
        using pfn_unload_t =
            status_t (__cdecl*)();
        using pfn_get_error_message_t =
            status_t (__cdecl*)(status_t, char*);
        using pfn_enum_physical_gpus_t =
            status_t (__cdecl*)(
                physical_gpu_handle_t*,
                uint32_t*);
        using pfn_gpu_get_full_name_t =
            status_t (__cdecl*)(
                physical_gpu_handle_t,
                char*);
        using pfn_gpu_get_pci_identifiers_t =
            status_t (__cdecl*)(
                physical_gpu_handle_t,
                uint32_t*,
                uint32_t*,
                uint32_t*,
                uint32_t*);
        using pfn_gpu_get_bus_id_t =
            status_t (__cdecl*)(
                physical_gpu_handle_t,
                uint32_t*);
        using pfn_gpu_get_bus_slot_id_t =
            status_t (__cdecl*)(
                physical_gpu_handle_t,
                uint32_t*);
        using pfn_gpu_get_connected_display_ids_t =
            status_t (__cdecl*)(
                physical_gpu_handle_t,
                gpu_display_ids_t*,
                uint32_t*,
                uint32_t);
        using pfn_gpu_get_output_type_t =
            status_t (__cdecl*)(
                physical_gpu_handle_t,
                uint32_t,
                int32_t*);
        using pfn_disp_get_output_mode_t =
            status_t (__cdecl*)(
                uint32_t,
                int32_t*);
    }

    void SetError(const std::string& value)
    {
        g_LastError = value;
    }

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


    std::wstring Utf8ToWide(const char* value)
    {
        if (value == nullptr || *value == '\0')
            return {};

        const int required =
            MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value,
                -1,
                nullptr,
                0);

        if (required <= 1)
            return {};

        std::wstring result(
            static_cast<size_t>(required),
            L'\0');

        const int written =
            MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value,
                -1,
                result.data(),
                required);

        if (written <= 1)
            return {};

        result.resize(
            static_cast<size_t>(written - 1));

        return result;
    }

    std::wstring NormalizeGdiDisplayName(
        const char* displayName)
    {
        std::wstring value =
            Utf8ToWide(displayName);

        if (value.empty())
            return {};

        if (value.rfind(L"\\\\.\\", 0) == 0)
            return value;

        if (_wcsnicmp(
                value.c_str(),
                L"DISPLAY",
                7) == 0)
        {
            return L"\\\\.\\" + value;
        }

        return value;
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

    std::string HResultText(HRESULT hr)
    {
        std::ostringstream out;
        out << "0x"
            << std::hex
            << std::uppercase
            << static_cast<unsigned long>(hr);
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

    struct DeviceInfo
    {
        uint32_t Index = 0;
        std::string Name;
        LUID Luid = {};
        uint32_t Sources = 0;
        std::string Wddm;

        bool HasPciAddress = false;
        uint32_t Bus = 0;
        uint32_t Device = 0;
        uint32_t Function = 0;

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

    struct OutputInfo
    {
        UINT Index = 0;
        DXGI_OUTPUT_DESC Desc = {};
    };


    struct ActiveDisplayPath
    {
        LUID AdapterLuid = {};
        UINT32 SourceId = 0;
        UINT32 TargetId = 0;
        std::wstring GdiDeviceName;
        HMONITOR Monitor = nullptr;
    };

    BOOL CALLBACK FindMonitorByGdiNameCallback(
        HMONITOR monitor,
        HDC,
        LPRECT,
        LPARAM parameter)
    {
        auto* state =
            reinterpret_cast<std::pair<
                const wchar_t*,
                HMONITOR*>*>(parameter);

        if (state == nullptr ||
            state->first == nullptr ||
            state->second == nullptr)
        {
            return TRUE;
        }

        MONITORINFOEXW info = {};
        info.cbSize = sizeof(info);

        if (GetMonitorInfoW(
                monitor,
                &info) &&
            _wcsicmp(
                info.szDevice,
                state->first) == 0)
        {
            *state->second = monitor;
            return FALSE;
        }

        return TRUE;
    }

    HMONITOR FindMonitorByGdiName(
        const wchar_t* deviceName)
    {
        if (deviceName == nullptr ||
            *deviceName == L'\0')
        {
            return nullptr;
        }

        HMONITOR result = nullptr;
        std::pair<const wchar_t*, HMONITOR*> state{
            deviceName,
            &result
        };

        EnumDisplayMonitors(
            nullptr,
            nullptr,
            FindMonitorByGdiNameCallback,
            reinterpret_cast<LPARAM>(&state));

        return result;
    }

    bool GetActiveDisplayPaths(
        std::vector<ActiveDisplayPath>& activePaths)
    {
        activePaths.clear();

        constexpr UINT32 flags =
            QDC_ONLY_ACTIVE_PATHS;

        UINT32 pathCount = 0;
        UINT32 modeCount = 0;

        LONG result =
            GetDisplayConfigBufferSizes(
                flags,
                &pathCount,
                &modeCount);

        if (result != ERROR_SUCCESS)
            return false;

        std::vector<DISPLAYCONFIG_PATH_INFO>
            paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO>
            modes(modeCount);

        do
        {
            result =
                QueryDisplayConfig(
                    flags,
                    &pathCount,
                    paths.data(),
                    &modeCount,
                    modes.data(),
                    nullptr);

            if (result ==
                ERROR_INSUFFICIENT_BUFFER)
            {
                result =
                    GetDisplayConfigBufferSizes(
                        flags,
                        &pathCount,
                        &modeCount);

                if (result != ERROR_SUCCESS)
                    return false;

                paths.assign(pathCount, {});
                modes.assign(modeCount, {});
            }
        }
        while (result ==
               ERROR_INSUFFICIENT_BUFFER);

        if (result != ERROR_SUCCESS)
            return false;

        paths.resize(pathCount);

        for (const auto& path : paths)
        {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
            source.header.type =
                DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            source.header.size = sizeof(source);
            source.header.adapterId =
                path.sourceInfo.adapterId;
            source.header.id =
                path.sourceInfo.id;

            if (DisplayConfigGetDeviceInfo(
                    &source.header) !=
                ERROR_SUCCESS)
            {
                continue;
            }

            ActiveDisplayPath item;
            item.AdapterLuid =
                path.sourceInfo.adapterId;
            item.SourceId =
                path.sourceInfo.id;
            item.TargetId =
                path.targetInfo.id;
            item.GdiDeviceName =
                source.viewGdiDeviceName;
            item.Monitor =
                FindMonitorByGdiName(
                    source.viewGdiDeviceName);

            activePaths.push_back(
                std::move(item));
        }

        return true;
    }

    bool SameLuid(const LUID& left, const LUID& right)
    {
        return left.HighPart == right.HighPart &&
               left.LowPart == right.LowPart;
    }


    bool MapGdiDisplayToKmtAdapter(
        const wchar_t* deviceName,
        LUID& adapterLuid,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID& sourceId)
    {
        if (deviceName == nullptr || *deviceName == L'\0')
            return false;

        D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME open = {};
        wcsncpy_s(
            open.DeviceName,
            ARRAYSIZE(open.DeviceName),
            deviceName,
            _TRUNCATE);

        const NTSTATUS status =
            D3DKMTOpenAdapterFromGdiDisplayName(&open);

        if (!NtSuccess(status))
            return false;

        adapterLuid = open.AdapterLuid;
        sourceId = open.VidPnSourceId;

        D3DKMT_CLOSEADAPTER close = {};
        close.hAdapter = open.hAdapter;
        D3DKMTCloseAdapter(&close);

        return true;
    }

    int EnumerateDevices(std::vector<DeviceInfo>& devices)
    {
        devices.clear();

        D3DKMT_ENUMADAPTERS2 enumeration = {};
        NTSTATUS status = D3DKMTEnumAdapters2(&enumeration);
        if (!NtSuccess(status))
        {
            std::ostringstream error;
            error << "D3DKMTEnumAdapters2(size) failed: 0x"
                  << std::hex
                  << static_cast<ULONG>(status);
            SetError(error.str());
            return KS_ENUMERATION_FAILED;
        }

        if (enumeration.NumAdapters == 0)
            return KS_OK;

        std::vector<D3DKMT_ADAPTERINFO> adapters(enumeration.NumAdapters);
        enumeration.pAdapters = adapters.data();

        status = D3DKMTEnumAdapters2(&enumeration);
        if (!NtSuccess(status))
        {
            std::ostringstream error;
            error << "D3DKMTEnumAdapters2(data) failed: 0x"
                  << std::hex
                  << static_cast<ULONG>(status);
            SetError(error.str());
            return KS_ENUMERATION_FAILED;
        }

        adapters.resize(enumeration.NumAdapters);
        devices.reserve(adapters.size());

        for (uint32_t i = 0; i < static_cast<uint32_t>(adapters.size()); ++i)
        {
            const D3DKMT_ADAPTERINFO& adapter = adapters[i];

            DeviceInfo item;
            item.Index = i;
            item.Luid = adapter.AdapterLuid;
            item.Sources = adapter.NumOfSources;

            D3DKMT_ADAPTERREGISTRYINFO registryInfo = {};
            if (QueryAdapter(adapter.hAdapter, KMTQAITYPE_ADAPTERREGISTRYINFO, registryInfo))
                item.Name = WideToUtf8(registryInfo.AdapterString);

            D3DKMT_DRIVERVERSION driverVersion = {};
            if (QueryAdapter(adapter.hAdapter, KMTQAITYPE_DRIVERVERSION, driverVersion))
                item.Wddm = WddmLabel(driverVersion);

            D3DKMT_ADAPTERADDRESS address = {};
            if (QueryAdapter(adapter.hAdapter, KMTQAITYPE_ADAPTERADDRESS, address))
            {
                item.HasPciAddress = true;
                item.Bus = address.BusNumber;
                item.Device = address.DeviceNumber;
                item.Function = address.FunctionNumber;
            }

            D3DKMT_ADAPTERTYPE adapterType = {};
            if (QueryAdapter(adapter.hAdapter, KMTQAITYPE_ADAPTERTYPE, adapterType))
            {
                item.HasType = true;
                item.RenderSupported = adapterType.RenderSupported != 0;
                item.DisplaySupported = adapterType.DisplaySupported != 0;
                item.SoftwareDevice = adapterType.SoftwareDevice != 0;
                item.PostDevice = adapterType.PostDevice != 0;
                item.HybridDiscrete = adapterType.HybridDiscrete != 0;
                item.HybridIntegrated = adapterType.HybridIntegrated != 0;
                item.IndirectDisplayDevice = adapterType.IndirectDisplayDevice != 0;
                item.Paravirtualized = adapterType.Paravirtualized != 0;
            }

            if (item.Name.empty())
                item.Name = "Unnamed WDDM adapter";

            if (item.Wddm.empty())
                item.Wddm = "unknown";

            devices.push_back(item);
        }

        for (const D3DKMT_ADAPTERINFO& adapter : adapters)
        {
            D3DKMT_CLOSEADAPTER closeAdapter = {};
            closeAdapter.hAdapter = adapter.hAdapter;
            D3DKMTCloseAdapter(&closeAdapter);
        }

        return KS_OK;
    }

    ComPtr<IDXGIAdapter1> FindDxgiAdapter(
        IDXGIFactory1* factory,
        const LUID& luid)
    {
        if (factory == nullptr)
            return nullptr;

        for (UINT index = 0;; ++index)
        {
            ComPtr<IDXGIAdapter1> adapter;
            const HRESULT hr = factory->EnumAdapters1(index, adapter.GetAddressOf());
            if (hr == DXGI_ERROR_NOT_FOUND)
                break;

            if (FAILED(hr))
                break;

            DXGI_ADAPTER_DESC1 desc = {};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
                SameLuid(desc.AdapterLuid, luid))
            {
                return adapter;
            }
        }

        return nullptr;
    }


    bool ReadBmpDimensions(
        const std::vector<uint8_t>& bmpBytes,
        uint32_t& width,
        uint32_t& height)
    {
        width = 0;
        height = 0;

        const size_t headerBytes =
            sizeof(BITMAPFILEHEADER) +
            sizeof(BITMAPINFOHEADER);

        if (bmpBytes.size() < headerBytes)
            return false;

        BITMAPFILEHEADER fileHeader = {};
        BITMAPINFOHEADER infoHeader = {};

        std::memcpy(
            &fileHeader,
            bmpBytes.data(),
            sizeof(fileHeader));

        std::memcpy(
            &infoHeader,
            bmpBytes.data() +
                sizeof(fileHeader),
            sizeof(infoHeader));

        if (fileHeader.bfType != 0x4D42 ||
            infoHeader.biWidth <= 0 ||
            infoHeader.biHeight == 0)
        {
            return false;
        }

        width =
            static_cast<uint32_t>(
                infoHeader.biWidth);

        const int64_t signedHeight =
            static_cast<int64_t>(
                infoHeader.biHeight);

        height =
            static_cast<uint32_t>(
                signedHeight < 0
                ? -signedHeight
                : signedHeight);

        return true;
    }

    std::string GetDxgiAdapterName(
        const LUID& adapterLuid)
    {
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(
                CreateDXGIFactory1(
                    IID_PPV_ARGS(
                        factory.GetAddressOf()))))
        {
            return {};
        }

        ComPtr<IDXGIAdapter1> adapter =
            FindDxgiAdapter(
                factory.Get(),
                adapterLuid);

        if (adapter == nullptr)
            return {};

        DXGI_ADAPTER_DESC1 desc = {};
        if (FAILED(adapter->GetDesc1(&desc)))
            return {};

        return WideToUtf8(desc.Description);
    }

    void RecordCaptureSuccess(
        const LUID& adapterLuid,
        const wchar_t* displayName,
        const char* backend,
        const char* route,
        const std::vector<uint8_t>& bmpBytes,
        bool hasAttemptedDeviceIndex,
        uint32_t attemptedDeviceIndex)
    {
        CaptureReport report;
        report.Available = true;
        report.HasAdapterLuid = true;
        report.AdapterLuid = adapterLuid;
        report.AdapterName =
            GetDxgiAdapterName(adapterLuid);
        report.Backend =
            backend != nullptr ? backend : "";
        report.Route =
            route != nullptr ? route : "";
        report.BmpBytes =
            static_cast<uint64_t>(
                bmpBytes.size());
        report.HasAttemptedDeviceIndex =
            hasAttemptedDeviceIndex;
        report.AttemptedDeviceIndex =
            attemptedDeviceIndex;

        if (displayName != nullptr &&
            *displayName != L'\0')
        {
            report.GdiDeviceName =
                WideToUtf8(displayName);

            std::vector<ActiveDisplayPath>
                activePaths;

            if (GetActiveDisplayPaths(
                    activePaths))
            {
                for (const ActiveDisplayPath& path :
                     activePaths)
                {
                    if (_wcsicmp(
                            path.GdiDeviceName.c_str(),
                            displayName) != 0)
                    {
                        continue;
                    }

                    report.AdapterLuid =
                        path.AdapterLuid;
                    report.HasAdapterLuid =
                        true;
                    report.AdapterName =
                        GetDxgiAdapterName(
                            path.AdapterLuid);
                    report.HasVidPnSourceId =
                        true;
                    report.VidPnSourceId =
                        path.SourceId;
                    report.HasTargetId =
                        true;
                    report.TargetId =
                        path.TargetId;
                    break;
                }
            }
        }

        ReadBmpDimensions(
            bmpBytes,
            report.Width,
            report.Height);

        g_LastCaptureReport =
            std::move(report);
    }

    std::string BuildCaptureReportJson()
    {
        const CaptureReport& report =
            g_LastCaptureReport;

        std::ostringstream json;
        json
            << "{"
            << "\"available\":"
            << (report.Available
                ? "true"
                : "false");

        if (!report.Available)
        {
            json << "}";
            return json.str();
        }

        json
            << ",\"request\":{"
            << "\"mode\":\""
            << JsonEscape(report.RequestMode)
            << "\""
            << ",\"requestedDeviceIndex\":";

        if (report.HasRequestedDeviceIndex)
            json << report.RequestedDeviceIndex;
        else
            json << "null";

        json << ",\"requestedDisplayName\":";

        if (!report.RequestedDisplayName.empty())
        {
            json
                << "\""
                << JsonEscape(
                    report.RequestedDisplayName)
                << "\"";
        }
        else
        {
            json << "null";
        }

        json << ",\"autoCandidateKind\":";

        if (!report.AutoCandidateKind.empty())
        {
            json
                << "\""
                << JsonEscape(
                    report.AutoCandidateKind)
                << "\"";
        }
        else
        {
            json << "null";
        }

        json
            << "}"
            << ",\"result\":{"
            << "\"attemptedDeviceIndex\":";

        if (report.HasAttemptedDeviceIndex)
            json << report.AttemptedDeviceIndex;
        else
            json << "null";

        json
            << ",\"adapter\":{"
            << "\"name\":";

        if (!report.AdapterName.empty())
        {
            json
                << "\""
                << JsonEscape(
                    report.AdapterName)
                << "\"";
        }
        else
        {
            json << "null";
        }

        json << ",\"luidHighPart\":";
        if (report.HasAdapterLuid)
            json << report.AdapterLuid.HighPart;
        else
            json << "null";

        json << ",\"luidLowPart\":";
        if (report.HasAdapterLuid)
            json << report.AdapterLuid.LowPart;
        else
            json << "null";

        json
            << "}"
            << ",\"display\":{"
            << "\"gdiDeviceName\":";

        if (!report.GdiDeviceName.empty())
        {
            json
                << "\""
                << JsonEscape(
                    report.GdiDeviceName)
                << "\"";
        }
        else
        {
            json << "null";
        }

        json << ",\"vidPnSourceId\":";
        if (report.HasVidPnSourceId)
            json << report.VidPnSourceId;
        else
            json << "null";

        json << ",\"targetId\":";
        if (report.HasTargetId)
            json << report.TargetId;
        else
            json << "null";

        json
            << "}"
            << ",\"backend\":\""
            << JsonEscape(report.Backend)
            << "\""
            << ",\"route\":\""
            << JsonEscape(report.Route)
            << "\""
            << ",\"width\":"
            << report.Width
            << ",\"height\":"
            << report.Height
            << ",\"bmpBytes\":"
            << report.BmpBytes
            << "}"
            << "}";

        return json.str();
    }

    void AppendOutputsJson(
        std::ostringstream& json,
        IDXGIFactory1* factory,
        const LUID& luid)
    {
        std::vector<OutputInfo> outputs;

        ComPtr<IDXGIAdapter1> adapter = FindDxgiAdapter(factory, luid);
        if (adapter != nullptr)
        {
            for (UINT index = 0;; ++index)
            {
                ComPtr<IDXGIOutput> output;
                const HRESULT hr = adapter->EnumOutputs(index, output.GetAddressOf());
                if (hr == DXGI_ERROR_NOT_FOUND)
                    break;

                if (FAILED(hr))
                    break;

                DXGI_OUTPUT_DESC desc = {};
                if (FAILED(output->GetDesc(&desc)))
                    continue;

                OutputInfo item;
                item.Index = index;
                item.Desc = desc;
                outputs.push_back(item);
            }
        }

        UINT attachedOutputCount = 0;
        for (const OutputInfo& output : outputs)
        {
            if (output.Desc.AttachedToDesktop)
                ++attachedOutputCount;
        }

        json
            << "\"outputCount\":" << outputs.size()
            << ",\"attachedOutputCount\":" << attachedOutputCount
            << ",\"hasAttachedDesktopOutput\":"
            << (attachedOutputCount != 0 ? "true" : "false")
            << ",\"outputs\":[";

        for (size_t i = 0; i < outputs.size(); ++i)
        {
            if (i != 0)
                json << ",";

            const OutputInfo& output = outputs[i];
            const DXGI_OUTPUT_DESC& desc = output.Desc;

            json
                << "{"
                << "\"index\":" << output.Index
                << ",\"name\":\"" << JsonEscape(WideToUtf8(desc.DeviceName)) << "\""
                << ",\"attachedToDesktop\":" << (desc.AttachedToDesktop ? "true" : "false")
                << ",\"desktopLeft\":" << desc.DesktopCoordinates.left
                << ",\"desktopTop\":" << desc.DesktopCoordinates.top
                << ",\"desktopRight\":" << desc.DesktopCoordinates.right
                << ",\"desktopBottom\":" << desc.DesktopCoordinates.bottom
                << ",\"rotation\":" << static_cast<unsigned int>(desc.Rotation);

            LUID kmtLuid = {};
            D3DDDI_VIDEO_PRESENT_SOURCE_ID sourceId = 0;
            if (MapGdiDisplayToKmtAdapter(
                    desc.DeviceName,
                    kmtLuid,
                    sourceId))
            {
                json
                    << ",\"kmtAdapterLuidHighPart\":" << kmtLuid.HighPart
                    << ",\"kmtAdapterLuidLowPart\":" << kmtLuid.LowPart
                    << ",\"vidPnSourceId\":" << sourceId
                    << ",\"kmtMatchesDxgiAdapter\":"
                    << (SameLuid(kmtLuid, luid) ? "true" : "false");
            }
            else
            {
                json
                    << ",\"kmtAdapterLuidHighPart\":null"
                    << ",\"kmtAdapterLuidLowPart\":null"
                    << ",\"vidPnSourceId\":null"
                    << ",\"kmtMatchesDxgiAdapter\":null";
            }

            json << "}";
        }

        json << "]";
    }

    int BuildDevicesJson(std::string& jsonText)
    {
        std::vector<DeviceInfo> devices;
        const int enumerateStatus = EnumerateDevices(devices);
        if (enumerateStatus != KS_OK)
            return enumerateStatus;

        ComPtr<IDXGIFactory1> factory;
        const HRESULT factoryHr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));

        std::ostringstream json;
        json << "[";

        for (size_t i = 0; i < devices.size(); ++i)
        {
            if (i != 0)
                json << ",";

            const DeviceInfo& item = devices[i];

            json
                << "{"
                << "\"index\":" << item.Index
                << ",\"name\":\"" << JsonEscape(item.Name) << "\""
                << ",\"luidHighPart\":" << item.Luid.HighPart
                << ",\"luidLowPart\":" << item.Luid.LowPart
                << ",\"sources\":" << item.Sources
                << ",\"wddm\":\"" << JsonEscape(item.Wddm) << "\"";

            if (item.HasPciAddress)
            {
                json
                    << ",\"pci\":{"
                    << "\"bus\":" << item.Bus
                    << ",\"device\":" << item.Device
                    << ",\"function\":" << item.Function
                    << "}";
            }
            else
            {
                json << ",\"pci\":null";
            }

            if (item.HasType)
            {
                json
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
                json << ",\"type\":null";
            }

            json << ",";

            if (SUCCEEDED(factoryHr))
                AppendOutputsJson(json, factory.Get(), item.Luid);
            else
                json << "\"outputCount\":0,\"attachedOutputCount\":0,\"hasAttachedDesktopOutput\":false,\"outputs\":[]";

            json << "}";
        }

        json << "]";
        jsonText = json.str();
        return KS_OK;
    }



    std::string IgclOutputTypeLabel(uint32_t value)
    {
        switch (value)
        {
        case 1: return "DisplayPort";
        case 2: return "HDMI";
        case 3: return "DVI";
        case 4: return "MIPI";
        case 5: return "CRT";
        default: return "Invalid";
        }
    }

    std::string IgclMuxTypeLabel(uint32_t value)
    {
        switch (value)
        {
        case 0: return "Native";
        case 1: return "Thunderbolt";
        case 2: return "USB-C";
        case 3: return "USB4";
        default: return "Unknown";
        }
    }

    std::string IgclWireColorModelLabel(uint32_t value)
    {
        switch (value)
        {
        case 0: return "RGB";
        case 1: return "YCbCr420";
        case 2: return "YCbCr422";
        case 3: return "YCbCr444";
        default: return "Unknown";
        }
    }


    std::string NvapiConnectorTypeLabel(int32_t value)
    {
        switch (value)
        {
        case 0: return "Uninitialized";
        case 1: return "VGA";
        case 2: return "Component";
        case 3: return "SVideo";
        case 4: return "HDMI";
        case 5: return "DVI";
        case 6: return "LVDS";
        case 7: return "DisplayPort";
        case 8: return "Composite";
        default: return "Unknown";
        }
    }

    std::string NvapiOutputTypeLabel(int32_t value)
    {
        switch (value)
        {
        case 1: return "CRT";
        case 2: return "DigitalFlatPanel";
        case 3: return "TV";
        default: return "Unknown";
        }
    }

    std::string NvapiOutputModeLabel(int32_t value)
    {
        switch (value)
        {
        case 0: return "SDR";
        case 1: return "HDR10";
        case 2: return "HDR10PlusGaming";
        default: return "Unknown";
        }
    }

    void AppendNvidiaVendorJson(std::ostringstream& json)
    {
        using namespace nvapi_abi;

        HMODULE module = LoadLibraryExW(
            L"nvapi64.dll",
            nullptr,
            LOAD_LIBRARY_SEARCH_SYSTEM32);

        json << "\"nvidia\":{";

        if (module == nullptr)
        {
            json
                << "\"available\":false"
                << ",\"library\":\"nvapi64.dll\""
                << ",\"loadError\":" << GetLastError()
                << "}";
            return;
        }

        auto queryInterface =
            reinterpret_cast<query_interface_t>(
                GetProcAddress(
                    module,
                    "nvapi_QueryInterface"));

        if (queryInterface == nullptr)
        {
            json
                << "\"available\":true"
                << ",\"library\":\"nvapi64.dll\""
                << ",\"error\":\"nvapi_QueryInterface export is missing\""
                << "}";
            FreeLibrary(module);
            return;
        }

        const auto initialize =
            reinterpret_cast<pfn_initialize_t>(
                queryInterface(ID_INITIALIZE));
        const auto unload =
            reinterpret_cast<pfn_unload_t>(
                queryInterface(ID_UNLOAD));
        const auto getErrorMessage =
            reinterpret_cast<pfn_get_error_message_t>(
                queryInterface(ID_GET_ERROR_MESSAGE));
        const auto enumPhysicalGpus =
            reinterpret_cast<pfn_enum_physical_gpus_t>(
                queryInterface(ID_ENUM_PHYSICAL_GPUS));
        const auto getFullName =
            reinterpret_cast<pfn_gpu_get_full_name_t>(
                queryInterface(ID_GPU_GET_FULL_NAME));
        const auto getPciIdentifiers =
            reinterpret_cast<pfn_gpu_get_pci_identifiers_t>(
                queryInterface(ID_GPU_GET_PCI_IDENTIFIERS));
        const auto getBusId =
            reinterpret_cast<pfn_gpu_get_bus_id_t>(
                queryInterface(ID_GPU_GET_BUS_ID));
        const auto getBusSlotId =
            reinterpret_cast<pfn_gpu_get_bus_slot_id_t>(
                queryInterface(ID_GPU_GET_BUS_SLOT_ID));
        const auto getConnectedDisplayIds =
            reinterpret_cast<pfn_gpu_get_connected_display_ids_t>(
                queryInterface(ID_GPU_GET_CONNECTED_DISPLAY_IDS));
        const auto getOutputType =
            reinterpret_cast<pfn_gpu_get_output_type_t>(
                queryInterface(ID_GPU_GET_OUTPUT_TYPE));
        const auto getOutputMode =
            reinterpret_cast<pfn_disp_get_output_mode_t>(
                queryInterface(ID_DISP_GET_OUTPUT_MODE));

        if (initialize == nullptr ||
            enumPhysicalGpus == nullptr)
        {
            json
                << "\"available\":true"
                << ",\"library\":\"nvapi64.dll\""
                << ",\"error\":\"Required NVAPI interfaces are missing\""
                << "}";
            FreeLibrary(module);
            return;
        }

        const status_t initResult = initialize();

        json
            << "\"available\":true"
            << ",\"library\":\"nvapi64.dll\""
            << ",\"initResult\":" << initResult;

        if (initResult != OK)
        {
            if (getErrorMessage != nullptr)
            {
                char errorText[SHORT_STRING_MAX] = {};
                if (getErrorMessage(
                        initResult,
                        errorText) == OK)
                {
                    json
                        << ",\"initError\":\""
                        << JsonEscape(errorText)
                        << "\"";
                }
            }

            json << ",\"gpus\":[]}";
            FreeLibrary(module);
            return;
        }

        physical_gpu_handle_t gpuHandles[
            MAX_PHYSICAL_GPUS] = {};
        uint32_t gpuCount = 0;

        const status_t enumResult =
            enumPhysicalGpus(
                gpuHandles,
                &gpuCount);

        json
            << ",\"enumeratePhysicalGpusResult\":"
            << enumResult
            << ",\"gpus\":[";

        if (enumResult == OK)
        {
            for (uint32_t gpuIndex = 0;
                 gpuIndex < gpuCount;
                 ++gpuIndex)
            {
                if (gpuIndex != 0)
                    json << ",";

                const physical_gpu_handle_t gpu =
                    gpuHandles[gpuIndex];

                json << "{\"index\":" << gpuIndex;

                if (getFullName != nullptr)
                {
                    char name[SHORT_STRING_MAX] = {};
                    const status_t nameResult =
                        getFullName(gpu, name);

                    json
                        << ",\"nameResult\":"
                        << nameResult;

                    if (nameResult == OK)
                    {
                        json
                            << ",\"name\":\""
                            << JsonEscape(name)
                            << "\"";
                    }
                }

                if (getPciIdentifiers != nullptr)
                {
                    uint32_t deviceId = 0;
                    uint32_t subsystemId = 0;
                    uint32_t revisionId = 0;
                    uint32_t externalDeviceId = 0;

                    const status_t pciResult =
                        getPciIdentifiers(
                            gpu,
                            &deviceId,
                            &subsystemId,
                            &revisionId,
                            &externalDeviceId);

                    json
                        << ",\"pciResult\":"
                        << pciResult;

                    if (pciResult == OK)
                    {
                        json
                            << ",\"pciDeviceIdRaw\":"
                            << deviceId
                            << ",\"pciSubsystemIdRaw\":"
                            << subsystemId
                            << ",\"pciRevisionIdRaw\":"
                            << revisionId
                            << ",\"pciExternalDeviceIdRaw\":"
                            << externalDeviceId;
                    }
                }

                if (getBusId != nullptr)
                {
                    uint32_t busId = 0;
                    const status_t busResult =
                        getBusId(
                            gpu,
                            &busId);

                    json
                        << ",\"busIdResult\":"
                        << busResult;

                    if (busResult == OK)
                        json << ",\"pciBus\":"
                             << busId;
                }

                if (getBusSlotId != nullptr)
                {
                    uint32_t slotId = 0;
                    const status_t slotResult =
                        getBusSlotId(
                            gpu,
                            &slotId);

                    json
                        << ",\"busSlotIdResult\":"
                        << slotResult;

                    if (slotResult == OK)
                        json << ",\"pciBusSlot\":"
                             << slotId;
                }

                uint32_t displayCount = 0;
                status_t displayCountResult = -3;

                if (getConnectedDisplayIds != nullptr)
                {
                    displayCountResult =
                        getConnectedDisplayIds(
                            gpu,
                            nullptr,
                            &displayCount,
                            0);
                }

                json
                    << ",\"connectedDisplayCountResult\":"
                    << displayCountResult
                    << ",\"connectedDisplays\":[";

                if (displayCountResult == OK &&
                    displayCount != 0 &&
                    getConnectedDisplayIds != nullptr)
                {
                    std::vector<gpu_display_ids_t> displays(
                        displayCount);

                    for (auto& display : displays)
                        display.version =
                            GPU_DISPLAYIDS_VER;

                    status_t displayResult =
                        getConnectedDisplayIds(
                            gpu,
                            displays.data(),
                            &displayCount,
                            0);

                    if (displayResult == OK)
                    {
                        displays.resize(displayCount);

                        for (size_t displayIndex = 0;
                             displayIndex < displays.size();
                             ++displayIndex)
                        {
                            if (displayIndex != 0)
                                json << ",";

                            const gpu_display_ids_t& display =
                                displays[displayIndex];

                            json
                                << "{"
                                << "\"index\":"
                                << displayIndex
                                << ",\"displayId\":"
                                << display.displayId
                                << ",\"connectorType\":"
                                << display.connectorType
                                << ",\"connectorTypeName\":\""
                                << NvapiConnectorTypeLabel(
                                    display.connectorType)
                                << "\""
                                << ",\"isDynamic\":"
                                << ((display.flags & (1u << 0)) != 0
                                    ? "true"
                                    : "false")
                                << ",\"isMultiStreamRootNode\":"
                                << ((display.flags & (1u << 1)) != 0
                                    ? "true"
                                    : "false")
                                << ",\"isActive\":"
                                << ((display.flags & (1u << 2)) != 0
                                    ? "true"
                                    : "false")
                                << ",\"isCluster\":"
                                << ((display.flags & (1u << 3)) != 0
                                    ? "true"
                                    : "false")
                                << ",\"isOSVisible\":"
                                << ((display.flags & (1u << 4)) != 0
                                    ? "true"
                                    : "false")
                                << ",\"isConnected\":"
                                << ((display.flags & (1u << 6)) != 0
                                    ? "true"
                                    : "false")
                                << ",\"isPhysicallyConnected\":"
                                << ((display.flags & (1u << 17)) != 0
                                    ? "true"
                                    : "false");

                            if (getOutputType != nullptr)
                            {
                                int32_t outputType = 0;
                                const status_t outputTypeResult =
                                    getOutputType(
                                        gpu,
                                        display.displayId,
                                        &outputType);

                                json
                                    << ",\"outputTypeResult\":"
                                    << outputTypeResult;

                                if (outputTypeResult == OK)
                                {
                                    json
                                        << ",\"outputType\":"
                                        << outputType
                                        << ",\"outputTypeName\":\""
                                        << NvapiOutputTypeLabel(
                                            outputType)
                                        << "\"";
                                }
                            }

                            if (getOutputMode != nullptr)
                            {
                                int32_t outputMode = 0;
                                const status_t outputModeResult =
                                    getOutputMode(
                                        display.displayId,
                                        &outputMode);

                                json
                                    << ",\"outputModeResult\":"
                                    << outputModeResult;

                                if (outputModeResult == OK)
                                {
                                    json
                                        << ",\"outputMode\":"
                                        << outputMode
                                        << ",\"outputModeName\":\""
                                        << NvapiOutputModeLabel(
                                            outputMode)
                                        << "\"";
                                }
                            }

                            json << "}";
                        }
                    }
                }

                json << "]}";
            }
        }

        json << "]}";

        if (unload != nullptr)
            unload();

        FreeLibrary(module);
    }

    int BuildVendorPipelinesJson(std::string& jsonText)
    {
        using namespace igcl_abi;

        HMODULE module = LoadLibraryExW(
            L"ControlLib.dll",
            nullptr,
            LOAD_LIBRARY_SEARCH_SYSTEM32);

        std::ostringstream json;
        json << "{\"intel\":{";

        if (module == nullptr)
        {
            json
                << "\"available\":false"
                << ",\"library\":\"ControlLib.dll\""
                << ",\"loadError\":" << GetLastError()
                << "}";

            AppendNvidiaVendorJson(json);
            json << "}";

            jsonText = json.str();
            return KS_OK;
        }

        const auto ctlInit =
            reinterpret_cast<pfn_init_t>(
                GetProcAddress(module, "ctlInit"));
        const auto ctlClose =
            reinterpret_cast<pfn_close_t>(
                GetProcAddress(module, "ctlClose"));
        const auto ctlEnumerateDevices =
            reinterpret_cast<pfn_enumerate_devices_t>(
                GetProcAddress(module, "ctlEnumerateDevices"));
        const auto ctlGetDeviceProperties =
            reinterpret_cast<pfn_get_device_properties_t>(
                GetProcAddress(module, "ctlGetDeviceProperties"));
        const auto ctlEnumerateDisplayOutputs =
            reinterpret_cast<pfn_enumerate_display_outputs_t>(
                GetProcAddress(module, "ctlEnumerateDisplayOutputs"));
        const auto ctlGetDisplayProperties =
            reinterpret_cast<pfn_get_display_properties_t>(
                GetProcAddress(module, "ctlGetDisplayProperties"));
        const auto ctlGetSetWireFormat =
            reinterpret_cast<pfn_get_set_wire_format_t>(
                GetProcAddress(module, "ctlGetSetWireFormat"));

        if (ctlInit == nullptr ||
            ctlClose == nullptr ||
            ctlEnumerateDevices == nullptr ||
            ctlGetDeviceProperties == nullptr ||
            ctlEnumerateDisplayOutputs == nullptr ||
            ctlGetDisplayProperties == nullptr)
        {
            json
                << "\"available\":false"
                << ",\"library\":\"ControlLib.dll\""
                << ",\"error\":\"Required IGCL exports are missing\""
                << "},\"nvidia\":{"
                << "\"available\":"
                << (GetModuleHandleW(L"nvapi64.dll") != nullptr
                    ? "true"
                    : "false")
                << ",\"note\":\"NVAPI deep probe not implemented yet\""
                << "}}";

            FreeLibrary(module);
            jsonText = json.str();
            return KS_OK;
        }

        init_args_t init = {};
        init.Size = sizeof(init);
        init.AppVersion = IMPL_VERSION;

        api_handle_t api = nullptr;
        const result_t initResult =
            ctlInit(&init, &api);

        json
            << "\"available\":true"
            << ",\"library\":\"ControlLib.dll\""
            << ",\"initResult\":" << initResult
            << ",\"requestedVersion\":"
            << IMPL_VERSION
            << ",\"supportedVersion\":"
            << init.SupportedVersion;

        if (initResult != RESULT_SUCCESS || api == nullptr)
        {
            json
                << ",\"adapters\":[]"
                << "}";

            AppendNvidiaVendorJson(json);
            json << "}";

            FreeLibrary(module);
            jsonText = json.str();
            return KS_OK;
        }

        uint32_t adapterCount = 0;
        result_t result =
            ctlEnumerateDevices(
                api,
                &adapterCount,
                nullptr);

        std::vector<device_handle_t> adapters;

        if (result == RESULT_SUCCESS && adapterCount != 0)
        {
            adapters.resize(adapterCount, nullptr);
            result =
                ctlEnumerateDevices(
                    api,
                    &adapterCount,
                    adapters.data());

            if (result != RESULT_SUCCESS)
                adapters.clear();
            else
                adapters.resize(adapterCount);
        }

        json
            << ",\"enumerateDevicesResult\":"
            << result
            << ",\"adapters\":[";

        for (size_t adapterIndex = 0;
             adapterIndex < adapters.size();
             ++adapterIndex)
        {
            if (adapterIndex != 0)
                json << ",";

            LUID luid = {};
            device_adapter_properties_t props = {};
            props.Size = sizeof(props);
            props.pDeviceID = &luid;
            props.device_id_size = sizeof(luid);

            const result_t propsResult =
                ctlGetDeviceProperties(
                    adapters[adapterIndex],
                    &props);

            json
                << "{"
                << "\"index\":" << adapterIndex
                << ",\"propertiesResult\":"
                << propsResult;

            if (propsResult == RESULT_SUCCESS)
            {
                json
                    << ",\"name\":\""
                    << JsonEscape(std::string(props.name))
                    << "\""
                    << ",\"luidHighPart\":"
                    << luid.HighPart
                    << ",\"luidLowPart\":"
                    << luid.LowPart
                    << ",\"pciVendorId\":"
                    << props.pci_vendor_id
                    << ",\"pciDeviceId\":"
                    << props.pci_device_id
                    << ",\"pciSubsysVendorId\":"
                    << props.pci_subsys_vendor_id
                    << ",\"pciSubsysId\":"
                    << props.pci_subsys_id
                    << ",\"pciBus\":"
                    << static_cast<unsigned int>(
                        props.adapter_bdf.bus)
                    << ",\"pciDevice\":"
                    << static_cast<unsigned int>(
                        props.adapter_bdf.device)
                    << ",\"pciFunction\":"
                    << static_cast<unsigned int>(
                        props.adapter_bdf.function)
                    << ",\"integrated\":"
                    << ((props.graphics_adapter_properties &
                         ADAPTER_FLAG_INTEGRATED) != 0
                        ? "true"
                        : "false")
                    << ",\"driverVersionRaw\":"
                    << props.driver_version;
            }

            uint32_t displayCount = 0;
            result_t displayEnumResult =
                ctlEnumerateDisplayOutputs(
                    adapters[adapterIndex],
                    &displayCount,
                    nullptr);

            std::vector<display_handle_t> displays;
            if (displayEnumResult == RESULT_SUCCESS &&
                displayCount != 0)
            {
                displays.resize(displayCount, nullptr);
                displayEnumResult =
                    ctlEnumerateDisplayOutputs(
                        adapters[adapterIndex],
                        &displayCount,
                        displays.data());

                if (displayEnumResult != RESULT_SUCCESS)
                    displays.clear();
                else
                    displays.resize(displayCount);
            }

            json
                << ",\"enumerateDisplaysResult\":"
                << displayEnumResult
                << ",\"displays\":[";

            for (size_t displayIndex = 0;
                 displayIndex < displays.size();
                 ++displayIndex)
            {
                if (displayIndex != 0)
                    json << ",";

                display_properties_t display = {};
                display.Size = sizeof(display);
                display.Display_Timing_Info.Size =
                    sizeof(display.Display_Timing_Info);

                const result_t displayResult =
                    ctlGetDisplayProperties(
                        displays[displayIndex],
                        &display);

                json
                    << "{"
                    << "\"index\":" << displayIndex
                    << ",\"propertiesResult\":"
                    << displayResult;

                if (displayResult == RESULT_SUCCESS)
                {
                    json
                        << ",\"windowsDisplayEncoderId\":"
                        << display
                            .Os_display_encoder_handle
                            .WindowsDisplayEncoderID
                        << ",\"type\":"
                        << display.Type
                        << ",\"typeName\":\""
                        << IgclOutputTypeLabel(display.Type)
                        << "\""
                        << ",\"muxType\":"
                        << display.AttachedDisplayMuxType
                        << ",\"muxTypeName\":\""
                        << IgclMuxTypeLabel(
                            display.AttachedDisplayMuxType)
                        << "\""
                        << ",\"protocolConverterOutput\":"
                        << display.ProtocolConverterOutput
                        << ",\"supportedSpec\":\""
                        << static_cast<unsigned int>(
                            display.SupportedSpec.major_version)
                        << "."
                        << static_cast<unsigned int>(
                            display.SupportedSpec.minor_version)
                        << "."
                        << static_cast<unsigned int>(
                            display.SupportedSpec.revision_version)
                        << "\""
                        << ",\"supportedBpcFlags\":"
                        << display.SupportedOutputBPCFlags
                        << ",\"displayConfigFlags\":"
                        << display.DisplayConfigFlags
                        << ",\"active\":"
                        << ((display.DisplayConfigFlags &
                             DISPLAY_CONFIG_ACTIVE) != 0
                            ? "true"
                            : "false")
                        << ",\"attached\":"
                        << ((display.DisplayConfigFlags &
                             DISPLAY_CONFIG_ATTACHED) != 0
                            ? "true"
                            : "false")
                        << ",\"ditheringEnabled\":"
                        << ((display.DisplayConfigFlags &
                             DISPLAY_CONFIG_DITHERING) != 0
                            ? "true"
                            : "false")
                        << ",\"featureEnabledFlags\":"
                        << display.FeatureEnabledFlags
                        << ",\"featureSupportedFlags\":"
                        << display.FeatureSupportedFlags
                        << ",\"advancedFeatureEnabledFlags\":"
                        << display.AdvancedFeatureEnabledFlags
                        << ",\"advancedFeatureSupportedFlags\":"
                        << display.AdvancedFeatureSupportedFlags
                        << ",\"timing\":{"
                        << "\"pixelClock\":"
                        << display.Display_Timing_Info.PixelClock
                        << ",\"hActive\":"
                        << display.Display_Timing_Info.HActive
                        << ",\"vActive\":"
                        << display.Display_Timing_Info.VActive
                        << ",\"hTotal\":"
                        << display.Display_Timing_Info.HTotal
                        << ",\"vTotal\":"
                        << display.Display_Timing_Info.VTotal
                        << ",\"hBlank\":"
                        << display.Display_Timing_Info.HBlank
                        << ",\"vBlank\":"
                        << display.Display_Timing_Info.VBlank
                        << ",\"hSync\":"
                        << display.Display_Timing_Info.HSync
                        << ",\"vSync\":"
                        << display.Display_Timing_Info.VSync
                        << ",\"refreshRate\":"
                        << display.Display_Timing_Info.RefreshRate
                        << ",\"signalStandard\":"
                        << display.Display_Timing_Info.SignalStandard
                        << ",\"vicId\":"
                        << static_cast<unsigned int>(
                            display.Display_Timing_Info.VicId)
                        << "}";
                }

                if (ctlGetSetWireFormat != nullptr)
                {
                    get_set_wire_format_config_t wire = {};
                    wire.Size = sizeof(wire);
                    wire.Operation = 0;

                    for (auto& supported :
                         wire.SupportedWireFormat)
                    {
                        supported.Size =
                            sizeof(supported);
                    }

                    wire.WireFormat.Size =
                        sizeof(wire.WireFormat);

                    const result_t wireResult =
                        ctlGetSetWireFormat(
                            displays[displayIndex],
                            &wire);

                    json
                        << ",\"wireFormatResult\":"
                        << wireResult;

                    if (wireResult == RESULT_SUCCESS)
                    {
                        json
                            << ",\"wireFormat\":{"
                            << "\"colorModel\":"
                            << wire.WireFormat.ColorModel
                            << ",\"colorModelName\":\""
                            << IgclWireColorModelLabel(
                                wire.WireFormat.ColorModel)
                            << "\""
                            << ",\"colorDepthFlags\":"
                            << wire.WireFormat.ColorDepth
                            << "}";
                    }
                }

                json << "}";
            }

            json << "]}";
        }

        json << "]}";

        ctlClose(api);
        FreeLibrary(module);

        json << ",";
        AppendNvidiaVendorJson(json);
        json << "}";

        jsonText = json.str();
        return KS_OK;
    }

    int BuildDisplayPipelinesJson(std::string& jsonText)
    {
        constexpr UINT32 flags = QDC_ONLY_ACTIVE_PATHS;

        std::vector<DISPLAYCONFIG_PATH_INFO> paths;
        std::vector<DISPLAYCONFIG_MODE_INFO> modes;

        LONG result = ERROR_SUCCESS;
        UINT32 pathCount = 0;
        UINT32 modeCount = 0;

        do
        {
            pathCount = 0;
            modeCount = 0;

            result = GetDisplayConfigBufferSizes(
                flags,
                &pathCount,
                &modeCount);

            if (result != ERROR_SUCCESS)
            {
                SetError(
                    "GetDisplayConfigBufferSizes failed: " +
                    std::to_string(result));
                return KS_ENUMERATION_FAILED;
            }

            paths.assign(pathCount, {});
            modes.assign(modeCount, {});

            result = QueryDisplayConfig(
                flags,
                &pathCount,
                paths.data(),
                &modeCount,
                modes.data(),
                nullptr);
        }
        while (result == ERROR_INSUFFICIENT_BUFFER);

        if (result != ERROR_SUCCESS)
        {
            SetError(
                "QueryDisplayConfig failed: " +
                std::to_string(result));
            return KS_ENUMERATION_FAILED;
        }

        paths.resize(pathCount);
        modes.resize(modeCount);

        std::ostringstream json;
        json << "[";

        for (size_t pathIndex = 0; pathIndex < paths.size(); ++pathIndex)
        {
            if (pathIndex != 0)
                json << ",";

            const DISPLAYCONFIG_PATH_INFO& path = paths[pathIndex];

            DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {};
            sourceName.header.type =
                DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            sourceName.header.size = sizeof(sourceName);
            sourceName.header.adapterId =
                path.sourceInfo.adapterId;
            sourceName.header.id =
                path.sourceInfo.id;

            const LONG sourceNameResult =
                DisplayConfigGetDeviceInfo(
                    &sourceName.header);

            DISPLAYCONFIG_TARGET_DEVICE_NAME targetName = {};
            targetName.header.type =
                DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
            targetName.header.size = sizeof(targetName);
            targetName.header.adapterId =
                path.targetInfo.adapterId;
            targetName.header.id =
                path.targetInfo.id;

            const LONG targetNameResult =
                DisplayConfigGetDeviceInfo(
                    &targetName.header);

            DISPLAYCONFIG_ADAPTER_NAME adapterName = {};
            adapterName.header.type =
                DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME;
            adapterName.header.size = sizeof(adapterName);
            adapterName.header.adapterId =
                path.sourceInfo.adapterId;

            const LONG adapterNameResult =
                DisplayConfigGetDeviceInfo(
                    &adapterName.header);

            const std::string gdiName =
                sourceNameResult == ERROR_SUCCESS
                ? WideToUtf8(sourceName.viewGdiDeviceName)
                : std::string();

            json
                << "{"
                << "\"pathIndex\":" << pathIndex
                << ",\"active\":"
                << ((path.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0
                    ? "true"
                    : "false")
                << ",\"source\":{"
                << "\"adapterLuidHighPart\":"
                << path.sourceInfo.adapterId.HighPart
                << ",\"adapterLuidLowPart\":"
                << path.sourceInfo.adapterId.LowPart
                << ",\"id\":" << path.sourceInfo.id
                << ",\"statusFlags\":"
                << path.sourceInfo.statusFlags
                << ",\"gdiDeviceName\":";

            if (sourceNameResult == ERROR_SUCCESS)
                json << "\"" << JsonEscape(gdiName) << "\"";
            else
                json << "null";

            json << "}";

            json
                << ",\"target\":{"
                << "\"adapterLuidHighPart\":"
                << path.targetInfo.adapterId.HighPart
                << ",\"adapterLuidLowPart\":"
                << path.targetInfo.adapterId.LowPart
                << ",\"id\":" << path.targetInfo.id
                << ",\"outputTechnology\":"
                << static_cast<int>(path.targetInfo.outputTechnology)
                << ",\"rotation\":"
                << static_cast<unsigned int>(path.targetInfo.rotation)
                << ",\"scaling\":"
                << static_cast<unsigned int>(path.targetInfo.scaling)
                << ",\"refreshRateNumerator\":"
                << path.targetInfo.refreshRate.Numerator
                << ",\"refreshRateDenominator\":"
                << path.targetInfo.refreshRate.Denominator
                << ",\"scanLineOrdering\":"
                << static_cast<unsigned int>(
                    path.targetInfo.scanLineOrdering)
                << ",\"targetAvailable\":"
                << (path.targetInfo.targetAvailable
                    ? "true"
                    : "false")
                << ",\"statusFlags\":"
                << path.targetInfo.statusFlags
                << ",\"friendlyName\":";

            if (targetNameResult == ERROR_SUCCESS &&
                targetName.monitorFriendlyDeviceName[0] != L'\0')
            {
                json
                    << "\""
                    << JsonEscape(
                        WideToUtf8(
                            targetName.monitorFriendlyDeviceName))
                    << "\"";
            }
            else
            {
                json << "null";
            }

            json << ",\"monitorDevicePath\":";

            if (targetNameResult == ERROR_SUCCESS &&
                targetName.monitorDevicePath[0] != L'\0')
            {
                json
                    << "\""
                    << JsonEscape(
                        WideToUtf8(
                            targetName.monitorDevicePath))
                    << "\"";
            }
            else
            {
                json << "null";
            }

            json << "}";

            json << ",\"adapterDevicePath\":";

            if (adapterNameResult == ERROR_SUCCESS)
            {
                json
                    << "\""
                    << JsonEscape(
                        WideToUtf8(
                            adapterName.adapterDevicePath))
                    << "\"";
            }
            else
            {
                json << "null";
            }

            json << ",\"ccdSourceMode\":";

            if (path.sourceInfo.modeInfoIdx !=
                    DISPLAYCONFIG_PATH_MODE_IDX_INVALID &&
                path.sourceInfo.modeInfoIdx < modes.size() &&
                modes[path.sourceInfo.modeInfoIdx].infoType ==
                    DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
            {
                const DISPLAYCONFIG_SOURCE_MODE& sourceMode =
                    modes[path.sourceInfo.modeInfoIdx].sourceMode;

                json
                    << "{"
                    << "\"width\":" << sourceMode.width
                    << ",\"height\":" << sourceMode.height
                    << ",\"pixelFormat\":"
                    << static_cast<unsigned int>(
                        sourceMode.pixelFormat)
                    << ",\"positionX\":"
                    << sourceMode.position.x
                    << ",\"positionY\":"
                    << sourceMode.position.y
                    << "}";
            }
            else
            {
                json << "null";
            }

            json << ",\"ccdTargetSignal\":";

            if (path.targetInfo.modeInfoIdx !=
                    DISPLAYCONFIG_PATH_MODE_IDX_INVALID &&
                path.targetInfo.modeInfoIdx < modes.size() &&
                modes[path.targetInfo.modeInfoIdx].infoType ==
                    DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
            {
                const DISPLAYCONFIG_VIDEO_SIGNAL_INFO& signal =
                    modes[path.targetInfo.modeInfoIdx]
                        .targetMode.targetVideoSignalInfo;

                json
                    << "{"
                    << "\"pixelRate\":" << signal.pixelRate
                    << ",\"hSyncNumerator\":"
                    << signal.hSyncFreq.Numerator
                    << ",\"hSyncDenominator\":"
                    << signal.hSyncFreq.Denominator
                    << ",\"vSyncNumerator\":"
                    << signal.vSyncFreq.Numerator
                    << ",\"vSyncDenominator\":"
                    << signal.vSyncFreq.Denominator
                    << ",\"activeWidth\":"
                    << signal.activeSize.cx
                    << ",\"activeHeight\":"
                    << signal.activeSize.cy
                    << ",\"totalWidth\":"
                    << signal.totalSize.cx
                    << ",\"totalHeight\":"
                    << signal.totalSize.cy
                    << ",\"videoStandard\":"
                    << signal.videoStandard
                    << ",\"scanLineOrdering\":"
                    << static_cast<unsigned int>(
                        signal.scanLineOrdering)
                    << "}";
            }
            else
            {
                json << "null";
            }

            json << ",\"kmt\":";

            if (!gdiName.empty())
            {
                D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME open = {};
                wcsncpy_s(
                    open.DeviceName,
                    ARRAYSIZE(open.DeviceName),
                    sourceName.viewGdiDeviceName,
                    _TRUNCATE);

                const NTSTATUS openStatus =
                    D3DKMTOpenAdapterFromGdiDisplayName(&open);

                if (NtSuccess(openStatus))
                {
                    D3DKMT_ADAPTERREGISTRYINFO registryInfo = {};
                    D3DKMT_DRIVERVERSION driverVersion = {};

                    D3DKMT_CURRENTDISPLAYMODE currentMode = {};
                    currentMode.VidPnSourceId =
                        open.VidPnSourceId;

                    D3DKMT_OUTPUTDUPLCONTEXTSCOUNT duplCount = {};
                    duplCount.VidPnSourceId =
                        open.VidPnSourceId;

                    const bool hasRegistry =
                        QueryAdapter(
                            open.hAdapter,
                            KMTQAITYPE_ADAPTERREGISTRYINFO,
                            registryInfo);

                    const bool hasDriverVersion =
                        QueryAdapter(
                            open.hAdapter,
                            KMTQAITYPE_DRIVERVERSION,
                            driverVersion);

                    const bool hasCurrentMode =
                        QueryAdapter(
                            open.hAdapter,
                            KMTQAITYPE_CURRENTDISPLAYMODE,
                            currentMode);

                    const bool hasDuplCount =
                        QueryAdapter(
                            open.hAdapter,
                            KMTQAITYPE_OUTPUTDUPLCONTEXTSCOUNT,
                            duplCount);

                    json
                        << "{"
                        << "\"adapterLuidHighPart\":"
                        << open.AdapterLuid.HighPart
                        << ",\"adapterLuidLowPart\":"
                        << open.AdapterLuid.LowPart
                        << ",\"vidPnSourceId\":"
                        << open.VidPnSourceId
                        << ",\"matchesCcdSourceAdapter\":"
                        << (SameLuid(
                                open.AdapterLuid,
                                path.sourceInfo.adapterId)
                            ? "true"
                            : "false")
                        << ",\"adapterName\":";

                    if (hasRegistry)
                    {
                        json
                            << "\""
                            << JsonEscape(
                                WideToUtf8(
                                    registryInfo.AdapterString))
                            << "\"";
                    }
                    else
                    {
                        json << "null";
                    }

                    json << ",\"wddm\":";

                    if (hasDriverVersion)
                    {
                        json
                            << "\""
                            << JsonEscape(
                                WddmLabel(driverVersion))
                            << "\"";
                    }
                    else
                    {
                        json << "null";
                    }

                    json << ",\"outputDuplicationClientCount\":";

                    if (hasDuplCount)
                        json << duplCount.OutputDuplicationCount;
                    else
                        json << "null";

                    json << ",\"currentDisplayMode\":";

                    if (hasCurrentMode)
                    {
                        const D3DKMT_DISPLAYMODE& mode =
                            currentMode.DisplayMode;

                        json
                            << "{"
                            << "\"width\":" << mode.Width
                            << ",\"height\":" << mode.Height
                            << ",\"format\":"
                            << static_cast<unsigned int>(
                                mode.Format)
                            << ",\"integerRefreshRate\":"
                            << mode.IntegerRefreshRate
                            << ",\"refreshNumerator\":"
                            << mode.RefreshRate.Numerator
                            << ",\"refreshDenominator\":"
                            << mode.RefreshRate.Denominator
                            << ",\"scanLineOrdering\":"
                            << static_cast<unsigned int>(
                                mode.ScanLineOrdering)
                            << ",\"displayOrientation\":"
                            << static_cast<unsigned int>(
                                mode.DisplayOrientation)
                            << ",\"displayFixedOutput\":"
                            << mode.DisplayFixedOutput
                            << "}";
                    }
                    else
                    {
                        json << "null";
                    }

                    json << "}";

                    D3DKMT_CLOSEADAPTER close = {};
                    close.hAdapter = open.hAdapter;
                    D3DKMTCloseAdapter(&close);
                }
                else
                {
                    json
                        << "{"
                        << "\"openStatus\":\"0x"
                        << std::hex
                        << std::uppercase
                        << static_cast<ULONG>(openStatus)
                        << std::dec
                        << "\""
                        << "}";
                }
            }
            else
            {
                json << "null";
            }

            json << "}";
        }

        json << "]";
        jsonText = json.str();
        return KS_OK;
    }

    struct FrameGuard
    {
        IDXGIOutputDuplication* Duplication = nullptr;
        bool Acquired = false;

        ~FrameGuard()
        {
            if (Acquired && Duplication != nullptr)
                Duplication->ReleaseFrame();
        }
    };


    struct RoGuard
    {
        bool ShouldUninitialize = false;

        ~RoGuard()
        {
            if (ShouldUninitialize)
                RoUninitialize();
        }
    };

    struct HandleGuard
    {
        HANDLE Value = nullptr;

        ~HandleGuard()
        {
            if (Value != nullptr)
                CloseHandle(Value);
        }
    };

    int TextureToBmp(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        ID3D11Texture2D* sourceTexture,
        std::vector<uint8_t>& bmpBytes,
        std::string& errorText)
    {
        if (device == nullptr ||
            context == nullptr ||
            sourceTexture == nullptr)
        {
            errorText = "TextureToBmp received a null D3D object.";
            return KS_CAPTURE_FAILED;
        }

        D3D11_TEXTURE2D_DESC textureDesc = {};
        sourceTexture->GetDesc(&textureDesc);

        if (textureDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
        {
            errorText =
                "Unexpected capture pixel format: " +
                std::to_string(static_cast<unsigned int>(textureDesc.Format));
            return KS_CAPTURE_FAILED;
        }

        D3D11_TEXTURE2D_DESC stagingDesc = textureDesc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> stagingTexture;
        HRESULT hr = device->CreateTexture2D(
            &stagingDesc,
            nullptr,
            stagingTexture.GetAddressOf());

        if (FAILED(hr))
        {
            errorText =
                "CreateTexture2D(staging) failed: " +
                HResultText(hr);
            return KS_CAPTURE_FAILED;
        }

        context->CopyResource(stagingTexture.Get(), sourceTexture);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = context->Map(
            stagingTexture.Get(),
            0,
            D3D11_MAP_READ,
            0,
            &mapped);

        if (FAILED(hr))
        {
            errorText =
                "Map(staging) failed: " +
                HResultText(hr);
            return KS_CAPTURE_FAILED;
        }

        const uint64_t tightStride64 =
            static_cast<uint64_t>(textureDesc.Width) * 4ull;
        const uint64_t imageBytes64 =
            tightStride64 * static_cast<uint64_t>(textureDesc.Height);
        const uint64_t fileBytes64 =
            sizeof(BITMAPFILEHEADER) +
            sizeof(BITMAPINFOHEADER) +
            imageBytes64;

        if (tightStride64 > UINT32_MAX ||
            imageBytes64 > UINT32_MAX ||
            fileBytes64 > UINT32_MAX)
        {
            context->Unmap(stagingTexture.Get(), 0);
            errorText =
                "Captured image is too large for the current BMP API.";
            return KS_CAPTURE_FAILED;
        }

        const uint32_t tightStride =
            static_cast<uint32_t>(tightStride64);
        const uint32_t imageBytes =
            static_cast<uint32_t>(imageBytes64);
        const uint32_t fileBytes =
            static_cast<uint32_t>(fileBytes64);

        bmpBytes.assign(fileBytes, 0);

        BITMAPFILEHEADER fileHeader = {};
        fileHeader.bfType = 0x4D42;
        fileHeader.bfOffBits =
            sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        fileHeader.bfSize = fileBytes;

        BITMAPINFOHEADER infoHeader = {};
        infoHeader.biSize = sizeof(infoHeader);
        infoHeader.biWidth =
            static_cast<LONG>(textureDesc.Width);
        infoHeader.biHeight =
            static_cast<LONG>(textureDesc.Height);
        infoHeader.biPlanes = 1;
        infoHeader.biBitCount = 32;
        infoHeader.biCompression = BI_RGB;
        infoHeader.biSizeImage = imageBytes;

        std::memcpy(
            bmpBytes.data(),
            &fileHeader,
            sizeof(fileHeader));

        std::memcpy(
            bmpBytes.data() + sizeof(fileHeader),
            &infoHeader,
            sizeof(infoHeader));

        uint8_t* destination =
            bmpBytes.data() +
            sizeof(BITMAPFILEHEADER) +
            sizeof(BITMAPINFOHEADER);

        const uint8_t* source =
            static_cast<const uint8_t*>(mapped.pData);

        for (UINT y = 0; y < textureDesc.Height; ++y)
        {
            const UINT sourceY =
                textureDesc.Height - 1 - y;

            std::memcpy(
                destination +
                    static_cast<size_t>(y) * tightStride,
                source +
                    static_cast<size_t>(sourceY) * mapped.RowPitch,
                tightStride);
        }

        context->Unmap(stagingTexture.Get(), 0);
        return KS_OK;
    }

    int CaptureMonitorWithWindowsGraphicsCapture(
        HMONITOR monitor,
        std::vector<uint8_t>& bmpBytes,
        std::string& errorText)
    {
        if (monitor == nullptr)
        {
            errorText =
                "Windows Graphics Capture fallback received a null monitor.";
            return KS_CAPTURE_FAILED;
        }

        const HRESULT roHr =
            RoInitialize(RO_INIT_MULTITHREADED);

        RoGuard roGuard{
            SUCCEEDED(roHr)
        };

        if (FAILED(roHr) &&
            roHr != RPC_E_CHANGED_MODE)
        {
            errorText =
                "RoInitialize failed: " +
                HResultText(roHr);
            return KS_CAPTURE_FAILED;
        }

        try
        {
            using namespace winrt::Windows::Graphics;
            using namespace winrt::Windows::Graphics::Capture;
            using namespace winrt::Windows::Graphics::DirectX;
            using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

            if (!GraphicsCaptureSession::IsSupported())
            {
                errorText =
                    "Windows Graphics Capture is not supported.";
                return KS_CAPTURE_FAILED;
            }

            ComPtr<ID3D11Device> d3dDevice;
            ComPtr<ID3D11DeviceContext> d3dContext;
            D3D_FEATURE_LEVEL featureLevel =
                D3D_FEATURE_LEVEL_9_1;

            HRESULT hr = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                d3dDevice.GetAddressOf(),
                &featureLevel,
                d3dContext.GetAddressOf());

            if (FAILED(hr))
            {
                hr = D3D11CreateDevice(
                    nullptr,
                    D3D_DRIVER_TYPE_WARP,
                    nullptr,
                    D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                    nullptr,
                    0,
                    D3D11_SDK_VERSION,
                    d3dDevice.ReleaseAndGetAddressOf(),
                    &featureLevel,
                    d3dContext.ReleaseAndGetAddressOf());
            }

            if (FAILED(hr))
            {
                errorText =
                    "Windows Graphics Capture D3D11CreateDevice failed: " +
                    HResultText(hr);
                return KS_CAPTURE_FAILED;
            }

            ComPtr<IDXGIDevice> dxgiDevice;
            hr = d3dDevice.As(&dxgiDevice);
            if (FAILED(hr))
            {
                errorText =
                    "Windows Graphics Capture IDXGIDevice query failed: " +
                    HResultText(hr);
                return KS_CAPTURE_FAILED;
            }

            winrt::com_ptr<IInspectable> inspectableDevice;
            hr = CreateDirect3D11DeviceFromDXGIDevice(
                dxgiDevice.Get(),
                inspectableDevice.put());

            if (FAILED(hr))
            {
                errorText =
                    "CreateDirect3D11DeviceFromDXGIDevice failed: " +
                    HResultText(hr);
                return KS_CAPTURE_FAILED;
            }

            auto runtimeDevice =
                inspectableDevice.as<IDirect3DDevice>();

            auto itemInterop =
                winrt::get_activation_factory<
                    GraphicsCaptureItem,
                    IGraphicsCaptureItemInterop>();

            GraphicsCaptureItem item{nullptr};
            hr = itemInterop->CreateForMonitor(
                monitor,
                winrt::guid_of<GraphicsCaptureItem>(),
                winrt::put_abi(item));

            if (FAILED(hr))
            {
                errorText =
                    "GraphicsCaptureItem::CreateForMonitor failed: " +
                    HResultText(hr);
                return KS_CAPTURE_FAILED;
            }

            const SizeInt32 size = item.Size();
            if (size.Width <= 0 || size.Height <= 0)
            {
                errorText =
                    "Windows Graphics Capture returned an invalid monitor size.";
                return KS_CAPTURE_FAILED;
            }

            auto framePool =
                Direct3D11CaptureFramePool::CreateFreeThreaded(
                    runtimeDevice,
                    DirectXPixelFormat::B8G8R8A8UIntNormalized,
                    2,
                    size);

            auto session =
                framePool.CreateCaptureSession(item);

            HandleGuard frameEvent{
                CreateEventW(
                    nullptr,
                    FALSE,
                    FALSE,
                    nullptr)
            };

            if (frameEvent.Value == nullptr)
            {
                errorText =
                    "CreateEvent failed for Windows Graphics Capture.";
                return KS_CAPTURE_FAILED;
            }

            const auto token =
                framePool.FrameArrived(
                    [eventHandle = frameEvent.Value](
                        Direct3D11CaptureFramePool const&,
                        winrt::Windows::Foundation::IInspectable const&)
                    {
                        SetEvent(eventHandle);
                    });

            session.StartCapture();

            const DWORD waitResult =
                WaitForSingleObject(
                    frameEvent.Value,
                    3000);

            framePool.FrameArrived(token);

            if (waitResult != WAIT_OBJECT_0)
            {
                session.Close();
                framePool.Close();

                errorText =
                    waitResult == WAIT_TIMEOUT
                    ? "Windows Graphics Capture timed out waiting for a frame."
                    : "Windows Graphics Capture wait failed.";
                return KS_CAPTURE_FAILED;
            }

            auto frame =
                framePool.TryGetNextFrame();

            if (frame == nullptr)
            {
                session.Close();
                framePool.Close();

                errorText =
                    "Windows Graphics Capture signaled a frame but none was available.";
                return KS_CAPTURE_FAILED;
            }

            auto surfaceAccess =
                frame.Surface().as<
                    ::Windows::Graphics::DirectX::Direct3D11::
                    IDirect3DDxgiInterfaceAccess>();

            ComPtr<ID3D11Texture2D> texture;
            hr = surfaceAccess->GetInterface(
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(
                    texture.GetAddressOf()));

            if (FAILED(hr))
            {
                frame.Close();
                session.Close();
                framePool.Close();

                errorText =
                    "Windows Graphics Capture surface unwrap failed: " +
                    HResultText(hr);
                return KS_CAPTURE_FAILED;
            }

            std::string textureError;
            const int result =
                TextureToBmp(
                    d3dDevice.Get(),
                    d3dContext.Get(),
                    texture.Get(),
                    bmpBytes,
                    textureError);

            frame.Close();
            session.Close();
            framePool.Close();

            if (result != KS_OK)
                errorText = textureError;

            return result;
        }
        catch (const winrt::hresult_error& error)
        {
            errorText =
                "Windows Graphics Capture failed: " +
                HResultText(
                    static_cast<HRESULT>(error.code()));
            return KS_CAPTURE_FAILED;
        }
        catch (...)
        {
            errorText =
                "Windows Graphics Capture failed with an unexpected exception.";
            return KS_CAPTURE_FAILED;
        }
    }


    int CaptureExactDxgiDisplay(
        const LUID& adapterLuid,
        const wchar_t* displayName,
        std::vector<uint8_t>& bmpBytes,
        std::string& errorText,
        std::string& backendName)
    {
        if (displayName == nullptr ||
            *displayName == L'\0')
        {
            errorText =
                "Exact display capture received an empty display name.";
            return KS_INVALID_ARGUMENT;
        }

        ComPtr<IDXGIFactory1> factory;
        HRESULT hr =
            CreateDXGIFactory1(
                IID_PPV_ARGS(
                    factory.GetAddressOf()));

        if (FAILED(hr))
        {
            errorText =
                "CreateDXGIFactory1 failed: " +
                HResultText(hr);
            return KS_CAPTURE_FAILED;
        }

        ComPtr<IDXGIAdapter1> adapter =
            FindDxgiAdapter(
                factory.Get(),
                adapterLuid);

        if (adapter == nullptr)
        {
            errorText =
                "No DXGI adapter matched the active CCD owner LUID.";
            return KS_CAPTURE_FAILED;
        }

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL featureLevel =
            D3D_FEATURE_LEVEL_9_1;

        hr =
            D3D11CreateDevice(
                adapter.Get(),
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                device.GetAddressOf(),
                &featureLevel,
                context.GetAddressOf());

        if (FAILED(hr))
        {
            errorText =
                "D3D11CreateDevice for exact display failed: " +
                HResultText(hr);
            return KS_CAPTURE_FAILED;
        }

        ComPtr<IDXGIOutputDuplication> duplication;
        HRESULT duplicate1Hr = S_OK;
        HRESULT duplicateHr = S_OK;
        bool matchedOutput = false;

        for (UINT outputIndex = 0;; ++outputIndex)
        {
            ComPtr<IDXGIOutput> output;
            hr =
                adapter->EnumOutputs(
                    outputIndex,
                    output.GetAddressOf());

            if (hr == DXGI_ERROR_NOT_FOUND)
                break;

            if (FAILED(hr))
                break;

            DXGI_OUTPUT_DESC desc = {};
            if (FAILED(output->GetDesc(&desc)))
                continue;

            if (_wcsicmp(
                    desc.DeviceName,
                    displayName) != 0)
            {
                continue;
            }

            matchedOutput = true;

            if (!desc.AttachedToDesktop)
            {
                errorText =
                    "The requested DXGI output exists but is not attached to the desktop.";
                return KS_CAPTURE_FAILED;
            }

            ComPtr<IDXGIOutputDuplication> candidate;

            ComPtr<IDXGIOutput5> output5;
            if (SUCCEEDED(output.As(&output5)))
            {
                const DXGI_FORMAT supportedFormats[] =
                {
                    DXGI_FORMAT_B8G8R8A8_UNORM
                };

                duplicate1Hr =
                    output5->DuplicateOutput1(
                        device.Get(),
                        0,
                        static_cast<UINT>(
                            ARRAYSIZE(
                                supportedFormats)),
                        supportedFormats,
                        candidate.GetAddressOf());

                if (SUCCEEDED(duplicate1Hr))
                {
                    backendName = "DuplicateOutput1";
                    duplication = candidate;
                    break;
                }
            }

            ComPtr<IDXGIOutput1> output1;
            if (SUCCEEDED(output.As(&output1)))
            {
                candidate.Reset();

                duplicateHr =
                    output1->DuplicateOutput(
                        device.Get(),
                        candidate.GetAddressOf());

                if (SUCCEEDED(duplicateHr))
                {
                    backendName = "DuplicateOutput";
                    duplication = candidate;
                    break;
                }
            }

            break;
        }

        if (duplication == nullptr)
        {
            std::ostringstream error;

            if (!matchedOutput)
            {
                error
                    << "The CCD owner adapter did not expose "
                    << WideToUtf8(displayName)
                    << " through DXGI.";
            }
            else
            {
                error
                    << "Desktop Duplication could not open "
                    << WideToUtf8(displayName)
                    << ".";

                if (FAILED(duplicate1Hr))
                {
                    error
                        << " DuplicateOutput1="
                        << HResultText(duplicate1Hr)
                        << ".";
                }

                if (FAILED(duplicateHr))
                {
                    error
                        << " DuplicateOutput="
                        << HResultText(duplicateHr)
                        << ".";
                }
            }

            errorText = error.str();
            return KS_CAPTURE_FAILED;
        }

        DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
        ComPtr<IDXGIResource> desktopResource;
        bool acquiredDesktopPresent = false;

        for (int attempt = 0;
             attempt < 10;
             ++attempt)
        {
            frameInfo = {};
            desktopResource.Reset();

            hr =
                duplication->AcquireNextFrame(
                    1000,
                    &frameInfo,
                    desktopResource.GetAddressOf());

            if (hr == DXGI_ERROR_WAIT_TIMEOUT)
                continue;

            if (FAILED(hr))
                break;

            const bool hasDesktopPresent =
                frameInfo.LastPresentTime.QuadPart != 0 ||
                frameInfo.AccumulatedFrames != 0;

            if (hasDesktopPresent)
            {
                acquiredDesktopPresent = true;
                break;
            }

            duplication->ReleaseFrame();
        }

        if (FAILED(hr))
        {
            errorText =
                "AcquireNextFrame for exact display failed: " +
                HResultText(hr);
            return KS_CAPTURE_FAILED;
        }

        if (!acquiredDesktopPresent)
        {
            errorText =
                "AcquireNextFrame returned no desktop-present frame for the requested display.";
            return KS_CAPTURE_FAILED;
        }

        FrameGuard frameGuard{
            duplication.Get(),
            true
        };

        ComPtr<ID3D11Texture2D> desktopTexture;
        hr =
            desktopResource.As(
                &desktopTexture);

        if (FAILED(hr))
        {
            errorText =
                "Exact display desktop resource is not an ID3D11Texture2D: " +
                HResultText(hr);
            return KS_CAPTURE_FAILED;
        }

        return TextureToBmp(
            device.Get(),
            context.Get(),
            desktopTexture.Get(),
            bmpBytes,
            errorText);
    }

    int BuildBmpBytesForDisplay(
        const char* displayName,
        std::vector<uint8_t>& bmpBytes)
    {
        const std::wstring normalizedName =
            NormalizeGdiDisplayName(
                displayName);

        if (normalizedName.empty())
        {
            SetError(
                "Display name is empty or invalid UTF-8.");
            return KS_INVALID_ARGUMENT;
        }

        std::vector<ActiveDisplayPath> activePaths;
        if (!GetActiveDisplayPaths(activePaths))
        {
            SetError(
                "Could not query the active CCD display topology.");
            return KS_ENUMERATION_FAILED;
        }

        const ActiveDisplayPath* selectedPath =
            nullptr;

        for (const ActiveDisplayPath& path :
             activePaths)
        {
            if (_wcsicmp(
                    path.GdiDeviceName.c_str(),
                    normalizedName.c_str()) == 0)
            {
                selectedPath = &path;
                break;
            }
        }

        if (selectedPath == nullptr)
        {
            SetError(
                "Requested display is not an active CCD/VidPN path: " +
                WideToUtf8(
                    normalizedName.c_str()));
            return KS_DEVICE_NOT_FOUND;
        }

        std::string ddaError;
        std::string ddaBackend;
        int status =
            CaptureExactDxgiDisplay(
                selectedPath->AdapterLuid,
                selectedPath->GdiDeviceName.c_str(),
                bmpBytes,
                ddaError,
                ddaBackend);

        if (status == KS_OK)
        {
            RecordCaptureSuccess(
                selectedPath->AdapterLuid,
                selectedPath->GdiDeviceName.c_str(),
                ddaBackend.c_str(),
                "exact-display-dda",
                bmpBytes,
                false,
                0);
            return KS_OK;
        }

        if (selectedPath->Monitor == nullptr)
        {
            SetError(
                "Exact display capture failed. DDA: " +
                ddaError +
                " Windows Graphics Capture: no HMONITOR could be resolved.");
            return KS_CAPTURE_FAILED;
        }

        std::string wgcError;
        status =
            CaptureMonitorWithWindowsGraphicsCapture(
                selectedPath->Monitor,
                bmpBytes,
                wgcError);

        if (status == KS_OK)
        {
            RecordCaptureSuccess(
                selectedPath->AdapterLuid,
                selectedPath->GdiDeviceName.c_str(),
                "WindowsGraphicsCapture",
                "exact-display-wgc",
                bmpBytes,
                false,
                0);
            return KS_OK;
        }

        SetError(
            "Exact display capture failed for " +
            WideToUtf8(
                selectedPath->GdiDeviceName.c_str()) +
            ". DDA: " +
            ddaError +
            " Windows Graphics Capture: " +
            wgcError);

        return KS_CAPTURE_FAILED;
    }

    int BuildBmpBytes(
        uint32_t deviceIndex,
        std::vector<uint8_t>& bmpBytes)
    {
        std::vector<DeviceInfo> devices;
        const int enumerateStatus = EnumerateDevices(devices);
        if (enumerateStatus != KS_OK)
            return enumerateStatus;

        if (deviceIndex >= devices.size())
        {
            SetError("Requested device index does not exist.");
            return KS_DEVICE_NOT_FOUND;
        }

        const DeviceInfo& selectedDevice = devices[deviceIndex];

        ComPtr<IDXGIFactory1> factory;
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(hr))
        {
            SetError("CreateDXGIFactory1 failed: " + HResultText(hr));
            return KS_CAPTURE_FAILED;
        }

        ComPtr<IDXGIAdapter1> adapter = FindDxgiAdapter(factory.Get(), selectedDevice.Luid);
        if (adapter == nullptr)
        {
            SetError("No DXGI adapter matched the selected D3DKMT LUID.");
            return KS_CAPTURE_FAILED;
        }

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_9_1;

        hr = D3D11CreateDevice(
            adapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            device.GetAddressOf(),
            &featureLevel,
            context.GetAddressOf());

        if (FAILED(hr))
        {
            SetError("D3D11CreateDevice failed: " + HResultText(hr));
            return KS_CAPTURE_FAILED;
        }

        ComPtr<IDXGIOutputDuplication> duplication;
        UINT enumeratedOutputCount = 0;
        UINT attachedOutputCount = 0;
        HRESULT lastDuplicateHr = S_OK;
        HRESULT lastDuplicate1Hr = S_OK;
        std::string lastDuplicateOutputName;
        std::string lastFallbackError;
        std::string lastCcdFallbackOutputName;
        UINT activeCcdPathCount = 0;
        std::string captureBackend;
        std::string captureRoute;
        std::wstring captureDisplayName;
        LUID captureAdapterLuid =
            selectedDevice.Luid;

        for (UINT outputIndex = 0;; ++outputIndex)
        {
            ComPtr<IDXGIOutput> output;
            hr = adapter->EnumOutputs(outputIndex, output.GetAddressOf());
            if (hr == DXGI_ERROR_NOT_FOUND)
                break;

            if (FAILED(hr))
                break;

            ++enumeratedOutputCount;

            DXGI_OUTPUT_DESC desc = {};
            if (FAILED(output->GetDesc(&desc)))
                continue;

            if (!desc.AttachedToDesktop)
                continue;

            ++attachedOutputCount;
            lastDuplicateOutputName =
                WideToUtf8(desc.DeviceName);

            ComPtr<IDXGIOutputDuplication> candidate;

            ComPtr<IDXGIOutput5> output5;
            if (SUCCEEDED(output.As(&output5)))
            {
                const DXGI_FORMAT supportedFormats[] =
                {
                    DXGI_FORMAT_B8G8R8A8_UNORM
                };

                lastDuplicate1Hr =
                    output5->DuplicateOutput1(
                        device.Get(),
                        0,
                        static_cast<UINT>(
                            ARRAYSIZE(supportedFormats)),
                        supportedFormats,
                        candidate.GetAddressOf());

                if (SUCCEEDED(lastDuplicate1Hr))
                {
                    captureBackend =
                        "DuplicateOutput1";
                    captureRoute =
                        "dxgi-output";
                    captureDisplayName =
                        desc.DeviceName;
                    captureAdapterLuid =
                        selectedDevice.Luid;
                    duplication = candidate;
                    break;
                }
            }

            ComPtr<IDXGIOutput1> output1;
            if (SUCCEEDED(output.As(&output1)))
            {
                candidate.Reset();

                lastDuplicateHr =
                    output1->DuplicateOutput(
                        device.Get(),
                        candidate.GetAddressOf());

                if (SUCCEEDED(lastDuplicateHr))
                {
                    captureBackend =
                        "DuplicateOutput";
                    captureRoute =
                        "dxgi-output";
                    captureDisplayName =
                        desc.DeviceName;
                    captureAdapterLuid =
                        selectedDevice.Luid;
                    duplication = candidate;
                    break;
                }
            }

            // Resolve the GDI display name through D3DKMT as a second,
            // lower-level ownership check. On hybrid systems the VidPN/GDI
            // owner can differ from the adapter on which DXGI first exposed
            // the output. If it does, retry Desktop Duplication on that exact
            // KMT adapter before leaving DDA.
            LUID kmtLuid = {};
            D3DDDI_VIDEO_PRESENT_SOURCE_ID kmtSourceId = 0;
            if (MapGdiDisplayToKmtAdapter(
                    desc.DeviceName,
                    kmtLuid,
                    kmtSourceId) &&
                !SameLuid(kmtLuid, selectedDevice.Luid))
            {
                ComPtr<IDXGIAdapter1> kmtAdapter =
                    FindDxgiAdapter(factory.Get(), kmtLuid);

                if (kmtAdapter != nullptr)
                {
                    ComPtr<ID3D11Device> kmtDevice;
                    ComPtr<ID3D11DeviceContext> kmtContext;
                    D3D_FEATURE_LEVEL kmtFeatureLevel =
                        D3D_FEATURE_LEVEL_9_1;

                    HRESULT kmtDeviceHr =
                        D3D11CreateDevice(
                            kmtAdapter.Get(),
                            D3D_DRIVER_TYPE_UNKNOWN,
                            nullptr,
                            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                            nullptr,
                            0,
                            D3D11_SDK_VERSION,
                            kmtDevice.GetAddressOf(),
                            &kmtFeatureLevel,
                            kmtContext.GetAddressOf());

                    if (SUCCEEDED(kmtDeviceHr))
                    {
                        for (UINT kmtOutputIndex = 0;; ++kmtOutputIndex)
                        {
                            ComPtr<IDXGIOutput> kmtOutput;
                            HRESULT kmtOutputHr =
                                kmtAdapter->EnumOutputs(
                                    kmtOutputIndex,
                                    kmtOutput.GetAddressOf());

                            if (kmtOutputHr == DXGI_ERROR_NOT_FOUND)
                                break;

                            if (FAILED(kmtOutputHr))
                                break;

                            DXGI_OUTPUT_DESC kmtDesc = {};
                            if (FAILED(kmtOutput->GetDesc(&kmtDesc)))
                                continue;

                            if (_wcsicmp(
                                    kmtDesc.DeviceName,
                                    desc.DeviceName) != 0)
                            {
                                continue;
                            }

                            ComPtr<IDXGIOutputDuplication> kmtCandidate;

                            ComPtr<IDXGIOutput5> kmtOutput5;
                            if (SUCCEEDED(kmtOutput.As(&kmtOutput5)))
                            {
                                const DXGI_FORMAT supportedFormats[] =
                                {
                                    DXGI_FORMAT_B8G8R8A8_UNORM
                                };

                                HRESULT kmtDupHr =
                                    kmtOutput5->DuplicateOutput1(
                                        kmtDevice.Get(),
                                        0,
                                        static_cast<UINT>(
                                            ARRAYSIZE(supportedFormats)),
                                        supportedFormats,
                                        kmtCandidate.GetAddressOf());

                                if (SUCCEEDED(kmtDupHr))
                                {
                                    captureBackend =
                                        "DuplicateOutput1";
                                    captureRoute =
                                        "kmt-owner-retry";
                                    captureDisplayName =
                                        kmtDesc.DeviceName;
                                    captureAdapterLuid =
                                        kmtLuid;
                                    device = kmtDevice;
                                    context = kmtContext;
                                    duplication = kmtCandidate;
                                    break;
                                }
                            }

                            ComPtr<IDXGIOutput1> kmtOutput1;
                            if (SUCCEEDED(kmtOutput.As(&kmtOutput1)))
                            {
                                kmtCandidate.Reset();

                                HRESULT kmtDupHr =
                                    kmtOutput1->DuplicateOutput(
                                        kmtDevice.Get(),
                                        kmtCandidate.GetAddressOf());

                                if (SUCCEEDED(kmtDupHr))
                                {
                                    captureBackend =
                                        "DuplicateOutput";
                                    captureRoute =
                                        "kmt-owner-retry";
                                    captureDisplayName =
                                        kmtDesc.DeviceName;
                                    captureAdapterLuid =
                                        kmtLuid;
                                    device = kmtDevice;
                                    context = kmtContext;
                                    duplication = kmtCandidate;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (duplication != nullptr)
                    break;
            }

            // Desktop Duplication can return DXGI_ERROR_UNSUPPORTED on
            // hybrid systems even though DXGI reports an attached output.
            // Keep DDA as the primary backend, and use Windows Graphics
            // Capture only after the KMT ownership retry also fails.
            std::vector<uint8_t> fallbackBmp;
            std::string fallbackError;
            const int fallbackStatus =
                CaptureMonitorWithWindowsGraphicsCapture(
                    desc.Monitor,
                    fallbackBmp,
                    fallbackError);

            if (fallbackStatus == KS_OK)
            {
                bmpBytes = std::move(fallbackBmp);
                RecordCaptureSuccess(
                    selectedDevice.Luid,
                    desc.DeviceName,
                    "WindowsGraphicsCapture",
                    "dxgi-monitor-fallback",
                    bmpBytes,
                    true,
                    deviceIndex);
                return KS_OK;
            }

            lastFallbackError = fallbackError;
        }


        if (duplication == nullptr)
        {
            // DXGI output enumeration can disagree with the active VidPN/CCD
            // ownership on hybrid laptops. Use the active CCD source LUID as
            // the source of truth and, for this selected adapter, resolve the
            // exact GDI monitor to an HMONITOR. This keeps DDA first; the
            // monitor-level Windows Graphics Capture fallback is only used
            // after DDA cannot expose a duplicable IDXGIOutput.
            std::vector<ActiveDisplayPath> activePaths;
            if (GetActiveDisplayPaths(activePaths))
            {
                for (const ActiveDisplayPath& activePath :
                     activePaths)
                {
                    if (!SameLuid(
                            activePath.AdapterLuid,
                            selectedDevice.Luid))
                    {
                        continue;
                    }

                    ++activeCcdPathCount;

                    if (activePath.Monitor == nullptr)
                        continue;

                    lastCcdFallbackOutputName =
                        WideToUtf8(
                            activePath.GdiDeviceName.c_str());

                    std::vector<uint8_t> fallbackBmp;
                    std::string fallbackError;

                    const int fallbackStatus =
                        CaptureMonitorWithWindowsGraphicsCapture(
                            activePath.Monitor,
                            fallbackBmp,
                            fallbackError);

                    if (fallbackStatus == KS_OK)
                    {
                        bmpBytes =
                            std::move(fallbackBmp);
                        RecordCaptureSuccess(
                            activePath.AdapterLuid,
                            activePath.GdiDeviceName.c_str(),
                            "WindowsGraphicsCapture",
                            "ccd-monitor-fallback",
                            bmpBytes,
                            true,
                            deviceIndex);
                        return KS_OK;
                    }

                    lastFallbackError =
                        fallbackError;
                }
            }
        }

        if (duplication == nullptr)
        {
            std::ostringstream error;
            error
                << "No attached desktop output on the selected adapter could be captured."
                << " deviceIndex=" << deviceIndex
                << ", adapter=\"" << selectedDevice.Name << "\""
                << ", enumeratedOutputs=" << enumeratedOutputCount
                << ", attachedOutputs=" << attachedOutputCount
                << ", activeCcdPaths=" << activeCcdPathCount;

            if (!lastDuplicateOutputName.empty())
            {
                error
                    << ", lastOutput=\"" << lastDuplicateOutputName << "\"";
            }

            if (!lastCcdFallbackOutputName.empty())
            {
                error
                    << ", ccdFallbackOutput=\""
                    << lastCcdFallbackOutputName
                    << "\"";
            }

            if (FAILED(lastDuplicate1Hr))
            {
                error
                    << ", DuplicateOutput1="
                    << HResultText(lastDuplicate1Hr);
            }

            if (FAILED(lastDuplicateHr))
            {
                error
                    << ", DuplicateOutput="
                    << HResultText(lastDuplicateHr);
            }

            if (!lastFallbackError.empty())
            {
                error
                    << ", WindowsGraphicsCapture=\""
                    << lastFallbackError
                    << "\"";
            }

            error
                << ".";

            SetError(error.str());
            return KS_CAPTURE_FAILED;
        }

        DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
        ComPtr<IDXGIResource> desktopResource;
        bool acquiredDesktopPresent = false;

        for (int attempt = 0; attempt < 10; ++attempt)
        {
            frameInfo = {};
            desktopResource.Reset();

            hr = duplication->AcquireNextFrame(
                1000,
                &frameInfo,
                desktopResource.GetAddressOf());

            if (hr == DXGI_ERROR_WAIT_TIMEOUT)
                continue;

            if (FAILED(hr))
                break;

            const bool hasDesktopPresent =
                frameInfo.LastPresentTime.QuadPart != 0 ||
                frameInfo.AccumulatedFrames != 0;

            if (hasDesktopPresent)
            {
                acquiredDesktopPresent = true;
                break;
            }

            duplication->ReleaseFrame();
        }

        if (FAILED(hr))
        {
            SetError("AcquireNextFrame failed: " + HResultText(hr));
            return KS_CAPTURE_FAILED;
        }

        if (!acquiredDesktopPresent)
        {
            SetError(
                "AcquireNextFrame returned no desktop-present frame.");
            return KS_CAPTURE_FAILED;
        }

        FrameGuard frameGuard{duplication.Get(), true};

        ComPtr<ID3D11Texture2D> desktopTexture;
        hr = desktopResource.As(&desktopTexture);
        if (FAILED(hr))
        {
            SetError(
                "Desktop resource is not an ID3D11Texture2D: " +
                HResultText(hr));
            return KS_CAPTURE_FAILED;
        }

        std::string textureError;
        const int textureStatus =
            TextureToBmp(
                device.Get(),
                context.Get(),
                desktopTexture.Get(),
                bmpBytes,
                textureError);

        if (textureStatus != KS_OK)
        {
            SetError(textureError);
            return textureStatus;
        }

        RecordCaptureSuccess(
            captureAdapterLuid,
            captureDisplayName.empty()
                ? nullptr
                : captureDisplayName.c_str(),
            captureBackend.empty()
                ? "DesktopDuplication"
                : captureBackend.c_str(),
            captureRoute.empty()
                ? "dxgi-output"
                : captureRoute.c_str(),
            bmpBytes,
            true,
            deviceIndex);

        return KS_OK;
    }


    int BuildBmpBytesAuto(
        std::vector<uint8_t>& bmpBytes)
    {
        std::vector<DeviceInfo> devices;
        const int enumerateStatus =
            EnumerateDevices(devices);

        if (enumerateStatus != KS_OK)
            return enumerateStatus;

        if (devices.empty())
        {
            SetError(
                "Auto capture found no WDDM adapters.");
            return KS_DEVICE_NOT_FOUND;
        }

        std::vector<uint32_t> candidates;
        size_t activeOwnerCount = 0;

        auto addCandidate =
            [&](uint32_t index)
            {
                if (std::find(
                        candidates.begin(),
                        candidates.end(),
                        index) == candidates.end())
                {
                    candidates.push_back(index);
                }
            };

        std::vector<ActiveDisplayPath> activePaths;
        if (GetActiveDisplayPaths(activePaths))
        {
            for (const ActiveDisplayPath& path :
                 activePaths)
            {
                for (const DeviceInfo& device :
                     devices)
                {
                    if (SameLuid(
                            path.AdapterLuid,
                            device.Luid))
                    {
                        const size_t before =
                            candidates.size();

                        addCandidate(device.Index);

                        if (candidates.size() != before)
                            ++activeOwnerCount;

                        break;
                    }
                }
            }
        }

        // If CCD did not resolve an owner, or if all active owners fail,
        // retain a compatibility fallback across the remaining WDDM
        // adapters. The important ordering guarantee is that active
        // CCD/VidPN owners are always tried first.
        for (const DeviceInfo& device : devices)
            addCandidate(device.Index);

        std::vector<std::string> failures;

        for (size_t candidateIndex = 0;
             candidateIndex < candidates.size();
             ++candidateIndex)
        {
            const uint32_t deviceIndex =
                candidates[candidateIndex];

            bmpBytes.clear();

            const int status =
                BuildBmpBytes(
                    deviceIndex,
                    bmpBytes);

            if (status == KS_OK)
            {
                g_LastCaptureReport.AutoCandidateKind =
                    candidateIndex < activeOwnerCount
                    ? "activeCcdOwner"
                    : "fallbackAdapter";
                return KS_OK;
            }

            std::ostringstream failure;
            failure
                << (candidateIndex < activeOwnerCount
                    ? "active CCD owner"
                    : "fallback adapter")
                << " device "
                << deviceIndex
                << ": "
                << g_LastError;

            failures.push_back(
                failure.str());
        }

        std::ostringstream error;
        error
            << "Auto capture failed after trying "
            << activeOwnerCount
            << " active CCD/VidPN owner adapter(s) first";

        if (candidates.size() > activeOwnerCount)
        {
            error
                << " and "
                << (candidates.size() - activeOwnerCount)
                << " remaining fallback adapter(s)";
        }

        error << ".";

        for (const std::string& failure : failures)
            error << "\n  " << failure;

        SetError(error.str());
        return KS_CAPTURE_FAILED;
    }

    int CopyTextResult(
        const std::string& value,
        char* buffer,
        uint32_t* bufferBytes)
    {
        if (bufferBytes == nullptr)
            return KS_INVALID_ARGUMENT;

        const uint64_t required64 =
            static_cast<uint64_t>(value.size()) + 1ull;

        if (required64 > UINT32_MAX)
        {
            SetError("Text result is too large.");
            return KS_CAPTURE_FAILED;
        }

        const uint32_t required =
            static_cast<uint32_t>(required64);

        if (buffer == nullptr)
        {
            *bufferBytes = required;
            return KS_OK;
        }

        if (*bufferBytes < required)
        {
            *bufferBytes = required;
            return KS_BUFFER_TOO_SMALL;
        }

        std::memcpy(buffer, value.c_str(), required);
        *bufferBytes = required;
        return KS_OK;
    }

    int CopyBinaryResult(
        const std::vector<uint8_t>& value,
        uint8_t* buffer,
        uint32_t* bufferBytes)
    {
        if (bufferBytes == nullptr)
            return KS_INVALID_ARGUMENT;

        if (value.size() > UINT32_MAX)
        {
            SetError("Binary result is too large.");
            return KS_CAPTURE_FAILED;
        }

        const uint32_t required =
            static_cast<uint32_t>(value.size());

        if (buffer == nullptr)
        {
            *bufferBytes = required;
            return KS_OK;
        }

        if (*bufferBytes < required)
        {
            *bufferBytes = required;
            return KS_BUFFER_TOO_SMALL;
        }

        if (required != 0)
            std::memcpy(buffer, value.data(), required);

        *bufferBytes = required;
        return KS_OK;
    }
}

KS_API int KS_CALL KS_ListDevicesJson(
    char* buffer,
    uint32_t* bufferBytes)
{
    g_LastError.clear();

    if (bufferBytes == nullptr)
    {
        SetError("bufferBytes is null.");
        return KS_INVALID_ARGUMENT;
    }

    std::string jsonText;
    const int status = BuildDevicesJson(jsonText);
    if (status != KS_OK)
        return status;

    return CopyTextResult(
        jsonText,
        buffer,
        bufferBytes);
}

KS_API int KS_CALL KS_ListVendorPipelinesJson(
    char* buffer,
    uint32_t* bufferBytes)
{
    g_LastError.clear();

    if (bufferBytes == nullptr)
    {
        SetError("bufferBytes is null.");
        return KS_INVALID_ARGUMENT;
    }

    std::string jsonText;
    const int status = BuildVendorPipelinesJson(jsonText);
    if (status != KS_OK)
        return status;

    return CopyTextResult(
        jsonText,
        buffer,
        bufferBytes);
}

KS_API int KS_CALL KS_ListDisplayPipelinesJson(
    char* buffer,
    uint32_t* bufferBytes)
{
    g_LastError.clear();

    if (bufferBytes == nullptr)
    {
        SetError("bufferBytes is null.");
        return KS_INVALID_ARGUMENT;
    }

    std::string jsonText;
    const int status = BuildDisplayPipelinesJson(jsonText);
    if (status != KS_OK)
        return status;

    return CopyTextResult(
        jsonText,
        buffer,
        bufferBytes);
}

KS_API int KS_CALL KS_CaptureBmp(
    uint32_t deviceIndex,
    uint8_t* buffer,
    uint32_t* bufferBytes)
{
    g_LastError.clear();

    if (bufferBytes == nullptr)
    {
        SetError("bufferBytes is null.");
        return KS_INVALID_ARGUMENT;
    }

    // The public API uses the standard two-call buffer pattern:
    //
    //   1. buffer == nullptr -> return the required byte count.
    //   2. caller allocates that buffer and calls again.
    //
    // Capturing on both calls would require two independent Desktop
    // Duplication frames. On a static desktop the second AcquireNextFrame
    // can wait for another present, making CLI callers appear to hang.
    // Capture exactly once on the size-query call and keep those bytes for
    // the immediately following copy call on the same thread/device.
    if (buffer == nullptr)
    {
        ResetCaptureReport();
        g_PendingBmp.clear();
        g_PendingBmpDisplayName.clear();
        g_HasPendingBmp = false;

        const int status =
            BuildBmpBytes(deviceIndex, g_PendingBmp);

        if (status != KS_OK)
            return status;

        g_LastCaptureReport.RequestMode =
            "device";
        g_LastCaptureReport.HasRequestedDeviceIndex =
            true;
        g_LastCaptureReport.RequestedDeviceIndex =
            deviceIndex;

        if (g_PendingBmp.size() > UINT32_MAX)
        {
            g_PendingBmp.clear();
            SetError("Captured BMP is too large.");
            return KS_CAPTURE_FAILED;
        }

        g_PendingBmpDeviceIndex = deviceIndex;
        g_PendingBmpAuto = false;
        g_HasPendingBmp = true;
        *bufferBytes = static_cast<uint32_t>(g_PendingBmp.size());
        return KS_OK;
    }

    if (g_HasPendingBmp &&
        !g_PendingBmpAuto &&
        g_PendingBmpDeviceIndex == deviceIndex)
    {
        const int status =
            CopyBinaryResult(
                g_PendingBmp,
                buffer,
                bufferBytes);

        if (status == KS_OK)
        {
            g_PendingBmp.clear();
            g_HasPendingBmp = false;
        }

        return status;
    }

    // Also support callers that provide a destination buffer on the first
    // call. In that case capture one frame and copy it immediately.
    ResetCaptureReport();

    std::vector<uint8_t> bmpBytes;
    const int status =
        BuildBmpBytes(deviceIndex, bmpBytes);

    if (status != KS_OK)
        return status;

    g_LastCaptureReport.RequestMode =
        "device";
    g_LastCaptureReport.HasRequestedDeviceIndex =
        true;
    g_LastCaptureReport.RequestedDeviceIndex =
        deviceIndex;

    return CopyBinaryResult(
        bmpBytes,
        buffer,
        bufferBytes);
}


KS_API int KS_CALL KS_CaptureBmpAuto(
    uint8_t* buffer,
    uint32_t* bufferBytes)
{
    g_LastError.clear();

    if (bufferBytes == nullptr)
    {
        SetError("bufferBytes is null.");
        return KS_INVALID_ARGUMENT;
    }

    if (buffer == nullptr)
    {
        ResetCaptureReport();
        g_PendingBmp.clear();
        g_PendingBmpDisplayName.clear();
        g_HasPendingBmp = false;

        const int status =
            BuildBmpBytesAuto(g_PendingBmp);

        if (status != KS_OK)
            return status;

        g_LastCaptureReport.RequestMode =
            "auto";

        if (g_PendingBmp.size() > UINT32_MAX)
        {
            g_PendingBmp.clear();
            SetError("Captured BMP is too large.");
            return KS_CAPTURE_FAILED;
        }

        g_PendingBmpDeviceIndex = UINT32_MAX;
        g_PendingBmpAuto = true;
        g_HasPendingBmp = true;
        *bufferBytes =
            static_cast<uint32_t>(
                g_PendingBmp.size());

        return KS_OK;
    }

    if (g_HasPendingBmp &&
        g_PendingBmpAuto)
    {
        const int status =
            CopyBinaryResult(
                g_PendingBmp,
                buffer,
                bufferBytes);

        if (status == KS_OK)
        {
            g_PendingBmp.clear();
            g_PendingBmpAuto = false;
            g_PendingBmpDisplayName.clear();
            g_HasPendingBmp = false;
        }

        return status;
    }

    ResetCaptureReport();

    std::vector<uint8_t> bmpBytes;
    const int status =
        BuildBmpBytesAuto(bmpBytes);

    if (status != KS_OK)
        return status;

    g_LastCaptureReport.RequestMode =
        "auto";

    return CopyBinaryResult(
        bmpBytes,
        buffer,
        bufferBytes);
}


KS_API int KS_CALL KS_CaptureDisplayBmp(
    const char* displayName,
    uint8_t* buffer,
    uint32_t* bufferBytes)
{
    g_LastError.clear();

    if (displayName == nullptr ||
        *displayName == '\0')
    {
        SetError("displayName is null or empty.");
        return KS_INVALID_ARGUMENT;
    }

    if (bufferBytes == nullptr)
    {
        SetError("bufferBytes is null.");
        return KS_INVALID_ARGUMENT;
    }

    const std::wstring normalizedName =
        NormalizeGdiDisplayName(displayName);

    if (normalizedName.empty())
    {
        SetError(
            "Display name is empty or invalid UTF-8.");
        return KS_INVALID_ARGUMENT;
    }

    const std::string normalizedUtf8 =
        WideToUtf8(
            normalizedName.c_str());

    if (buffer == nullptr)
    {
        ResetCaptureReport();
        g_PendingBmp.clear();
        g_PendingBmpAuto = false;
        g_PendingBmpDisplayName.clear();
        g_HasPendingBmp = false;

        const int status =
            BuildBmpBytesForDisplay(
                normalizedUtf8.c_str(),
                g_PendingBmp);

        if (status != KS_OK)
            return status;

        g_LastCaptureReport.RequestMode =
            "display";
        g_LastCaptureReport.RequestedDisplayName =
            normalizedUtf8;

        if (g_PendingBmp.size() > UINT32_MAX)
        {
            g_PendingBmp.clear();
            SetError("Captured BMP is too large.");
            return KS_CAPTURE_FAILED;
        }

        g_PendingBmpDeviceIndex = UINT32_MAX;
        g_PendingBmpDisplayName =
            normalizedUtf8;
        g_HasPendingBmp = true;

        *bufferBytes =
            static_cast<uint32_t>(
                g_PendingBmp.size());

        return KS_OK;
    }

    if (g_HasPendingBmp &&
        !g_PendingBmpAuto &&
        g_PendingBmpDeviceIndex == UINT32_MAX &&
        _stricmp(
            g_PendingBmpDisplayName.c_str(),
            normalizedUtf8.c_str()) == 0)
    {
        const int status =
            CopyBinaryResult(
                g_PendingBmp,
                buffer,
                bufferBytes);

        if (status == KS_OK)
        {
            g_PendingBmp.clear();
            g_PendingBmpDisplayName.clear();
            g_HasPendingBmp = false;
        }

        return status;
    }

    ResetCaptureReport();

    std::vector<uint8_t> bmpBytes;
    const int status =
        BuildBmpBytesForDisplay(
            normalizedUtf8.c_str(),
            bmpBytes);

    if (status != KS_OK)
        return status;

    g_LastCaptureReport.RequestMode =
        "display";
    g_LastCaptureReport.RequestedDisplayName =
        normalizedUtf8;

    return CopyBinaryResult(
        bmpBytes,
        buffer,
        bufferBytes);
}


KS_API int KS_CALL KS_GetLastCaptureReportJson(
    char* buffer,
    uint32_t* bufferBytes)
{
    if (bufferBytes == nullptr)
    {
        SetError("bufferBytes is null.");
        return KS_INVALID_ARGUMENT;
    }

    if (!g_LastCaptureReport.Available)
    {
        SetError(
            "No successful capture report is available on this thread.");
        return KS_CAPTURE_FAILED;
    }

    return CopyTextResult(
        BuildCaptureReportJson(),
        buffer,
        bufferBytes);
}

KS_API int KS_CALL KS_GetLastErrorMessage(
    char* buffer,
    uint32_t* bufferBytes)
{
    if (bufferBytes == nullptr)
        return KS_INVALID_ARGUMENT;

    return CopyTextResult(
        g_LastError,
        buffer,
        bufferBytes);
}
