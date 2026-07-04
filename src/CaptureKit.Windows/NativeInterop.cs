using CaptureKit.Abstractions;
using System.Runtime.InteropServices;

namespace CaptureKit.Windows;

internal static partial class NativeInterop
{
    private const string NativeLibraryName = "CaptureKit.Windows.Native.dll";

    [DllImport(NativeLibraryName)]
    internal static extern CaptureRecorderResult StartScreenRecording(in NativeVideoCaptureOptions options);

    [DllImport(NativeLibraryName)]
    internal static extern CaptureRecorderResult PauseScreenRecording();

    [DllImport(NativeLibraryName)]
    internal static extern CaptureRecorderResult ResumeScreenRecording();

    [DllImport(NativeLibraryName)]
    internal static extern CaptureRecorderResult StopScreenRecording();

    [DllImport(NativeLibraryName)]
    internal static extern CaptureRecorderResult SetScreenRecordingAudioEnabled(uint enabled);

    [DllImport(NativeLibraryName, CharSet = CharSet.Unicode)]
    internal static extern CaptureRecorderResult SetScreenRecordingAudioInputSource(string? sourceId);

    [DllImport(NativeLibraryName)]
    internal static extern CaptureRecorderResult SetScreenRecordingAudioInputVolume(uint volumePercentage);

    [DllImport(NativeLibraryName)]
    internal static extern CaptureRecorderResult RegisterVideoFrameCallback(VideoFrameCallback? callback);

    [DllImport(NativeLibraryName)]
    internal static extern CaptureRecorderResult RegisterAudioSampleCallback(AudioSampleCallback? callback);

    [DllImport(NativeLibraryName)]
    internal static extern CaptureRecorderResult StartAudioRecording(in NativeAudioCaptureOptions options);

    [DllImport(NativeLibraryName)]
    internal static extern CaptureRecorderResult PauseAudioRecording();

    [DllImport(NativeLibraryName)]
    internal static extern CaptureRecorderResult ResumeAudioRecording();

    [DllImport(NativeLibraryName)]
    internal static extern CaptureRecorderResult StopAudioRecording();

    [DllImport(NativeLibraryName)]
    internal static extern CaptureRecorderResult SetAudioRecordingEnabled(uint enabled);

    [DllImport(NativeLibraryName, CharSet = CharSet.Unicode)]
    internal static extern CaptureRecorderResult SetAudioRecordingInputSource(string? sourceId);

    [DllImport(NativeLibraryName)]
    internal static extern CaptureRecorderResult SetAudioRecordingInputVolume(uint volumePercentage);

    [DllImport(NativeLibraryName)]
    internal static extern CaptureRecorderResult RegisterAudioRecordingSampleCallback(AudioSampleCallback? callback);

    [DllImport(NativeLibraryName)]
    internal static extern nint CaptureMonitorScreenshot(nint monitorHandle);

    [DllImport(NativeLibraryName)]
    internal static extern nint CaptureAllMonitorsScreenshot();

    [DllImport(NativeLibraryName)]
    internal static extern void GetScreenshotInfo(
        nint handle,
        out int width,
        out int height,
        out int left,
        out int top,
        out uint dpiX,
        out uint dpiY,
        [MarshalAs(UnmanagedType.I1)] out bool isPrimary);

    [DllImport(NativeLibraryName)]
    [return: MarshalAs(UnmanagedType.I1)]
    internal static extern bool CopyScreenshotPixels(
        nint handle,
        byte[] buffer,
        int bufferSize);

    [DllImport(NativeLibraryName, CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.I1)]
    internal static extern bool SaveScreenshotToPng(
        nint handle,
        string filePath);

    [DllImport(NativeLibraryName)]
    internal static extern void FreeScreenshot(nint handle);

    [DllImport(NativeLibraryName)]
    internal static extern nint CombineScreenshots(
        nint[] handles,
        int count);

    [DllImport(NativeLibraryName)]
    [return: MarshalAs(UnmanagedType.I1)]
    internal static extern bool ConvertTextureToPixelBuffer(
        nint texturePointer,
        nint devicePointer,
        nint contextPointer,
        byte[] outBuffer,
        uint bufferSize,
        out uint outRowPitch);
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct NativeVideoCaptureOptions
{
    public NativeVideoCaptureOptions(VideoCaptureOptions options)
    {
        if (options.Target.Kind == CaptureTargetKind.AllDisplays)
        {
            throw new ArgumentException("Video capture does not support all-displays targets.", nameof(options));
        }

        TargetKind = options.Target.Kind;
        MonitorHandle = options.Target.MonitorHandle;
        WindowHandle = options.Target.WindowHandle;
        Left = options.Target.Left;
        Top = options.Target.Top;
        Width = options.Target.Width;
        Height = options.Target.Height;
        OutputPath = options.OutputPath;
        CaptureAudio = options.CaptureAudio ? 1u : 0u;
        FrameRate = options.FrameRate;
        VideoBitrate = options.VideoBitrate;
        AudioBitrate = options.AudioBitrate;
        AudioInputSourceId = options.AudioInputSourceId;
        AudioInputVolumePercentage = (uint)Math.Clamp(options.AudioInputVolumePercentage, 0, 100);
    }

    public readonly CaptureTargetKind TargetKind;
    public readonly nint MonitorHandle;
    public readonly nint WindowHandle;
    public readonly int Left;
    public readonly int Top;
    public readonly int Width;
    public readonly int Height;

    [MarshalAs(UnmanagedType.LPWStr)]
    public readonly string OutputPath;

    public readonly uint CaptureAudio;
    public readonly uint FrameRate;
    public readonly uint VideoBitrate;
    public readonly uint AudioBitrate;

    [MarshalAs(UnmanagedType.LPWStr)]
    public readonly string? AudioInputSourceId;

    public readonly uint AudioInputVolumePercentage;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct NativeAudioCaptureOptions
{
    public NativeAudioCaptureOptions(AudioCaptureOptions options)
    {
        OutputPath = options.OutputPath;
        CaptureAudio = options.CaptureAudio ? 1u : 0u;
        AudioInputSourceId = options.AudioInputSourceId;
        AudioInputVolumePercentage = (uint)Math.Clamp(options.AudioInputVolumePercentage, 0, 100);
    }

    [MarshalAs(UnmanagedType.LPWStr)]
    public readonly string OutputPath;

    public readonly uint CaptureAudio;

    [MarshalAs(UnmanagedType.LPWStr)]
    public readonly string? AudioInputSourceId;

    public readonly uint AudioInputVolumePercentage;
}
