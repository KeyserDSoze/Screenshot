#include "KernelScreenshotApi.h"

#include <windows.h>
#include <winternl.h>
#include <d3dkmthk.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

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
    thread_local bool g_HasPendingBmp = false;

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

    bool SameLuid(const LUID& left, const LUID& right)
    {
        return left.HighPart == right.HighPart &&
               left.LowPart == right.LowPart;
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
                << ",\"rotation\":" << static_cast<unsigned int>(desc.Rotation)
                << "}";
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
        std::string lastDuplicateOutputName;

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

            ComPtr<IDXGIOutput1> output1;
            if (FAILED(output.As(&output1)))
                continue;

            ComPtr<IDXGIOutputDuplication> candidate;
            hr = output1->DuplicateOutput(device.Get(), candidate.GetAddressOf());
            if (FAILED(hr))
            {
                lastDuplicateHr = hr;
                lastDuplicateOutputName = WideToUtf8(desc.DeviceName);
                continue;
            }

            duplication = candidate;
            break;
        }

        if (duplication == nullptr)
        {
            std::ostringstream error;
            error
                << "No attached desktop output on the selected adapter could be duplicated."
                << " deviceIndex=" << deviceIndex
                << ", adapter=\"" << selectedDevice.Name << "\""
                << ", enumeratedOutputs=" << enumeratedOutputCount
                << ", attachedOutputs=" << attachedOutputCount;

            if (FAILED(lastDuplicateHr))
            {
                error
                    << ", lastDuplicateOutput=\"" << lastDuplicateOutputName << "\""
                    << ", DuplicateOutput=" << HResultText(lastDuplicateHr);
            }

            error
                << ". Run -list again after display topology changes.";

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

        D3D11_TEXTURE2D_DESC textureDesc = {};
        desktopTexture->GetDesc(&textureDesc);

        if (textureDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
        {
            SetError("Unexpected desktop duplication pixel format.");
            return KS_CAPTURE_FAILED;
        }

        D3D11_TEXTURE2D_DESC stagingDesc = textureDesc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> stagingTexture;
        hr = device->CreateTexture2D(
            &stagingDesc,
            nullptr,
            stagingTexture.GetAddressOf());

        if (FAILED(hr))
        {
            SetError("CreateTexture2D(staging) failed: " + HResultText(hr));
            return KS_CAPTURE_FAILED;
        }

        context->CopyResource(stagingTexture.Get(), desktopTexture.Get());

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = context->Map(
            stagingTexture.Get(),
            0,
            D3D11_MAP_READ,
            0,
            &mapped);

        if (FAILED(hr))
        {
            SetError("Map(staging) failed: " + HResultText(hr));
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
            SetError("Captured image is too large for the current BMP API.");
            return KS_CAPTURE_FAILED;
        }

        const uint32_t tightStride = static_cast<uint32_t>(tightStride64);
        const uint32_t imageBytes = static_cast<uint32_t>(imageBytes64);
        const uint32_t fileBytes = static_cast<uint32_t>(fileBytes64);

        bmpBytes.assign(fileBytes, 0);

        BITMAPFILEHEADER fileHeader = {};
        fileHeader.bfType = 0x4D42;
        fileHeader.bfOffBits =
            sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        fileHeader.bfSize = fileBytes;

        BITMAPINFOHEADER infoHeader = {};
        infoHeader.biSize = sizeof(infoHeader);
        infoHeader.biWidth = static_cast<LONG>(textureDesc.Width);
        infoHeader.biHeight = static_cast<LONG>(textureDesc.Height);
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
            const UINT sourceY = textureDesc.Height - 1 - y;

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
        g_PendingBmp.clear();
        g_HasPendingBmp = false;

        const int status =
            BuildBmpBytes(deviceIndex, g_PendingBmp);

        if (status != KS_OK)
            return status;

        if (g_PendingBmp.size() > UINT32_MAX)
        {
            g_PendingBmp.clear();
            SetError("Captured BMP is too large.");
            return KS_CAPTURE_FAILED;
        }

        g_PendingBmpDeviceIndex = deviceIndex;
        g_HasPendingBmp = true;
        *bufferBytes = static_cast<uint32_t>(g_PendingBmp.size());
        return KS_OK;
    }

    if (g_HasPendingBmp &&
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
    std::vector<uint8_t> bmpBytes;
    const int status =
        BuildBmpBytes(deviceIndex, bmpBytes);

    if (status != KS_OK)
        return status;

    return CopyBinaryResult(
        bmpBytes,
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
