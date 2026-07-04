using System.Runtime.InteropServices;

namespace CaptureKit.Abstractions;

[UnmanagedFunctionPointer(CallingConvention.StdCall)]
public delegate void AudioSampleCallback(ref AudioSampleData sampleData);
