using System.Runtime.InteropServices;

namespace CaptureKit.Abstractions;

[UnmanagedFunctionPointer(CallingConvention.StdCall)]
public delegate void VideoFrameCallback(ref VideoFrameData frameData);
