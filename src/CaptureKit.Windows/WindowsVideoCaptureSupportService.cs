using CaptureKit.Abstractions;
using Windows.Graphics.Capture;

namespace CaptureKit.Windows;

public sealed class WindowsVideoCaptureSupportService : IVideoCaptureSupportService
{
    // IGraphicsCaptureItemInterop::CreateForWindow/CreateForMonitor require
    // Windows 10 version 1903 (build 18362).
    internal const int MinimumSupportedBuild = 18362;

    private readonly Func<Version> _osVersionProvider;
    private readonly Func<bool> _graphicsCaptureSupportProvider;

    public WindowsVideoCaptureSupportService()
        : this(
            () => Environment.OSVersion.Version,
            GraphicsCaptureSession.IsSupported)
    {
    }

    internal WindowsVideoCaptureSupportService(
        Func<Version> osVersionProvider,
        Func<bool> graphicsCaptureSupportProvider)
    {
        ArgumentNullException.ThrowIfNull(osVersionProvider);
        ArgumentNullException.ThrowIfNull(graphicsCaptureSupportProvider);
        _osVersionProvider = osVersionProvider;
        _graphicsCaptureSupportProvider = graphicsCaptureSupportProvider;
    }

    public VideoCaptureSupportResult GetSupport()
    {
        try
        {
            Version version = _osVersionProvider();
            if (version.Major < 10 ||
                (version.Major == 10 && version.Build < MinimumSupportedBuild))
            {
                return VideoCaptureSupportResult.Unsupported(
                    VideoCaptureSupportReason.UnsupportedOperatingSystem);
            }

            return _graphicsCaptureSupportProvider()
                ? VideoCaptureSupportResult.Supported()
                : VideoCaptureSupportResult.Unsupported(
                    VideoCaptureSupportReason.GraphicsCaptureNotSupported);
        }
        catch (Exception exception)
        {
            return VideoCaptureSupportResult.Unsupported(
                VideoCaptureSupportReason.SupportCheckFailed,
                exception.HResult);
        }
    }
}
