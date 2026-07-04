using CaptureKit;
using System.Drawing;
using System.Drawing.Imaging;

namespace CaptureKit.Windows;

public sealed class ImageCaptureService : IImageCaptureService
{
    private readonly IDisplayCaptureService _displayCaptureService;

    public ImageCaptureService(IDisplayCaptureService displayCaptureService)
    {
        _displayCaptureService = displayCaptureService;
    }

    public ImageCaptureResult Capture(ImageCaptureRequest request)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(request.OutputPath);

        return request.Target.Kind switch
        {
            CaptureTargetKind.AllDisplays => CaptureAllDisplays(request.OutputPath),
            CaptureTargetKind.Monitor => CaptureMonitor(request.Target.MonitorHandle, request.OutputPath),
            CaptureTargetKind.Rectangle => CaptureRectangle(request.Target, request.OutputPath),
            CaptureTargetKind.Window => CaptureWindow(request.Target.WindowHandle, request.OutputPath),
            _ => throw new ArgumentOutOfRangeException(nameof(request), request.Target.Kind, "Unsupported image capture target.")
        };
    }

    private ImageCaptureResult CaptureAllDisplays(string outputPath)
    {
        IReadOnlyList<DisplayCapture> displays = _displayCaptureService.CaptureDisplays();
        using Bitmap bitmap = _displayCaptureService.CombineDisplays(displays);
        bitmap.Save(outputPath, ImageFormat.Png);
        return new ImageCaptureResult(outputPath, bitmap.Width, bitmap.Height);
    }

    private ImageCaptureResult CaptureMonitor(nint monitorHandle, string outputPath)
    {
        DisplayCapture display = _displayCaptureService.CaptureDisplays()
            .FirstOrDefault(display => display.MonitorHandle == monitorHandle);

        if (display.MonitorHandle == 0)
        {
            throw new ArgumentException("The requested monitor could not be captured.", nameof(monitorHandle));
        }

        using Bitmap bitmap = _displayCaptureService.CreateBitmap(display);
        bitmap.Save(outputPath, ImageFormat.Png);
        return new ImageCaptureResult(outputPath, bitmap.Width, bitmap.Height);
    }

    private ImageCaptureResult CaptureRectangle(CaptureTarget target, string outputPath)
    {
        DisplayCapture display = _displayCaptureService.CaptureDisplays()
            .FirstOrDefault(display => display.MonitorHandle == target.MonitorHandle);

        if (display.MonitorHandle == 0)
        {
            throw new ArgumentException("The requested monitor could not be captured.", nameof(target));
        }

        using Bitmap bitmap = _displayCaptureService.CreateBitmap(display);
        using Bitmap cropped = _displayCaptureService.CreateCroppedBitmap(
            bitmap,
            new Rectangle(target.Left, target.Top, target.Width, target.Height),
            display.Scale);

        cropped.Save(outputPath, ImageFormat.Png);
        return new ImageCaptureResult(outputPath, cropped.Width, cropped.Height);
    }

    private ImageCaptureResult CaptureWindow(nint windowHandle, string outputPath)
    {
        CaptureWindow window = _displayCaptureService.GetWindows()
            .FirstOrDefault(candidate => candidate.Handle == windowHandle);

        if (window.Handle == 0)
        {
            throw new ArgumentException("The requested window could not be found.", nameof(windowHandle));
        }

        IReadOnlyList<DisplayCapture> displays = _displayCaptureService.CaptureDisplays();
        using Bitmap fullDesktop = _displayCaptureService.CombineDisplays(displays);

        Rectangle desktopBounds = displays[0].Bounds;
        foreach (DisplayCapture display in displays)
        {
            desktopBounds = Rectangle.Union(desktopBounds, display.Bounds);
        }

        Rectangle crop = new(
            window.Bounds.X - desktopBounds.X,
            window.Bounds.Y - desktopBounds.Y,
            window.Bounds.Width,
            window.Bounds.Height);

        crop.Intersect(new Rectangle(0, 0, fullDesktop.Width, fullDesktop.Height));
        if (crop.Width <= 0 || crop.Height <= 0)
        {
            throw new InvalidOperationException("The requested window is outside the captured desktop bounds.");
        }

        using Bitmap cropped = fullDesktop.Clone(crop, fullDesktop.PixelFormat);
        cropped.Save(outputPath, ImageFormat.Png);
        return new ImageCaptureResult(outputPath, cropped.Width, cropped.Height);
    }
}
