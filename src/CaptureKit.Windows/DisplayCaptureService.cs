using CaptureKit;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace CaptureKit.Windows;

public sealed class DisplayCaptureService : IDisplayCaptureService
{
    public IReadOnlyList<DisplayCapture> CaptureDisplays()
    {
        var results = new List<DisplayCapture>();

        _ = EnumDisplayMonitors(
            nint.Zero,
            nint.Zero,
            (monitorHandle, _, _, _) =>
            {
                nint screenshotHandle = NativeInterop.CaptureMonitorScreenshot(monitorHandle);
                if (screenshotHandle == nint.Zero)
                {
                    return true;
                }

                try
                {
                    NativeInterop.GetScreenshotInfo(
                        screenshotHandle,
                        out int width,
                        out int height,
                        out int left,
                        out int top,
                        out uint dpiX,
                        out uint dpiY,
                        out bool isPrimary);

                    long totalBytes = (long)width * height * 4;
                    if (totalBytes <= 0 || totalBytes > int.MaxValue)
                    {
                        return true;
                    }

                    var pixels = new byte[(int)totalBytes];
                    if (!NativeInterop.CopyScreenshotPixels(screenshotHandle, pixels, pixels.Length))
                    {
                        return true;
                    }

                    var monitorInfo = new MonitorInfoEx
                    {
                        Size = Marshal.SizeOf<MonitorInfoEx>(),
                        DeviceName = string.Empty
                    };

                    if (!GetMonitorInfo(monitorHandle, ref monitorInfo))
                    {
                        return true;
                    }

                    int workWidth = monitorInfo.WorkArea.Right - monitorInfo.WorkArea.Left;
                    int workHeight = monitorInfo.WorkArea.Bottom - monitorInfo.WorkArea.Top;

                    results.Add(new DisplayCapture(
                        monitorHandle,
                        pixels,
                        dpiX,
                        dpiY,
                        new Rectangle(left, top, width, height),
                        new Rectangle(monitorInfo.WorkArea.Left, monitorInfo.WorkArea.Top, workWidth, workHeight),
                        isPrimary));
                }
                finally
                {
                    NativeInterop.FreeScreenshot(screenshotHandle);
                }

                return true;
            },
            nint.Zero);

        return results;
    }

    public IReadOnlyList<CaptureWindow> GetWindows()
    {
        var windows = new List<CaptureWindow>();

        _ = EnumWindows((windowHandle, _) =>
        {
            if (!IsWindowVisible(windowHandle))
            {
                return true;
            }

            int length = GetWindowTextLength(windowHandle);
            if (length <= 0)
            {
                return true;
            }

            nint buffer = Marshal.AllocHGlobal((length + 1) * sizeof(char));
            try
            {
                _ = GetWindowText(windowHandle, buffer, length + 1);
                string title = Marshal.PtrToStringUni(buffer) ?? string.Empty;
                if (string.IsNullOrWhiteSpace(title))
                {
                    return true;
                }

                if (!TryGetExtendedFrameBounds(windowHandle, out NativeRect rect)
                    && !GetWindowRect(windowHandle, out rect))
                {
                    return true;
                }

                windows.Add(new CaptureWindow(
                    windowHandle,
                    title,
                    new Rectangle(rect.Left, rect.Top, rect.Right - rect.Left, rect.Bottom - rect.Top)));
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }

            return true;
        }, nint.Zero);

        return windows;
    }

    public Bitmap CombineDisplays(IReadOnlyList<DisplayCapture> displays)
    {
        if (displays.Count == 0)
        {
            throw new ArgumentException("At least one display capture is required.", nameof(displays));
        }

        Rectangle unionBounds = displays[0].Bounds;
        foreach (DisplayCapture display in displays)
        {
            unionBounds = Rectangle.Union(unionBounds, display.Bounds);
        }

        int width = unionBounds.Width;
        int height = unionBounds.Height;
        var buffer = new byte[width * height * 4];

        foreach (DisplayCapture display in displays)
        {
            int offsetX = display.Bounds.X - unionBounds.X;
            int offsetY = display.Bounds.Y - unionBounds.Y;

            for (int y = 0; y < display.Bounds.Height; y++)
            {
                int srcRowStart = y * display.Bounds.Width * 4;
                int dstRowStart = ((offsetY + y) * width + offsetX) * 4;
                Buffer.BlockCopy(display.PixelBuffer, srcRowStart, buffer, dstRowStart, display.Bounds.Width * 4);
            }
        }

        return CreateBitmap(buffer, width, height);
    }

    public Bitmap CreateBitmap(DisplayCapture display)
        => CreateBitmap(display.PixelBuffer, display.Bounds.Width, display.Bounds.Height);

    public Bitmap CreateCroppedBitmap(Bitmap image, Rectangle area, float scale)
    {
        int cropX = Math.Clamp((int)Math.Round(area.Left * scale), 0, image.Width - 1);
        int cropY = Math.Clamp((int)Math.Round(area.Top * scale), 0, image.Height - 1);
        int cropWidth = Math.Clamp((int)Math.Round(area.Width * scale), 1, image.Width - cropX);
        int cropHeight = Math.Clamp((int)Math.Round(area.Height * scale), 1, image.Height - cropY);

        return image.Clone(new Rectangle(cropX, cropY, cropWidth, cropHeight), image.PixelFormat);
    }

    private static Bitmap CreateBitmap(byte[] pixelBuffer, int width, int height)
    {
        var bitmap = new Bitmap(width, height, PixelFormat.Format32bppArgb);
        BitmapData data = bitmap.LockBits(
            new Rectangle(0, 0, width, height),
            ImageLockMode.WriteOnly,
            PixelFormat.Format32bppArgb);

        try
        {
            Marshal.Copy(pixelBuffer, 0, data.Scan0, pixelBuffer.Length);
        }
        finally
        {
            bitmap.UnlockBits(data);
        }

        return bitmap;
    }

    private static bool TryGetExtendedFrameBounds(nint windowHandle, out NativeRect rect)
    {
        int hr = DwmGetWindowAttribute(windowHandle, 9, out rect, Marshal.SizeOf<NativeRect>());
        return hr >= 0;
    }

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumDisplayMonitors(
        nint hdc,
        nint clipRect,
        MonitorEnumProc callback,
        nint data);

    private delegate bool MonitorEnumProc(nint monitorHandle, nint monitorDc, nint monitorRect, nint data);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetMonitorInfo(nint monitorHandle, ref MonitorInfoEx monitorInfo);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumWindows(EnumWindowsProc callback, nint parameter);

    private delegate bool EnumWindowsProc(nint windowHandle, nint parameter);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(nint windowHandle);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowTextLength(nint windowHandle);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(nint windowHandle, nint text, int maxCount);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetWindowRect(nint windowHandle, out NativeRect rect);

    [DllImport("dwmapi.dll")]
    private static extern int DwmGetWindowAttribute(nint windowHandle, int attribute, out NativeRect value, int valueSize);

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeRect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct MonitorInfoEx
    {
        public int Size;
        public NativeRect Monitor;
        public NativeRect WorkArea;
        public uint Flags;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        public string DeviceName;
    }
}
