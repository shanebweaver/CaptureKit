#include "ScreenshotExports.h"
#include "ScreenshotHandle.h"
#include "WindowsScreenshotCapture.h"
#include "NativeExceptionBoundary.h"
#include <memory>

// Constants
constexpr uint32_t DEFAULT_DPI = 96;

using CaptureKit::Native::GuardNativeCall;

extern "C"
{
    __declspec(dllexport) ScreenshotHandle* CaptureMonitorScreenshot(HMONITOR hMonitor) noexcept
    {
        return GuardNativeCall(L"CaptureMonitorScreenshot", static_cast<ScreenshotHandle*>(nullptr), [&]() -> ScreenshotHandle* {
            WindowsScreenshotCapture capture;
            auto result = capture.CaptureMonitor(hMonitor);
            if (result.IsError())
            {
                return nullptr;
            }
        
            auto handle = std::make_unique<ScreenshotHandle>();
        
            handle->monitorData = std::move(result.Value());
            handle->isCombined = false;
            return handle.release();
        });
    }
    
    __declspec(dllexport) ScreenshotHandle* CaptureAllMonitorsScreenshot() noexcept
    {
        return GuardNativeCall(L"CaptureAllMonitorsScreenshot", static_cast<ScreenshotHandle*>(nullptr), []() -> ScreenshotHandle* {
            WindowsScreenshotCapture capture;
            auto monitorsResult = capture.CaptureAllMonitors();
            if (monitorsResult.IsError())
            {
                return nullptr;
            }
        
            auto combinedResult = capture.CombineMonitors(monitorsResult.Value());
            if (combinedResult.IsError())
            {
                return nullptr;
            }
        
            auto handle = std::make_unique<ScreenshotHandle>();
        
            handle->combinedData = std::move(combinedResult.Value());
            handle->isCombined = true;
            return handle.release();
        });
    }
    
    __declspec(dllexport) void GetScreenshotInfo(
        ScreenshotHandle* handle,
        int* width,
        int* height,
        int* left,
        int* top,
        uint32_t* dpiX,
        uint32_t* dpiY,
        bool* isPrimary) noexcept
    {
        if (!handle)
        {
            return;
        }
        
        if (handle->isCombined)
        {
            if (width) *width = handle->combinedData.width;
            if (height) *height = handle->combinedData.height;
            if (left) *left = handle->combinedData.left;
            if (top) *top = handle->combinedData.top;
            if (dpiX) *dpiX = DEFAULT_DPI;
            if (dpiY) *dpiY = DEFAULT_DPI;
            if (isPrimary) *isPrimary = false;
        }
        else
        {
            if (width) *width = handle->monitorData.width;
            if (height) *height = handle->monitorData.height;
            if (left) *left = handle->monitorData.left;
            if (top) *top = handle->monitorData.top;
            if (dpiX) *dpiX = handle->monitorData.dpiX;
            if (dpiY) *dpiY = handle->monitorData.dpiY;
            if (isPrimary) *isPrimary = handle->monitorData.isPrimary;
        }
    }
    
    __declspec(dllexport) bool CopyScreenshotPixels(
        ScreenshotHandle* handle,
        uint8_t* buffer,
        int bufferSize) noexcept
    {
        if (!handle || !buffer || bufferSize <= 0)
        {
            return false;
        }
        
        const std::vector<uint8_t>* pixelData = nullptr;
        
        if (handle->isCombined)
        {
            pixelData = &handle->combinedData.pixelData;
        }
        else
        {
            pixelData = &handle->monitorData.pixelData;
        }
        
        if (pixelData->empty() || static_cast<size_t>(bufferSize) < pixelData->size())
        {
            return false;
        }
        
        std::memcpy(buffer, pixelData->data(), pixelData->size());
        return true;
    }
    
    __declspec(dllexport) bool SaveScreenshotToPng(
        ScreenshotHandle* handle,
        const wchar_t* filePath) noexcept
    {
        return GuardNativeCall(L"SaveScreenshotToPng", false, [&] {
            if (!handle || !filePath)
            {
                return false;
            }
        
            const uint8_t* pixelData = nullptr;
            size_t pixelBytes = 0;
            int width = 0;
            int height = 0;
        
            if (handle->isCombined)
            {
                pixelData = handle->combinedData.pixelData.data();
                pixelBytes = handle->combinedData.pixelData.size();
                width = handle->combinedData.width;
                height = handle->combinedData.height;
            }
            else
            {
                pixelData = handle->monitorData.pixelData.data();
                pixelBytes = handle->monitorData.pixelData.size();
                width = handle->monitorData.width;
                height = handle->monitorData.height;
            }
        
            if (width <= 0 || height <= 0 ||
                uint64_t(width) * uint64_t(height) * 4 > pixelBytes)
            {
                return false;
            }

            WindowsScreenshotCapture capture;
            auto result = capture.SaveToPng(pixelData, width, height, filePath);
            return result.IsOk();
        });
    }
    
    __declspec(dllexport) void FreeScreenshot(ScreenshotHandle* handle) noexcept
    {
        delete handle;
    }
    
    __declspec(dllexport) ScreenshotHandle* CombineScreenshots(
        ScreenshotHandle** handles,
        int count) noexcept
    {
        return GuardNativeCall(L"CombineScreenshots", static_cast<ScreenshotHandle*>(nullptr), [&]() -> ScreenshotHandle* {
            if (!handles || count <= 0)
            {
                return nullptr;
            }
        
            std::vector<const MonitorScreenshot*> monitors;
            monitors.reserve(count);
        
            for (int i = 0; i < count; i++)
            {
                if (!handles[i] || handles[i]->isCombined)
                {
                    // Can only combine monitor screenshots, not already-combined ones
                    return nullptr;
                }
                // Borrow pixels. Inputs remain usable on both success and failure.
                monitors.push_back(&handles[i]->monitorData);
            }
        
            WindowsScreenshotCapture capture;
            auto result = capture.CombineMonitorViews(monitors);
            if (result.IsError())
            {
                return nullptr;
            }
        
            auto handle = std::make_unique<ScreenshotHandle>();
        
            handle->combinedData = std::move(result.Value());
            handle->isCombined = true;
            return handle.release();
        });
    }
}
