using System.Runtime.InteropServices;

namespace CaptureKit.Abstractions;

[StructLayout(LayoutKind.Sequential)]
public struct AudioSampleData
{
    public nint DataPointer;
    public uint NumFrames;
    public long Timestamp;
    public uint SampleRate;
    public ushort Channels;
    public ushort BitsPerSample;
}
