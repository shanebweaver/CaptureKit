using CaptureKit.Abstractions;
using System.Collections.Concurrent;
using System.Diagnostics;

namespace CaptureKit.Windows;

public sealed class AudioCaptureService : IAudioCaptureService
{
    public IAudioCaptureSession CreateSession(AudioCaptureOptions options)
        => new AudioCaptureSession(options);
}

internal sealed class AudioCaptureSession : IAudioCaptureSession
{
    private static readonly ConcurrentBag<Delegate> FailedCallbackUnregistrationRoots = [];

    private readonly IAudioCaptureNativeApi _nativeApi;
    private AudioSampleCallback? _audioSampleCallback;
    private bool _disposed;

    public AudioCaptureSession(AudioCaptureOptions options)
        : this(options, new AudioCaptureNativeApi())
    {
    }

    internal AudioCaptureSession(
        AudioCaptureOptions options,
        IAudioCaptureNativeApi nativeApi)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(nativeApi);
        Options = options;
        _nativeApi = nativeApi;
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
            _nativeApi.RegisterAudioRecordingSampleCallback(_audioSampleCallback).EnsureSuccess();
            var nativeOptions = new NativeAudioCaptureOptions(Options);
            _nativeApi.StartAudioRecording(in nativeOptions).EnsureSuccess();
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
            _nativeApi.StopAudioRecording().EnsureSuccess();
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
            _ = _nativeApi.StopAudioRecording();
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
        _nativeApi.PauseAudioRecording().EnsureSuccess();
        IsPaused = true;
    }

    public void Resume()
    {
        ThrowIfDisposed();
        _nativeApi.ResumeAudioRecording().EnsureSuccess();
        IsPaused = false;
    }

    public void SetAudioCaptureEnabled(bool enabled)
    {
        ThrowIfDisposed();
        _nativeApi.SetAudioRecordingEnabled(enabled ? 1u : 0u).EnsureSuccess();
    }

    public void SetAudioInputSource(string? sourceId)
    {
        ThrowIfDisposed();
        _nativeApi.SetAudioRecordingInputSource(sourceId).EnsureSuccess();
    }

    public void SetAudioInputVolume(int volumePercentage)
    {
        ThrowIfDisposed();
        _nativeApi.SetAudioRecordingInputVolume((uint)Math.Clamp(volumePercentage, 0, 100)).EnsureSuccess();
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
        try
        {
            CaptureRecorderResult result = _nativeApi.RegisterAudioRecordingSampleCallback(null);
            if (result.IsSuccess)
            {
                _audioSampleCallback = null;
            }
            else
            {
                RootFailedCallback(
                    new CaptureRecorderException(result.Status, result.HResult));
            }
        }
        catch (Exception exception)
        {
            RootFailedCallback(exception);
        }
    }

    private void RootFailedCallback(Exception exception)
    {
        if (_audioSampleCallback is not null)
        {
            // Native code may still hold this delegate's function pointer. Keep it
            // rooted when unregistering fails rather than risking a stale callback.
            FailedCallbackUnregistrationRoots.Add(_audioSampleCallback);
        }

        try
        {
            Trace.TraceError($"CaptureKit audio callback unregister failed: {exception}");
        }
        catch
        {
            // Diagnostics must not interfere with cleanup.
        }
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(AudioCaptureSession));
        }
    }
}

internal interface IAudioCaptureNativeApi
{
    CaptureRecorderResult StartAudioRecording(in NativeAudioCaptureOptions options);
    CaptureRecorderResult PauseAudioRecording();
    CaptureRecorderResult ResumeAudioRecording();
    CaptureRecorderResult StopAudioRecording();
    CaptureRecorderResult SetAudioRecordingEnabled(uint enabled);
    CaptureRecorderResult SetAudioRecordingInputSource(string? sourceId);
    CaptureRecorderResult SetAudioRecordingInputVolume(uint volumePercentage);
    CaptureRecorderResult RegisterAudioRecordingSampleCallback(AudioSampleCallback? callback);
}

internal sealed class AudioCaptureNativeApi : IAudioCaptureNativeApi
{
    public CaptureRecorderResult StartAudioRecording(in NativeAudioCaptureOptions options)
        => NativeInterop.StartAudioRecording(in options);

    public CaptureRecorderResult PauseAudioRecording() => NativeInterop.PauseAudioRecording();
    public CaptureRecorderResult ResumeAudioRecording() => NativeInterop.ResumeAudioRecording();
    public CaptureRecorderResult StopAudioRecording() => NativeInterop.StopAudioRecording();

    public CaptureRecorderResult SetAudioRecordingEnabled(uint enabled)
        => NativeInterop.SetAudioRecordingEnabled(enabled);

    public CaptureRecorderResult SetAudioRecordingInputSource(string? sourceId)
        => NativeInterop.SetAudioRecordingInputSource(sourceId);

    public CaptureRecorderResult SetAudioRecordingInputVolume(uint volumePercentage)
        => NativeInterop.SetAudioRecordingInputVolume(volumePercentage);

    public CaptureRecorderResult RegisterAudioRecordingSampleCallback(AudioSampleCallback? callback)
        => NativeInterop.RegisterAudioRecordingSampleCallback(callback);
}
