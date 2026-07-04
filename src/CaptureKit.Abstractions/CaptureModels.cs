using System.Drawing;
using System.Runtime.InteropServices;

namespace CaptureKit;

public enum CaptureMode
{
    Image,
    Video
}

public enum CaptureTargetKind
{
    Monitor = 0,
    Window = 1,
    Rectangle = 2,
    AllDisplays = 3
}

public readonly record struct CaptureOptions(CaptureMode CaptureMode, CaptureTargetKind TargetKind)
{
    public static CaptureOptions ImageDefault => new(CaptureMode.Image, CaptureTargetKind.Rectangle);
    public static CaptureOptions VideoDefault => new(CaptureMode.Video, CaptureTargetKind.Monitor);
}

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

public readonly record struct CaptureWindow(nint Handle, string Title, Rectangle Bounds);

public sealed record ImageCaptureRequest(CaptureTarget Target, string OutputPath);

public sealed record ImageCaptureResult(string FilePath, int Width, int Height);

public sealed record VideoCaptureOptions(
    CaptureTarget Target,
    string OutputPath,
    bool CaptureAudio = false,
    uint FrameRate = 30,
    uint VideoBitrate = 5_000_000,
    uint AudioBitrate = 128_000,
    string? AudioInputSourceId = null,
    int AudioInputVolumePercentage = 100);

public sealed record VideoCaptureResult(string FilePath);

public sealed record AudioCaptureOptions(
    string OutputPath,
    bool CaptureAudio = true,
    string? AudioInputSourceId = null,
    int AudioInputVolumePercentage = 100);

public sealed record AudioCaptureResult(string FilePath);

public enum CaptureRecorderStatus
{
    Success = 0,
    InvalidArgument = 1,
    InvalidState = 2,
    StartFailed = 3,
    NoActiveSession = 4
}

[StructLayout(LayoutKind.Sequential)]
public readonly struct CaptureRecorderResult
{
    public readonly CaptureRecorderStatus Status;
    public readonly int HResult;

    public CaptureRecorderResult(CaptureRecorderStatus status, int hResult)
    {
        Status = status;
        HResult = hResult;
    }

    public bool IsSuccess => Status == CaptureRecorderStatus.Success;

    public void EnsureSuccess()
    {
        if (IsSuccess)
        {
            return;
        }

        if (HResult != 0)
        {
            Marshal.ThrowExceptionForHR(HResult);
        }

        throw new InvalidOperationException($"Capture operation failed with status {Status}.");
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct VideoFrameData
{
    public nint TexturePointer;
    public long Timestamp;
    public uint Width;
    public uint Height;
}

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

public sealed class VideoFrameCapturedEventArgs : EventArgs
{
    public VideoFrameCapturedEventArgs(VideoFrameData frameData)
    {
        FrameData = frameData;
    }

    public VideoFrameData FrameData { get; }
}

public sealed class AudioSampleCapturedEventArgs : EventArgs
{
    public AudioSampleCapturedEventArgs(AudioSampleData sampleData)
    {
        SampleData = sampleData;
    }

    public AudioSampleData SampleData { get; }
}

[UnmanagedFunctionPointer(CallingConvention.StdCall)]
public delegate void VideoFrameCallback(ref VideoFrameData frameData);

[UnmanagedFunctionPointer(CallingConvention.StdCall)]
public delegate void AudioSampleCallback(ref AudioSampleData sampleData);
