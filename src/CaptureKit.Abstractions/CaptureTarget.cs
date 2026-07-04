namespace CaptureKit.Abstractions;

public readonly record struct CaptureTarget(
    CaptureTargetKind Kind,
    nint MonitorHandle = 0,
    nint WindowHandle = 0,
    int Left = 0,
    int Top = 0,
    int Width = 0,
    int Height = 0)
{
    public static CaptureTarget Monitor(nint monitorHandle)
        => new(CaptureTargetKind.Monitor, MonitorHandle: monitorHandle);

    public static CaptureTarget Window(nint windowHandle)
        => new(CaptureTargetKind.Window, WindowHandle: windowHandle);

    public static CaptureTarget Rectangle(nint monitorHandle, int left, int top, int width, int height)
        => new(CaptureTargetKind.Rectangle, monitorHandle, Left: left, Top: top, Width: width, Height: height);
}
