using CaptureKit.Abstractions;
using FluentAssertions;
using System.Drawing;

namespace CaptureKit.Windows.Tests;

[TestClass]
public sealed class DisplayCaptureServiceTests
{
    [TestMethod]
    public void ScreenshotPInvoke_AfterPreload_ResolvesValidatedNativeLibrary()
    {
        nint screenshotHandle = nint.Zero;

        Action act = () =>
        {
            NativeScreenshotLibrary.EnsureAvailable();
            screenshotHandle = NativeInterop.CaptureAllMonitorsScreenshot();
        };

        try
        {
            act.Should().NotThrow();
        }
        finally
        {
            if (screenshotHandle != nint.Zero)
            {
                NativeInterop.FreeScreenshot(screenshotHandle);
            }
        }
    }

    [TestMethod]
    public void CombineDisplays_WithAdjacentDisplays_ReturnsCombinedBitmap()
    {
        var service = new DisplayCaptureService();
        var displays = new[]
        {
            CreateDisplay(new Rectangle(0, 0, 1, 1), red: 255, green: 0, blue: 0),
            CreateDisplay(new Rectangle(1, 0, 1, 1), red: 0, green: 0, blue: 255),
        };

        using Bitmap bitmap = service.CombineDisplays(displays);

        bitmap.Width.Should().Be(2);
        bitmap.Height.Should().Be(1);
        bitmap.GetPixel(0, 0).Should().Be(Color.FromArgb(255, 255, 0, 0));
        bitmap.GetPixel(1, 0).Should().Be(Color.FromArgb(255, 0, 0, 255));
    }

    [TestMethod]
    public void CombineDisplays_WithNoDisplays_ThrowsArgumentException()
    {
        var service = new DisplayCaptureService();

        Action act = () => service.CombineDisplays([]);

        act.Should().Throw<ArgumentException>()
            .WithParameterName("displays");
    }

    [TestMethod]
    [DataRow(3)]
    [DataRow(8)]
    public void CreateBitmap_WithMismatchedPixelBuffer_RejectsBeforeNativeCopy(int bufferLength)
    {
        var service = new DisplayCaptureService();
        DisplayCapture display = CreateDisplay(new Rectangle(0, 0, 1, 1), 0, 0, 0)
            with { PixelBuffer = new byte[bufferLength] };

        Action act = () => service.CreateBitmap(display);

        act.Should().Throw<ArgumentException>().WithParameterName("pixelBuffer");
    }

    [TestMethod]
    public void CreateBitmap_WithHugeDimensions_RejectsBeforeAllocation()
    {
        var service = new DisplayCaptureService();
        DisplayCapture display = CreateDisplay(new Rectangle(0, 0, 1, 1), 0, 0, 0)
            with { Bounds = new Rectangle(0, 0, int.MaxValue, int.MaxValue) };

        Action act = () => service.CreateBitmap(display);

        act.Should().Throw<ArgumentException>().WithParameterName("pixelBuffer");
    }

    [TestMethod]
    public void CreateBitmap_WithMultipleRows_PreservesPixelOrder()
    {
        var service = new DisplayCaptureService();
        DisplayCapture display = CreateDisplay(new Rectangle(0, 0, 1, 2), 0, 0, 0)
            with { PixelBuffer = [0, 0, 255, 255, 255, 0, 0, 255] };

        using Bitmap bitmap = service.CreateBitmap(display);

        bitmap.GetPixel(0, 0).Should().Be(Color.FromArgb(255, 255, 0, 0));
        bitmap.GetPixel(0, 1).Should().Be(Color.FromArgb(255, 0, 0, 255));
    }

    private static DisplayCapture CreateDisplay(Rectangle bounds, byte red, byte green, byte blue)
    {
        var pixelBuffer = new byte[bounds.Width * bounds.Height * 4];
        for (int index = 0; index < pixelBuffer.Length; index += 4)
        {
            pixelBuffer[index] = blue;
            pixelBuffer[index + 1] = green;
            pixelBuffer[index + 2] = red;
            pixelBuffer[index + 3] = 255;
        }

        return new DisplayCapture(
            MonitorHandle: bounds.X == 0 ? 1 : 2,
            pixelBuffer,
            DpiX: 96,
            DpiY: 96,
            Bounds: bounds,
            WorkAreaBounds: bounds,
            IsPrimary: bounds.X == 0);
    }
}
