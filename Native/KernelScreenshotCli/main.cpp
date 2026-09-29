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

    int CaptureBmp(
        uint32_t deviceIndex,
        const std::string& outputPath)
    {
        std::vector<uint8_t> bmp;
        const int status = CaptureBmpBytes(deviceIndex, bmp);

        if (status != KS_OK)
        {
            std::cerr << GetLastErrorText() << "\n";
            return status;
        }

        return WriteBmp(bmp, outputPath);
    }

    int CaptureBmpAuto(const std::string& outputPath)
    {
        std::vector<std::string> failures;

        for (uint32_t deviceIndex = 0; deviceIndex < 64; ++deviceIndex)
        {
            std::vector<uint8_t> bmp;
            const int status = CaptureBmpBytes(deviceIndex, bmp);

            if (status == KS_DEVICE_NOT_FOUND)
                break;

            if (status == KS_OK)
                return WriteBmp(bmp, outputPath);

            const std::string error = GetLastErrorText();

            failures.push_back(
                "device " +
                std::to_string(deviceIndex) +
                ": " +
                error);

            if (status == KS_ENUMERATION_FAILED ||
                status == KS_INVALID_ARGUMENT)
            {
                break;
            }
        }

        std::cerr
            << "Auto capture could not find a capture-capable desktop adapter.";

        for (const std::string& failure : failures)
            std::cerr << "\n  " << failure;

        std::cerr << "\n";
        return KS_CAPTURE_FAILED;
    }

    void PrintUsage()
    {
        std::cerr
            << "Usage:\n"
            << "  KernelScreenshotCli.exe -list\n"
            << "  KernelScreenshotCli.exe -device <index|auto> -screenshot\n"
            << "  KernelScreenshotCli.exe -device <index|auto> -screenshot -out <file.bmp>\n";
    }
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "-list")
        return PrintList();

    if (argc >= 4 &&
        std::string(argv[1]) == "-device" &&
        std::string(argv[3]) == "-screenshot")
    {
        std::string outputPath;

        if (argc == 6 && std::string(argv[4]) == "-out")
            outputPath = argv[5];
        else if (argc != 4)
        {
            PrintUsage();
            return 2;
        }

        const std::string deviceArgument = argv[2];

        if (deviceArgument == "auto")
            return CaptureBmpAuto(outputPath);

        char* end = nullptr;
        const unsigned long parsed =
            std::strtoul(deviceArgument.c_str(), &end, 10);

        if (end == deviceArgument.c_str() ||
            *end != '\0' ||
            parsed > UINT32_MAX)
        {
            std::cerr << "Invalid device index. Use an integer or 'auto'.\n";
            return 2;
        }

        return CaptureBmp(
            static_cast<uint32_t>(parsed),
            outputPath);
    }

    PrintUsage();
    return 2;
}
