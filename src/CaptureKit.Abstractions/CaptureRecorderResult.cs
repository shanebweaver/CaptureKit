using System.Runtime.InteropServices;

namespace CaptureKit.Abstractions;

[StructLayout(LayoutKind.Sequential)]
public readonly struct CaptureRecorderResult
{
    public readonly CaptureRecorderStatus Status;
    public readonly int HResult;

    public CaptureRecorderResult(CaptureRecorderStatus status, int hResult)
    {
        Status = status;
        HResult = hResult;
    }

    public bool IsSuccess => Status == CaptureRecorderStatus.Success;

    public void EnsureSuccess()
    {
        if (IsSuccess)
        {
            return;
        }

        if (HResult != 0)
        {
            Marshal.ThrowExceptionForHR(HResult);
        }

        throw new InvalidOperationException($"Capture operation failed with status {Status}.");
    }
}
