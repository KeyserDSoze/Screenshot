#pragma once

class BASIC_DISPLAY_DRIVER;

NTSTATUS ScreenshotControlInitialize(_In_ PDRIVER_OBJECT DriverObject);
VOID ScreenshotControlShutdown();
VOID ScreenshotControlSetAdapter(_In_opt_ BASIC_DISPLAY_DRIVER* Adapter);
VOID ScreenshotControlClearAdapter(_In_opt_ BASIC_DISPLAY_DRIVER* Adapter);
