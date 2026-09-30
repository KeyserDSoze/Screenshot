#pragma once

#include <stdint.h>

#ifdef KERNELSCREENSHOTAPI_EXPORTS
#define KS_API extern "C" __declspec(dllexport)
#else
#define KS_API extern "C" __declspec(dllimport)
#endif

#define KS_CALL __cdecl

enum KS_STATUS
{
    KS_OK = 0,
    KS_BUFFER_TOO_SMALL = 1,
    KS_INVALID_ARGUMENT = 2,
    KS_DEVICE_NOT_FOUND = 3,
    KS_ENUMERATION_FAILED = 4,
    KS_CAPTURE_FAILED = 5
};

// UTF-8 JSON. bufferBytes includes the terminating NUL.
// Call once with buffer == nullptr to obtain the required size.
KS_API int KS_CALL KS_ListDevicesJson(char* buffer, uint32_t* bufferBytes);

// UTF-8 JSON describing active Windows display paths, CCD signal timing,
// D3DKMT/VidPN ownership and current KMT display mode.
// Call once with buffer == nullptr to obtain the required size.
KS_API int KS_CALL KS_ListDisplayPipelinesJson(char* buffer, uint32_t* bufferBytes);

// UTF-8 JSON from optional vendor driver interfaces (Intel IGCL / NVIDIA NVAPI).
// This is diagnostic only and does not install or replace display drivers.
KS_API int KS_CALL KS_ListVendorPipelinesJson(char* buffer, uint32_t* bufferBytes);

// Returns a complete 32-bit BMP file in memory.
// Call once with buffer == nullptr to obtain the required byte count.
KS_API int KS_CALL KS_CaptureBmp(
    uint32_t deviceIndex,
    uint8_t* buffer,
    uint32_t* bufferBytes);

// Automatically selects an active display owner from the Windows CCD/VidPN
// topology, preferring active owners before any remaining adapter fallback.
// Returns a complete 32-bit BMP file in memory.
// Call once with buffer == nullptr to obtain the required byte count.
KS_API int KS_CALL KS_CaptureBmpAuto(
    uint8_t* buffer,
    uint32_t* bufferBytes);

// UTF-8 diagnostic text for the most recent API failure on the calling thread.
// bufferBytes includes the terminating NUL.
KS_API int KS_CALL KS_GetLastErrorMessage(char* buffer, uint32_t* bufferBytes);
