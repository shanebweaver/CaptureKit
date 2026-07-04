using System.Drawing;

namespace CaptureKit.Abstractions;

public readonly record struct DisplayCapture(
    nint MonitorHandle,
    byte[] PixelBuffer,
    uint DpiX,
    uint DpiY,
    Rectangle Bounds,
    Rectangle WorkAreaBounds,
    bool IsPrimary)
{
    public float Scale => DpiX / 96f;
}
