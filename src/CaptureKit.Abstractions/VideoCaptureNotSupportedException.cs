namespace CaptureKit.Abstractions;

/// <summary>
/// Thrown when video capture is requested on a device that fails CaptureKit's
/// mandatory support check.
/// </summary>
public sealed class VideoCaptureNotSupportedException : PlatformNotSupportedException
{
    public VideoCaptureNotSupportedException(VideoCaptureSupportResult support)
        : base($"Video capture is not supported on this device ({support.Reason}).")
    {
        Support = support;
        if (support.HResult != 0)
        {
            HResult = support.HResult;
        }
    }

    public VideoCaptureSupportResult Support { get; }
    public VideoCaptureSupportReason Reason => Support.Reason;
}
