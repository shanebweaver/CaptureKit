using System.Runtime.InteropServices;

namespace CaptureKit.Abstractions;

/// <summary>
/// Represents a failure returned by CaptureKit's native recording boundary.
/// </summary>
public sealed class CaptureRecorderException : ExternalException
{
    public CaptureRecorderException(CaptureRecorderStatus status, int hResult)
        : base(CreateMessage(status, hResult), hResult)
    {
        Status = status;
    }

    /// <summary>
    /// Gets the stable operation status returned by the native recorder.
    /// </summary>
    public CaptureRecorderStatus Status { get; }

    /// <summary>
    /// Gets the native HRESULT without requiring consumers to parse the message.
    /// </summary>
    public override int ErrorCode => HResult;

    private static string CreateMessage(CaptureRecorderStatus status, int hResult)
    {
        string message = $"Capture operation failed with status {status}";
        if (hResult == 0)
        {
            return message + ".";
        }

        string? nativeMessage = Marshal.GetExceptionForHR(hResult)?.Message;
        return string.IsNullOrWhiteSpace(nativeMessage)
            ? $"{message} (HRESULT 0x{hResult:X8})."
            : $"{message} (HRESULT 0x{hResult:X8}): {nativeMessage}";
    }
}
