#include "BDD.hxx"
#include "screenshot_control.hxx"

namespace
{
    PDEVICE_OBJECT g_ControlDevice = NULL;
    PDRIVER_OBJECT g_DriverObject = NULL;
    BASIC_DISPLAY_DRIVER* volatile g_Adapter = NULL;

    PDRIVER_DISPATCH g_OriginalCreate = NULL;
    PDRIVER_DISPATCH g_OriginalClose = NULL;
    PDRIVER_DISPATCH g_OriginalDeviceControl = NULL;

    UNICODE_STRING g_SymbolicLink = RTL_CONSTANT_STRING(L"\\DosDevices\\KernelScreenshot");

    NTSTATUS CompleteIrp(_In_ PIRP Irp, _In_ NTSTATUS Status, _In_ ULONG_PTR Information)
    {
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = Information;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    NTSTATUS ForwardOrFail(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp, _In_opt_ PDRIVER_DISPATCH Original)
    {
        if (Original != NULL)
            return Original(DeviceObject, Irp);

        return CompleteIrp(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }

    NTSTATUS ScreenshotCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
    {
        if (DeviceObject != g_ControlDevice)
        {
            PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
            return ForwardOrFail(
                DeviceObject,
                Irp,
                (stack->MajorFunction == IRP_MJ_CREATE) ? g_OriginalCreate : g_OriginalClose);
        }

        return CompleteIrp(Irp, STATUS_SUCCESS, 0);
    }

    NTSTATUS ScreenshotDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
    {
        if (DeviceObject != g_ControlDevice)
            return ForwardOrFail(DeviceObject, Irp, g_OriginalDeviceControl);

        PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
        const ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
        const ULONG outputLength = stack->Parameters.DeviceIoControl.OutputBufferLength;

        BASIC_DISPLAY_DRIVER* adapter =
            reinterpret_cast<BASIC_DISPLAY_DRIVER*>(InterlockedCompareExchangePointer(
                reinterpret_cast<PVOID volatile*>(&g_Adapter),
                NULL,
                NULL));

        if (adapter == NULL)
            return CompleteIrp(Irp, STATUS_DEVICE_NOT_READY, 0);

        if (code == IOCTL_SCREENSHOT_QUERY)
        {
            if ((Irp->AssociatedIrp.SystemBuffer == NULL) ||
                (outputLength < sizeof(SCREENSHOT_FRAME_INFO)))
            {
                return CompleteIrp(Irp, STATUS_BUFFER_TOO_SMALL, sizeof(SCREENSHOT_FRAME_INFO));
            }

            SCREENSHOT_FRAME_INFO* info =
                reinterpret_cast<SCREENSHOT_FRAME_INFO*>(Irp->AssociatedIrp.SystemBuffer);

            NTSTATUS status = adapter->GetScreenshotInfo(info);
            return CompleteIrp(Irp, status, NT_SUCCESS(status) ? sizeof(SCREENSHOT_FRAME_INFO) : 0);
        }

        if (code == IOCTL_SCREENSHOT_CAPTURE)
        {
            if (Irp->MdlAddress == NULL)
                return CompleteIrp(Irp, STATUS_INVALID_PARAMETER, 0);

            PVOID output = MmGetSystemAddressForMdlSafe(
                Irp->MdlAddress,
                NormalPagePriority | MdlMappingNoExecute);

            if (output == NULL)
                return CompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);

            ULONG bytesWritten = 0;
            NTSTATUS status = adapter->CopyScreenshotFrame(output, outputLength, &bytesWritten);
            return CompleteIrp(Irp, status, NT_SUCCESS(status) ? bytesWritten : 0);
        }

        return CompleteIrp(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}

NTSTATUS ScreenshotControlInitialize(_In_ PDRIVER_OBJECT DriverObject)
{
    if (DriverObject == NULL)
        return STATUS_INVALID_PARAMETER;

    UNICODE_STRING deviceName = RTL_CONSTANT_STRING(L"\\Device\\KernelScreenshot");

    g_DriverObject = DriverObject;
    g_OriginalCreate = DriverObject->MajorFunction[IRP_MJ_CREATE];
    g_OriginalClose = DriverObject->MajorFunction[IRP_MJ_CLOSE];
    g_OriginalDeviceControl = DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL];

    NTSTATUS status = IoCreateDevice(
        DriverObject,
        0,
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &g_ControlDevice);

    if (!NT_SUCCESS(status))
    {
        g_ControlDevice = NULL;
        return status;
    }

    status = IoCreateSymbolicLink(&g_SymbolicLink, &deviceName);
    if (!NT_SUCCESS(status))
    {
        IoDeleteDevice(g_ControlDevice);
        g_ControlDevice = NULL;
        return status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = ScreenshotCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = ScreenshotCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ScreenshotDeviceControl;
    g_ControlDevice->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

VOID ScreenshotControlShutdown()
{
    ScreenshotControlSetAdapter(NULL);

    if (g_DriverObject != NULL)
    {
        g_DriverObject->MajorFunction[IRP_MJ_CREATE] = g_OriginalCreate;
        g_DriverObject->MajorFunction[IRP_MJ_CLOSE] = g_OriginalClose;
        g_DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = g_OriginalDeviceControl;
    }

    if (g_ControlDevice != NULL)
    {
        IoDeleteSymbolicLink(&g_SymbolicLink);
        IoDeleteDevice(g_ControlDevice);
        g_ControlDevice = NULL;
    }

    g_DriverObject = NULL;
}

VOID ScreenshotControlSetAdapter(_In_opt_ BASIC_DISPLAY_DRIVER* Adapter)
{
    InterlockedExchangePointer(
        reinterpret_cast<PVOID volatile*>(&g_Adapter),
        reinterpret_cast<PVOID>(Adapter));
}

VOID ScreenshotControlClearAdapter(_In_opt_ BASIC_DISPLAY_DRIVER* Adapter)
{
    InterlockedCompareExchangePointer(
        reinterpret_cast<PVOID volatile*>(&g_Adapter),
        NULL,
        reinterpret_cast<PVOID>(Adapter));
}
