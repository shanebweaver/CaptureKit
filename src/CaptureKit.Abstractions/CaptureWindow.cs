using System.Drawing;

namespace CaptureKit.Abstractions;

public readonly record struct CaptureWindow(nint Handle, string Title, Rectangle Bounds);
