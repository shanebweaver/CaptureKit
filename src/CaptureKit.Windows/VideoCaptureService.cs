using CaptureKit.Abstractions;

namespace CaptureKit.Windows;

public sealed class VideoCaptureService : IVideoCaptureService
{
    public IVideoCaptureSession CreateSession(VideoCaptureOptions options)
        => new VideoCaptureSession(options);
}

internal sealed class VideoCaptureSession : IVideoCaptureSession
{
    private VideoFrameCallback? _videoFrameCallback;
    private AudioSampleCallback? _audioSampleCallback;
    private int _hasObservedRecordingStart;
    private bool _disposed;

    public VideoCaptureSession(VideoCaptureOptions options)
    {
        Options = options;
    }

    public event EventHandler? RecordingStarted;
    public event EventHandler<VideoFrameCapturedEventArgs>? FrameCaptured;
    public event EventHandler<AudioSampleCapturedEventArgs>? AudioSampleCaptured;

    public bool IsRecording { get; private set; }
    public bool IsPaused { get; private set; }
    public VideoCaptureOptions Options { get; }

    public void Start()
    {
        ThrowIfDisposed();
        if (IsRecording)
        {
            throw new InvalidOperationException("Video capture is already recording.");
        }

        _hasObservedRecordingStart = 0;
        _videoFrameCallback = OnVideoFrameCaptured;
        _audioSampleCallback = OnAudioSampleCaptured;

        try
        {
            NativeInterop.RegisterVideoFrameCallback(_videoFrameCallback).EnsureSuccess();
            NativeInterop.RegisterAudioSampleCallback(_audioSampleCallback).EnsureSuccess();
            var nativeOptions = new NativeVideoCaptureOptions(Options);
            NativeInterop.StartScreenRecording(in nativeOptions).EnsureSuccess();
            IsRecording = true;
        }
        catch
        {
            ClearCallbacks();
            throw;
        }
    }

    public VideoCaptureResult Stop()
    {
        ThrowIfDisposed();
        if (!IsRecording)
        {
            throw new InvalidOperationException("Video capture is not recording.");
        }

        try
        {
            NativeInterop.StopScreenRecording().EnsureSuccess();
            return new VideoCaptureResult(Options.OutputPath);
        }
        finally
        {
            IsRecording = false;
            IsPaused = false;
            ClearCallbacks();
        }
    }

    public void Cancel()
    {
        ThrowIfDisposed();
        if (!IsRecording)
        {
            return;
        }

        try
        {
            _ = NativeInterop.StopScreenRecording();
        }
        finally
        {
            IsRecording = false;
            IsPaused = false;
            ClearCallbacks();
        }
    }

    public void Pause()
    {
        ThrowIfDisposed();
        NativeInterop.PauseScreenRecording().EnsureSuccess();
        IsPaused = true;
    }

    public void Resume()
    {
        ThrowIfDisposed();
        NativeInterop.ResumeScreenRecording().EnsureSuccess();
        IsPaused = false;
    }

    public void SetAudioCaptureEnabled(bool enabled)
    {
        ThrowIfDisposed();
        NativeInterop.SetScreenRecordingAudioEnabled(enabled ? 1u : 0u).EnsureSuccess();
    }

    public void SetAudioInputSource(string? sourceId)
    {
        ThrowIfDisposed();
        NativeInterop.SetScreenRecordingAudioInputSource(sourceId).EnsureSuccess();
    }

    public void SetAudioInputVolume(int volumePercentage)
    {
        ThrowIfDisposed();
        NativeInterop.SetScreenRecordingAudioInputVolume((uint)Math.Clamp(volumePercentage, 0, 100)).EnsureSuccess();
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        Cancel();
        _disposed = true;
    }

    private void OnVideoFrameCaptured(ref VideoFrameData frameData)
    {
        NotifyRecordingStarted();
        FrameCaptured?.Invoke(this, new VideoFrameCapturedEventArgs(frameData));
    }

    private void OnAudioSampleCaptured(ref AudioSampleData sampleData)
    {
        NotifyRecordingStarted();
        AudioSampleCaptured?.Invoke(this, new AudioSampleCapturedEventArgs(sampleData));
    }

    private void NotifyRecordingStarted()
    {
        if (Interlocked.Exchange(ref _hasObservedRecordingStart, 1) == 0)
        {
            RecordingStarted?.Invoke(this, EventArgs.Empty);
        }
    }

    private void ClearCallbacks()
    {
        _ = NativeInterop.RegisterAudioSampleCallback(null);
        _ = NativeInterop.RegisterVideoFrameCallback(null);
        _audioSampleCallback = null;
        _videoFrameCallback = null;
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(VideoCaptureSession));
        }
    }
}
