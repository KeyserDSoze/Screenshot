#include "BDD.hxx"

#pragma code_seg("PAGE")

NTSTATUS BASIC_DISPLAY_DRIVER::GetScreenshotInfo(_Out_ SCREENSHOT_FRAME_INFO* Info) const
{
    PAGED_CODE();

    if (Info == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Info, sizeof(*Info));

    if (!IsDriverActive())
        return STATUS_DEVICE_NOT_READY;

    const CURRENT_BDD_MODE* mode = GetCurrentMode(0);
    if ((mode == NULL) || (mode->FrameBuffer.Ptr == NULL) || !mode->Flags.FrameBufferIsActive)
        return STATUS_DEVICE_NOT_READY;

    const UINT bitsPerPixel = BPPFromPixelFormat(mode->DispInfo.ColorFormat);
    if (bitsPerPixel != 32)
        return STATUS_NOT_SUPPORTED;

    const ULONGLONG outputStride = (ULONGLONG)mode->DispInfo.Width * 4ull;
    const ULONGLONG frameBytes = outputStride * (ULONGLONG)mode->DispInfo.Height;

    if ((outputStride > MAXULONG) || (frameBytes > MAXULONG))
        return STATUS_INTEGER_OVERFLOW;

    if (mode->DispInfo.Pitch < (ULONG)outputStride)
        return STATUS_INVALID_DEVICE_STATE;

    Info->Version = SCREENSHOT_PROTOCOL_VERSION;
    Info->Width = mode->DispInfo.Width;
    Info->Height = mode->DispInfo.Height;
    Info->SourcePitch = mode->DispInfo.Pitch;
    Info->OutputStride = (ULONG)outputStride;
    Info->BitsPerPixel = bitsPerPixel;
    Info->PixelFormat = SCREENSHOT_PIXEL_FORMAT_BGRX8;
    Info->FrameBytes = (ULONG)frameBytes;
    return STATUS_SUCCESS;
}

NTSTATUS BASIC_DISPLAY_DRIVER::CopyScreenshotFrame(
    _Out_writes_bytes_(DestinationLength) VOID* Destination,
    _In_ ULONG DestinationLength,
    _Out_ ULONG* BytesWritten) const
{
    PAGED_CODE();

    if ((Destination == NULL) || (BytesWritten == NULL))
        return STATUS_INVALID_PARAMETER;

    *BytesWritten = 0;

    SCREENSHOT_FRAME_INFO info = {};
    NTSTATUS status = GetScreenshotInfo(&info);
    if (!NT_SUCCESS(status))
        return status;

    if (DestinationLength < info.FrameBytes)
    {
        *BytesWritten = info.FrameBytes;
        return STATUS_BUFFER_TOO_SMALL;
    }

    const CURRENT_BDD_MODE* mode = GetCurrentMode(0);
    if ((mode == NULL) || (mode->FrameBuffer.Ptr == NULL))
        return STATUS_DEVICE_NOT_READY;

    BYTE* destination = reinterpret_cast<BYTE*>(Destination);
    const BYTE* source = reinterpret_cast<const BYTE*>(mode->FrameBuffer.Ptr);

    for (ULONG y = 0; y < info.Height; ++y)
    {
        RtlCopyMemory(
            destination + ((SIZE_T)y * info.OutputStride),
            source + ((SIZE_T)y * info.SourcePitch),
            info.OutputStride);
    }

    *BytesWritten = info.FrameBytes;
    return STATUS_SUCCESS;
}
