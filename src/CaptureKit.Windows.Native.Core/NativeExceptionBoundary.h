#pragma once

#include <Windows.h>
#include <strsafe.h>
#include <wil/result.h>

#include <new>
#include <system_error>

namespace CaptureKit::Native
{
    /// <summary>
    /// Translate the exception currently being handled into an HRESULT without
    /// allowing an unknown exception type to trigger WIL's fail-fast behavior.
    /// This function must only be called from a catch block.
    /// </summary>
    inline HRESULT HResultFromCurrentException() noexcept
    {
        try
        {
            throw;
        }
        catch (const wil::ResultException& exception)
        {
            return exception.GetErrorCode();
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
        catch (const std::system_error& exception)
        {
            const int errorCode = exception.code().value();
            return errorCode > 0
                ? HRESULT_FROM_WIN32(static_cast<unsigned long>(errorCode))
                : E_FAIL;
        }
        catch (const std::exception&)
        {
            return E_FAIL;
        }
        catch (...)
        {
            return HRESULT_FROM_WIN32(ERROR_UNHANDLED_EXCEPTION);
        }
    }

    inline void ReportBoundaryException(const wchar_t* boundary, HRESULT hr) noexcept
    {
        wchar_t message[256]{};
        if (SUCCEEDED(StringCchPrintfW(
            message,
            ARRAYSIZE(message),
            L"[CaptureKit] Exception contained at %ls. HRESULT=0x%08X\r\n",
            boundary ? boundary : L"unknown boundary",
            static_cast<unsigned int>(hr))))
        {
            OutputDebugStringW(message);
        }
    }
}
