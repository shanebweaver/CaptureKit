using CaptureKit;

namespace CaptureKit.Windows;

public sealed class AudioCaptureService : IAudioCaptureService
{
    public IAudioCaptureSession CreateSession(AudioCaptureOptions options)
        => new AudioCaptureSession(options);
}

internal sealed class AudioCaptureSession : IAudioCaptureSession
{
    private AudioSampleCallback? _audioSampleCallback;
    private bool _disposed;

    public AudioCaptureSession(AudioCaptureOptions options)
    {
        Options = options;
    }

    public event EventHandler<AudioSampleCapturedEventArgs>? SampleCaptured;

    public bool IsRecording { get; private set; }
    public bool IsPaused { get; private set; }
    public AudioCaptureOptions Options { get; }

    public void Start()
    {
        ThrowIfDisposed();
        if (IsRecording)
        {
            throw new InvalidOperationException("Audio capture is already recording.");
        }

        _audioSampleCallback = OnAudioSampleCaptured;

        try
        {
            NativeInterop.RegisterAudioRecordingSampleCallback(_audioSampleCallback).EnsureSuccess();
            var nativeOptions = new NativeAudioCaptureOptions(Options);
            NativeInterop.StartAudioRecording(in nativeOptions).EnsureSuccess();
            IsRecording = true;
        }
        catch
        {
            ClearCallback();
            throw;
        }
    }

    public AudioCaptureResult Stop()
    {
        ThrowIfDisposed();
        if (!IsRecording)
        {
            throw new InvalidOperationException("Audio capture is not recording.");
        }

        try
        {
            NativeInterop.StopAudioRecording().EnsureSuccess();
            return new AudioCaptureResult(Options.OutputPath);
        }
        finally
        {
            IsRecording = false;
            IsPaused = false;
            ClearCallback();
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
            _ = NativeInterop.StopAudioRecording();
        }
        finally
        {
            IsRecording = false;
            IsPaused = false;
            ClearCallback();
        }
    }

    public void Pause()
    {
        ThrowIfDisposed();
        NativeInterop.PauseAudioRecording().EnsureSuccess();
        IsPaused = true;
    }

    public void Resume()
    {
        ThrowIfDisposed();
        NativeInterop.ResumeAudioRecording().EnsureSuccess();
        IsPaused = false;
    }

    public void SetAudioCaptureEnabled(bool enabled)
    {
        ThrowIfDisposed();
        NativeInterop.SetAudioRecordingEnabled(enabled ? 1u : 0u).EnsureSuccess();
    }

    public void SetAudioInputSource(string? sourceId)
    {
        ThrowIfDisposed();
        NativeInterop.SetAudioRecordingInputSource(sourceId).EnsureSuccess();
    }

    public void SetAudioInputVolume(int volumePercentage)
    {
        ThrowIfDisposed();
        NativeInterop.SetAudioRecordingInputVolume((uint)Math.Clamp(volumePercentage, 0, 100)).EnsureSuccess();
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

    private void OnAudioSampleCaptured(ref AudioSampleData sampleData)
    {
        SampleCaptured?.Invoke(this, new AudioSampleCapturedEventArgs(sampleData));
    }

    private void ClearCallback()
    {
        _ = NativeInterop.RegisterAudioRecordingSampleCallback(null);
        _audioSampleCallback = null;
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(AudioCaptureSession));
        }
    }
}
