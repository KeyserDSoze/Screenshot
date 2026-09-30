#include "../KernelScreenshotApi/KernelScreenshotApi.h"

#include <fcntl.h>
#include <io.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    std::string GetLastErrorText()
    {
        uint32_t bytes = 0;
        if (KS_GetLastErrorMessage(nullptr, &bytes) != KS_OK || bytes == 0)
            return "Unknown KernelScreenshot error.";

        std::vector<char> buffer(bytes);
        if (KS_GetLastErrorMessage(buffer.data(), &bytes) != KS_OK)
            return "Unknown KernelScreenshot error.";

        return std::string(buffer.data());
    }


    std::string GetLastCaptureReportText()
    {
        uint32_t bytes = 0;
        int status =
            KS_GetLastCaptureReportJson(
                nullptr,
                &bytes);

        if (status != KS_OK ||
            bytes == 0)
        {
            return {};
        }

        std::vector<char> buffer(bytes);
        status =
            KS_GetLastCaptureReportJson(
                buffer.data(),
                &bytes);

        if (status != KS_OK)
            return {};

        return std::string(buffer.data());
    }

    int PrintList()
    {
        uint32_t bytes = 0;
        int status = KS_ListDevicesJson(nullptr, &bytes);
        if (status != KS_OK)
        {
            std::cerr << GetLastErrorText() << "\n";
            return status;
        }

        std::vector<char> json(bytes);
        status = KS_ListDevicesJson(json.data(), &bytes);
        if (status != KS_OK)
        {
            std::cerr << GetLastErrorText() << "\n";
            return status;
        }

        std::cout << json.data() << "\n";
        return 0;
    }

    int PrintVendorPipeline()
    {
        uint32_t bytes = 0;
        int status = KS_ListVendorPipelinesJson(nullptr, &bytes);
        if (status != KS_OK)
        {
            std::cerr << GetLastErrorText() << "\n";
            return status;
        }

        std::vector<char> json(bytes);
        status = KS_ListVendorPipelinesJson(json.data(), &bytes);
        if (status != KS_OK)
        {
            std::cerr << GetLastErrorText() << "\n";
            return status;
        }

        std::cout << json.data() << "\n";
        return 0;
    }

    int PrintPipeline()
    {
        uint32_t bytes = 0;
        int status = KS_ListDisplayPipelinesJson(nullptr, &bytes);
        if (status != KS_OK)
        {
            std::cerr << GetLastErrorText() << "\n";
            return status;
        }

        std::vector<char> json(bytes);
        status = KS_ListDisplayPipelinesJson(json.data(), &bytes);
        if (status != KS_OK)
        {
            std::cerr << GetLastErrorText() << "\n";
            return status;
        }

        std::cout << json.data() << "\n";
        return 0;
    }

    int CaptureBmpBytes(
        uint32_t deviceIndex,
        std::vector<uint8_t>& bmp)
    {
        uint32_t bytes = 0;
        int status = KS_CaptureBmp(deviceIndex, nullptr, &bytes);
        if (status != KS_OK)
            return status;

        bmp.assign(bytes, 0);

        for (int attempt = 0; attempt < 2; ++attempt)
        {
            uint32_t capacity = static_cast<uint32_t>(bmp.size());
            status = KS_CaptureBmp(deviceIndex, bmp.data(), &capacity);

            if (status == KS_BUFFER_TOO_SMALL)
            {
                bmp.resize(capacity);
                continue;
            }

            if (status != KS_OK)
                return status;

            bmp.resize(capacity);
            return KS_OK;
        }

        return status;
    }



    int CaptureBmpBytesStrict(
        uint32_t deviceIndex,
        std::vector<uint8_t>& bmp)
    {
        uint32_t bytes = 0;
        int status =
            KS_CaptureBmpStrict(
                deviceIndex,
                nullptr,
                &bytes);

        if (status != KS_OK)
            return status;

        bmp.assign(bytes, 0);

        for (int attempt = 0;
             attempt < 2;
             ++attempt)
        {
            uint32_t capacity =
                static_cast<uint32_t>(
                    bmp.size());

            status =
                KS_CaptureBmpStrict(
                    deviceIndex,
                    bmp.data(),
                    &capacity);

            if (status == KS_BUFFER_TOO_SMALL)
            {
                bmp.resize(capacity);
                continue;
            }

            if (status != KS_OK)
                return status;

            bmp.resize(capacity);
            return KS_OK;
        }

        return status;
    }


    int CaptureBmpBytesAuto(
        std::vector<uint8_t>& bmp)
    {
        uint32_t bytes = 0;
        int status =
            KS_CaptureBmpAuto(
                nullptr,
                &bytes);

        if (status != KS_OK)
            return status;

        bmp.assign(bytes, 0);

        for (int attempt = 0; attempt < 2; ++attempt)
        {
            uint32_t capacity =
                static_cast<uint32_t>(
                    bmp.size());

            status =
                KS_CaptureBmpAuto(
                    bmp.data(),
                    &capacity);

            if (status == KS_BUFFER_TOO_SMALL)
            {
                bmp.resize(capacity);
                continue;
            }

            if (status != KS_OK)
                return status;

            bmp.resize(capacity);
            return KS_OK;
        }

        return status;
    }


    int CaptureBmpBytesDisplay(
        const std::string& displayName,
        std::vector<uint8_t>& bmp)
    {
        uint32_t bytes = 0;
        int status =
            KS_CaptureDisplayBmp(
                displayName.c_str(),
                nullptr,
                &bytes);

        if (status != KS_OK)
            return status;

        bmp.assign(bytes, 0);

        for (int attempt = 0;
             attempt < 2;
             ++attempt)
        {
            uint32_t capacity =
                static_cast<uint32_t>(
                    bmp.size());

            status =
                KS_CaptureDisplayBmp(
                    displayName.c_str(),
                    bmp.data(),
                    &capacity);

            if (status == KS_BUFFER_TOO_SMALL)
            {
                bmp.resize(capacity);
                continue;
            }

            if (status != KS_OK)
                return status;

            bmp.resize(capacity);
            return KS_OK;
        }

        return status;
    }

    int WriteBmp(
        const std::vector<uint8_t>& bmp,
        const std::string& outputPath)
    {
        if (!outputPath.empty())
        {
            std::ofstream file(outputPath, std::ios::binary);
            if (!file)
            {
                std::cerr << "Cannot open output file: " << outputPath << "\n";
                return 20;
            }

            file.write(
                reinterpret_cast<const char*>(bmp.data()),
                static_cast<std::streamsize>(bmp.size()));

            if (!file.good())
            {
                std::cerr << "Could not write output file.\n";
                return 21;
            }

            return 0;
        }

        _setmode(_fileno(stdout), _O_BINARY);
        const size_t written = std::fwrite(
            bmp.data(),
            1,
            bmp.size(),
            stdout);

        if (written != bmp.size())
        {
            std::cerr << "Could not write complete BMP to stdout.\n";
            return 22;
        }

        return 0;
    }


    int WriteCaptureReport(
        const std::string& reportPath)
    {
        if (reportPath.empty())
            return 0;

        const std::string report =
            GetLastCaptureReportText();

        if (report.empty())
        {
            std::cerr
                << "Could not obtain capture report: "
                << GetLastErrorText()
                << "\n";
            return 23;
        }

        std::ofstream file(
            reportPath,
            std::ios::binary);

        if (!file)
        {
            std::cerr
                << "Cannot open capture report file: "
                << reportPath
                << "\n";
            return 24;
        }

        file.write(
            report.data(),
            static_cast<std::streamsize>(
                report.size()));
        file.put('\n');

        if (!file.good())
        {
            std::cerr
                << "Could not write capture report file.\n";
            return 25;
        }

        return 0;
    }

    int WriteCaptureResult(
        const std::vector<uint8_t>& bmp,
        const std::string& outputPath,
        const std::string& reportPath)
    {
        const int bmpStatus =
            WriteBmp(
                bmp,
                outputPath);

        if (bmpStatus != 0)
            return bmpStatus;

        return WriteCaptureReport(
            reportPath);
    }

    int CaptureBmp(
        uint32_t deviceIndex,
        const std::string& outputPath,
        const std::string& reportPath)
    {
        std::vector<uint8_t> bmp;
        const int status = CaptureBmpBytes(deviceIndex, bmp);

        if (status != KS_OK)
        {
            std::cerr << GetLastErrorText() << "\n";
            return status;
        }

        return WriteCaptureResult(
            bmp,
            outputPath,
            reportPath);
    }


    int CaptureBmpStrict(
        uint32_t deviceIndex,
        const std::string& outputPath,
        const std::string& reportPath)
    {
        std::vector<uint8_t> bmp;
        const int status =
            CaptureBmpBytesStrict(
                deviceIndex,
                bmp);

        if (status != KS_OK)
        {
            std::cerr
                << "Strict device "
                << deviceIndex
                << " capture failed: "
                << GetLastErrorText()
                << "\n";
            return status;
        }

        return WriteCaptureResult(
            bmp,
            outputPath,
            reportPath);
    }


    int CaptureBmpPreferred(
        uint32_t preferredDeviceIndex,
        const std::string& outputPath,
        const std::string& reportPath)
    {
        std::vector<std::string> failures;

        auto tryDevice =
            [&](uint32_t deviceIndex, bool& deviceExists) -> int
            {
                std::vector<uint8_t> bmp;
                const int status = CaptureBmpBytes(deviceIndex, bmp);

                if (status == KS_DEVICE_NOT_FOUND)
                {
                    deviceExists = false;
                    return status;
                }

                deviceExists = true;

                if (status == KS_OK)
                    return WriteCaptureResult(
                        bmp,
                        outputPath,
                        reportPath);

                failures.push_back(
                    "device " +
                    std::to_string(deviceIndex) +
                    ": " +
                    GetLastErrorText());

                return status;
            };

        bool preferredExists = true;
        int status =
            tryDevice(preferredDeviceIndex, preferredExists);

        if (status == KS_OK ||
            (status >= 20 &&
             status <= 25))
        {
            return status;
        }

        for (uint32_t deviceIndex = 0; deviceIndex < 64; ++deviceIndex)
        {
            if (deviceIndex == preferredDeviceIndex)
                continue;

            bool deviceExists = true;
            status = tryDevice(deviceIndex, deviceExists);

            if (!deviceExists)
                break;

            if (status == KS_OK ||
                (status >= 20 &&
                 status <= 25))
            {
                return status;
            }

            if (status == KS_ENUMERATION_FAILED ||
                status == KS_INVALID_ARGUMENT)
            {
                break;
            }
        }

        std::cerr
            << "Preferred device " << preferredDeviceIndex
            << " could not capture, and no fallback adapter succeeded.";

        for (const std::string& failure : failures)
            std::cerr << "\n  " << failure;

        std::cerr << "\n";
        return KS_CAPTURE_FAILED;
    }


    int CaptureBmpDisplay(
        const std::string& displayName,
        const std::string& outputPath,
        const std::string& reportPath)
    {
        std::vector<uint8_t> bmp;
        const int status =
            CaptureBmpBytesDisplay(
                displayName,
                bmp);

        if (status != KS_OK)
        {
            std::cerr
                << GetLastErrorText()
                << "\n";
            return status;
        }

        return WriteCaptureResult(
            bmp,
            outputPath,
            reportPath);
    }

    int CaptureBmpAuto(
        const std::string& outputPath,
        const std::string& reportPath)
    {
        std::vector<uint8_t> bmp;
        const int status =
            CaptureBmpBytesAuto(bmp);

        if (status != KS_OK)
        {
            std::cerr
                << GetLastErrorText()
                << "\n";
            return status;
        }

        return WriteCaptureResult(
            bmp,
            outputPath,
            reportPath);
    }

    bool ParseCaptureOptions(
        int argc,
        char** argv,
        int startIndex,
        std::string& outputPath,
        std::string& reportPath,
        bool& strictDevice)
    {
        outputPath.clear();
        reportPath.clear();
        strictDevice = false;

        int index = startIndex;

        while (index < argc)
        {
            const std::string option =
                argv[index];

            if (option == "-strict")
            {
                if (strictDevice)
                    return false;

                strictDevice = true;
                ++index;
                continue;
            }

            if (index + 1 >= argc)
                return false;

            const std::string value =
                argv[index + 1];

            if (option == "-out")
            {
                if (!outputPath.empty())
                    return false;

                outputPath = value;
            }
            else if (option == "-report")
            {
                if (!reportPath.empty())
                    return false;

                reportPath = value;
            }
            else
            {
                return false;
            }

            index += 2;
        }

        return true;
    }

    void PrintUsage()
    {
        std::cerr
            << "Usage:\n"
            << "  KernelScreenshotCli.exe -list\n"
            << "  KernelScreenshotCli.exe -pipeline\n"
            << "  KernelScreenshotCli.exe -vendor-pipeline\n"
            << "  KernelScreenshotCli.exe -screenshot [-out <file.bmp>] [-report <file.json>]\n"
            << "  KernelScreenshotCli.exe -display <DISPLAYn> -screenshot [-out <file.bmp>] [-report <file.json>]\n"
            << "  KernelScreenshotCli.exe -device auto -screenshot [-out <file.bmp>] [-report <file.json>]\n"
            << "  KernelScreenshotCli.exe -device <index> -screenshot [-strict] [-out <file.bmp>] [-report <file.json>]\n";
    }
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "-list")
        return PrintList();

    if (argc == 2 && std::string(argv[1]) == "-pipeline")
        return PrintPipeline();

    if (argc == 2 && std::string(argv[1]) == "-vendor-pipeline")
        return PrintVendorPipeline();

    if (argc >= 2 &&
        std::string(argv[1]) == "-screenshot")
    {
        std::string outputPath;
        std::string reportPath;
        bool strictDevice = false;

        if (!ParseCaptureOptions(
                argc,
                argv,
                2,
                outputPath,
                reportPath,
                strictDevice))
        {
            PrintUsage();
            return 2;
        }

        if (strictDevice)
        {
            std::cerr
                << "-strict is valid only with a numeric -device <index>.\n";
            return 2;
        }

        return CaptureBmpAuto(
            outputPath,
            reportPath);
    }

    if (argc >= 4 &&
        std::string(argv[1]) == "-display" &&
        std::string(argv[3]) == "-screenshot")
    {
        std::string outputPath;
        std::string reportPath;
        bool strictDevice = false;

        if (!ParseCaptureOptions(
                argc,
                argv,
                4,
                outputPath,
                reportPath,
                strictDevice))
        {
            PrintUsage();
            return 2;
        }

        if (strictDevice)
        {
            std::cerr
                << "-strict is valid only with a numeric -device <index>.\n";
            return 2;
        }

        return CaptureBmpDisplay(
            argv[2],
            outputPath,
            reportPath);
    }

    if (argc >= 4 &&
        std::string(argv[1]) == "-device" &&
        std::string(argv[3]) == "-screenshot")
    {
        std::string outputPath;
        std::string reportPath;
        bool strictDevice = false;

        if (!ParseCaptureOptions(
                argc,
                argv,
                4,
                outputPath,
                reportPath,
                strictDevice))
        {
            PrintUsage();
            return 2;
        }

        const std::string deviceArgument =
            argv[2];

        if (deviceArgument == "auto")
        {
            if (strictDevice)
            {
                std::cerr
                    << "-strict cannot be combined with -device auto.\n";
                return 2;
            }

            return CaptureBmpAuto(
                outputPath,
                reportPath);
        }

        char* end = nullptr;
        const unsigned long parsed =
            std::strtoul(
                deviceArgument.c_str(),
                &end,
                10);

        if (end == deviceArgument.c_str() ||
            *end != '\0' ||
            parsed > UINT32_MAX)
        {
            std::cerr
                << "Invalid device index. Use an integer or 'auto'.\n";
            return 2;
        }

        const uint32_t deviceIndex =
            static_cast<uint32_t>(parsed);

        if (strictDevice)
        {
            return CaptureBmpStrict(
                deviceIndex,
                outputPath,
                reportPath);
        }

        return CaptureBmpPreferred(
            deviceIndex,
            outputPath,
            reportPath);
    }

    PrintUsage();
    return 2;
}
