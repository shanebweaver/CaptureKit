#pragma once

#include "IScreenshotCapture.h"

// Private definition of the opaque C ABI handle. Combining screenshots borrows
// these buffers; the caller retains ownership until FreeScreenshot.
struct ScreenshotHandle
{
    MonitorScreenshot monitorData;
    CombinedScreenshot combinedData;
    bool isCombined = false;
};
