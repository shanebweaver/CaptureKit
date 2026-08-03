namespace CaptureKit.Abstractions;

/// <summary>
/// Determines whether video capture is available on the current Windows device.
/// </summary>
public interface IVideoCaptureSupportService
{
    VideoCaptureSupportResult GetSupport();
}

/// <summary>
/// Stable reasons a device-wide video capture support check can return.
/// </summary>
public enum VideoCaptureSupportReason
{
    Supported = 0,
    UnsupportedOperatingSystem = 1,
    GraphicsCaptureNotSupported = 2,
    SupportCheckFailed = 3,
}

/// <summary>
/// Result of a device-wide video capture support check.
/// </summary>
public readonly record struct VideoCaptureSupportResult(
    bool IsSupported,
    VideoCaptureSupportReason Reason,
    int HResult = 0)
{
    public static VideoCaptureSupportResult Supported()
        => new(true, VideoCaptureSupportReason.Supported);

    public static VideoCaptureSupportResult Unsupported(
        VideoCaptureSupportReason reason,
        int hResult = 0)
        => new(false, reason, hResult);
}
