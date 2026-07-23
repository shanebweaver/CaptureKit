using CaptureKit.Abstractions;
using System.Runtime.InteropServices;

namespace CaptureKit.Windows;

internal static partial class NativeInterop
{
    internal const string RecordingNativeLibraryName = "CaptureKit.Windows.Native.Recording.dll";
    internal const string ScreenshotNativeLibraryName = "CaptureKit.Windows.Native.Screenshot.dll";

    [DllImport(RecordingNativeLibraryName)]
    internal static extern CaptureRecorderResult StartScreenRecording(in NativeVideoCaptureOptions options);

    [DllImport(RecordingNativeLibraryName)]
    internal static extern CaptureRecorderResult PauseScreenRecording();

    [DllImport(RecordingNativeLibraryName)]
    internal static extern CaptureRecorderResult ResumeScreenRecording();

    [DllImport(RecordingNativeLibraryName)]
    internal static extern CaptureRecorderResult StopScreenRecording();

    [DllImport(RecordingNativeLibraryName)]
    internal static extern CaptureRecorderResult SetScreenRecordingAudioEnabled(uint enabled);

    [DllImport(RecordingNativeLibraryName, CharSet = CharSet.Unicode)]
    internal static extern CaptureRecorderResult SetScreenRecordingAudioInputSource(string? sourceId);

    [DllImport(RecordingNativeLibraryName)]
    internal static extern CaptureRecorderResult SetScreenRecordingAudioInputVolume(uint volumePercentage);

    [DllImport(RecordingNativeLibraryName)]
    internal static extern CaptureRecorderResult RegisterVideoFrameCallback(VideoFrameCallback? callback);

    [DllImport(RecordingNativeLibraryName)]
    internal static extern CaptureRecorderResult RegisterAudioSampleCallback(AudioSampleCallback? callback);

    [DllImport(RecordingNativeLibraryName)]
    internal static extern CaptureRecorderResult StartAudioRecording(in NativeAudioCaptureOptions options);

    [DllImport(RecordingNativeLibraryName)]
    internal static extern CaptureRecorderResult PauseAudioRecording();

    [DllImport(RecordingNativeLibraryName)]
    internal static extern CaptureRecorderResult ResumeAudioRecording();

    [DllImport(RecordingNativeLibraryName)]
    internal static extern CaptureRecorderResult StopAudioRecording();

    [DllImport(RecordingNativeLibraryName)]
    internal static extern CaptureRecorderResult SetAudioRecordingEnabled(uint enabled);

    [DllImport(RecordingNativeLibraryName, CharSet = CharSet.Unicode)]
    internal static extern CaptureRecorderResult SetAudioRecordingInputSource(string? sourceId);

    [DllImport(RecordingNativeLibraryName)]
    internal static extern CaptureRecorderResult SetAudioRecordingInputVolume(uint volumePercentage);

    [DllImport(RecordingNativeLibraryName)]
    internal static extern CaptureRecorderResult RegisterAudioRecordingSampleCallback(AudioSampleCallback? callback);

    [DllImport(ScreenshotNativeLibraryName)]
    internal static extern nint CaptureMonitorScreenshot(nint monitorHandle);

    [DllImport(ScreenshotNativeLibraryName)]
    internal static extern nint CaptureAllMonitorsScreenshot();

    [DllImport(ScreenshotNativeLibraryName)]
    internal static extern void GetScreenshotInfo(
        nint handle,
        out int width,
        out int height,
        out int left,
        out int top,
        out uint dpiX,
        out uint dpiY,
        [MarshalAs(UnmanagedType.I1)] out bool isPrimary);

    [DllImport(ScreenshotNativeLibraryName)]
    [return: MarshalAs(UnmanagedType.I1)]
    internal static extern bool CopyScreenshotPixels(
        nint handle,
        byte[] buffer,
        int bufferSize);

    [DllImport(ScreenshotNativeLibraryName, CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.I1)]
    internal static extern bool SaveScreenshotToPng(
        nint handle,
        string filePath);

    [DllImport(ScreenshotNativeLibraryName)]
    internal static extern void FreeScreenshot(nint handle);

    [DllImport(ScreenshotNativeLibraryName)]
    internal static extern nint CombineScreenshots(
        nint[] handles,
        int count);

    [DllImport(RecordingNativeLibraryName)]
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
