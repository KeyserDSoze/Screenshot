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

    int CaptureBmp(uint32_t deviceIndex, const std::string& outputPath)
    {
        uint32_t bytes = 0;
        int status = KS_CaptureBmp(deviceIndex, nullptr, &bytes);
        if (status != KS_OK)
        {
            std::cerr << GetLastErrorText() << "\n";
            return status;
        }

        std::vector<uint8_t> bmp(bytes);

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
            {
                std::cerr << GetLastErrorText() << "\n";
                return status;
            }

            bmp.resize(capacity);
            break;
        }

        if (status != KS_OK)
            return status;

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

    void PrintUsage()
    {
        std::cerr
            << "Usage:\n"
            << "  KernelScreenshotCli.exe -list\n"
            << "  KernelScreenshotCli.exe -device <index> -screenshot\n"
            << "  KernelScreenshotCli.exe -device <index> -screenshot -out <file.bmp>\n";
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
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(argv[2], &end, 10);

        if (end == argv[2] || *end != '\0' || parsed > UINT32_MAX)
        {
            std::cerr << "Invalid device index.\n";
            return 2;
        }

        std::string outputPath;

        if (argc == 6 && std::string(argv[4]) == "-out")
            outputPath = argv[5];
        else if (argc != 4)
        {
            PrintUsage();
            return 2;
        }

        return CaptureBmp(
            static_cast<uint32_t>(parsed),
            outputPath);
    }

    PrintUsage();
    return 2;
}
