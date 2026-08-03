#pragma once

#include "NativeExceptionBoundary.h"
#include "RecorderCallbackContext.h"
#include "ScreenRecorder.h"

#include <utility>

template<typename TAction>
CaptureRecorderResult GuardRecorderCall(
    const wchar_t* boundary,
    CaptureRecorderStatus exceptionStatus,
    TAction&& action) noexcept
{
    if (CaptureKit::Native::RecorderCallbackScope::IsActive())
    {
        return CaptureRecorderResult{
            CaptureRecorderStatus::InvalidState,
            E_ILLEGAL_METHOD_CALL
        };
    }

    try
    {
        return std::forward<TAction>(action)();
    }
    catch (...)
    {
        const HRESULT hr = CaptureKit::Native::HResultFromCurrentException();
        CaptureKit::Native::ReportBoundaryException(boundary, hr);
        return CaptureRecorderResult{ exceptionStatus, hr };
    }
}
