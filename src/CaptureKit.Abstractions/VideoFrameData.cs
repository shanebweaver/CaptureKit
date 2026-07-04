using System.Runtime.InteropServices;

namespace CaptureKit.Abstractions;

[StructLayout(LayoutKind.Sequential)]
public struct VideoFrameData
{
    public nint TexturePointer;
    public long Timestamp;
    public uint Width;
    public uint Height;
}
