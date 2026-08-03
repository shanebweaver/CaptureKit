using CaptureKit.Abstractions;
using System.Collections.Concurrent;
using System.Diagnostics;

namespace CaptureKit.Windows;

public sealed class VideoCaptureService : IVideoCaptureService
{
    private readonly IVideoCaptureSupportService _supportService;
    private readonly IVideoCaptureNativeApi _nativeApi;

    public VideoCaptureService()
        : this(new WindowsVideoCaptureSupportService())
    {
    }

    public VideoCaptureService(IVideoCaptureSupportService supportService)
        : this(supportService, new VideoCaptureNativeApi())
    {
    }

    internal VideoCaptureService(
        IVideoCaptureSupportService supportService,
        IVideoCaptureNativeApi nativeApi)
    {
        ArgumentNullException.ThrowIfNull(supportService);
        ArgumentNullException.ThrowIfNull(nativeApi);
        _supportService = supportService;
        _nativeApi = nativeApi;
    }

    public IVideoCaptureSession CreateSession(VideoCaptureOptions options)
        => new VideoCaptureSession(options, _supportService, _nativeApi);
}

internal sealed class VideoCaptureSession : IVideoCaptureSession
{
    [ThreadStatic]
    private static int s_nativeCallbackDepth;

    private static readonly Lock SessionOwnershipLock = new();
    private static readonly ConcurrentBag<Delegate> FailedCallbackUnregistrationRoots = [];
    private static VideoCaptureSession? s_activeSession;

    private readonly IVideoCaptureSupportService _supportService;
    private readonly IVideoCaptureNativeApi _nativeApi;
    private VideoFrameCallback? _videoFrameCallback;
    private AudioSampleCallback? _audioSampleCallback;
    private int _hasObservedRecordingStart;
    private bool _disposed;

    public VideoCaptureSession(
        VideoCaptureOptions options,
        IVideoCaptureSupportService supportService,
        IVideoCaptureNativeApi nativeApi)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(supportService);
        ArgumentNullException.ThrowIfNull(nativeApi);
        Options = options;
        _supportService = supportService;
        _nativeApi = nativeApi;
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
        ThrowIfInsideNativeCallback();
        if (IsRecording)
        {
            throw new InvalidOperationException("Video capture is already recording.");
        }

        VideoCaptureSupportResult support = _supportService.GetSupport();
        if (!support.IsSupported)
        {
            throw new VideoCaptureNotSupportedException(support);
        }

        AcquireProcessOwnership();
        _hasObservedRecordingStart = 0;
        _videoFrameCallback = OnVideoFrameCaptured;
        _audioSampleCallback = OnAudioSampleCaptured;

        try
        {
            _nativeApi.RegisterVideoFrameCallback(_videoFrameCallback).EnsureSuccess();
            _nativeApi.RegisterAudioSampleCallback(_audioSampleCallback).EnsureSuccess();
            var nativeOptions = new NativeVideoCaptureOptions(Options);
            // A frame can arrive before the synchronous native start call returns.
            // Publish the starting state first so callback observers see a coherent session.
            IsRecording = true;
            _nativeApi.StartScreenRecording(in nativeOptions).EnsureSuccess();
        }
        catch
        {
            IsRecording = false;
            IsPaused = false;
            ClearCallbacks();
            ReleaseProcessOwnership();
            throw;
        }
    }

    public VideoCaptureResult Stop()
    {
        ThrowIfDisposed();
        ThrowIfInsideNativeCallback();
        if (!IsRecording)
        {
            throw new InvalidOperationException("Video capture is not recording.");
        }

        try
        {
            _nativeApi.StopScreenRecording().EnsureSuccess();
            return new VideoCaptureResult(Options.OutputPath);
        }
        finally
        {
            IsRecording = false;
            IsPaused = false;
            ClearCallbacks();
            ReleaseProcessOwnership();
        }
    }

    public void Cancel()
    {
        ThrowIfDisposed();
        ThrowIfInsideNativeCallback();
        if (!IsRecording)
        {
            return;
        }

        try
        {
            _ = _nativeApi.StopScreenRecording();
        }
        finally
        {
            IsRecording = false;
            IsPaused = false;
            ClearCallbacks();
            ReleaseProcessOwnership();
        }
    }

    public void Pause()
    {
        ThrowIfDisposed();
        ThrowIfInsideNativeCallback();
        _nativeApi.PauseScreenRecording().EnsureSuccess();
        IsPaused = true;
    }

    public void Resume()
    {
        ThrowIfDisposed();
        ThrowIfInsideNativeCallback();
        _nativeApi.ResumeScreenRecording().EnsureSuccess();
        IsPaused = false;
    }

    public void SetAudioCaptureEnabled(bool enabled)
    {
        ThrowIfDisposed();
        ThrowIfInsideNativeCallback();
        _nativeApi.SetScreenRecordingAudioEnabled(enabled ? 1u : 0u).EnsureSuccess();
    }

    public void SetAudioInputSource(string? sourceId)
    {
        ThrowIfDisposed();
        ThrowIfInsideNativeCallback();
        _nativeApi.SetScreenRecordingAudioInputSource(sourceId).EnsureSuccess();
    }

    public void SetAudioInputVolume(int volumePercentage)
    {
        ThrowIfDisposed();
        ThrowIfInsideNativeCallback();
        _nativeApi.SetScreenRecordingAudioInputVolume((uint)Math.Clamp(volumePercentage, 0, 100)).EnsureSuccess();
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

    internal void OnVideoFrameCaptured(ref VideoFrameData frameData)
    {
        s_nativeCallbackDepth++;
        try
        {
            // The native layer emits this callback only after the frame was
            // successfully accepted by the MP4 sink writer.
            NotifyRecordingStarted();
            InvokeSafely(FrameCaptured, new VideoFrameCapturedEventArgs(frameData), nameof(FrameCaptured));
        }
        catch (Exception exception)
        {
            ReportCallbackException(nameof(OnVideoFrameCaptured), exception);
        }
        finally
        {
            s_nativeCallbackDepth--;
        }
    }

    internal void OnAudioSampleCaptured(ref AudioSampleData sampleData)
    {
        s_nativeCallbackDepth++;
        try
        {
            // Audio can arrive before video encoder activation succeeds, so it
            // must never be used as evidence that recording has started.
            InvokeSafely(AudioSampleCaptured, new AudioSampleCapturedEventArgs(sampleData), nameof(AudioSampleCaptured));
        }
        catch (Exception exception)
        {
            ReportCallbackException(nameof(OnAudioSampleCaptured), exception);
        }
        finally
        {
            s_nativeCallbackDepth--;
        }
    }

    private void NotifyRecordingStarted()
    {
        if (Interlocked.Exchange(ref _hasObservedRecordingStart, 1) == 0)
        {
            InvokeSafely(RecordingStarted, EventArgs.Empty, nameof(RecordingStarted));
        }
    }

    private void InvokeSafely(EventHandler? handlers, EventArgs args, string eventName)
    {
        if (handlers is null)
        {
            return;
        }

        foreach (EventHandler handler in handlers.GetInvocationList())
        {
            try
            {
                handler(this, args);
            }
            catch (Exception exception)
            {
                ReportCallbackException(eventName, exception);
            }
        }
    }

    private void InvokeSafely<TEventArgs>(EventHandler<TEventArgs>? handlers, TEventArgs args, string eventName)
        where TEventArgs : EventArgs
    {
        if (handlers is null)
        {
            return;
        }

        foreach (EventHandler<TEventArgs> handler in handlers.GetInvocationList())
        {
            try
            {
                handler(this, args);
            }
            catch (Exception exception)
            {
                ReportCallbackException(eventName, exception);
            }
        }
    }

    private static void ReportCallbackException(string callbackName, Exception exception)
    {
        try
        {
            Trace.TraceError($"CaptureKit callback '{callbackName}' threw and was contained: {exception}");
        }
        catch
        {
            // Diagnostics must not allow an exception to cross reverse P/Invoke.
        }
    }

    private void ClearCallbacks()
    {
        try
        {
            CaptureRecorderResult audioResult = _nativeApi.RegisterAudioSampleCallback(null);
            if (audioResult.IsSuccess)
            {
                _audioSampleCallback = null;
            }
            else
            {
                RootFailedCallback(
                    _audioSampleCallback,
                    nameof(_audioSampleCallback),
                    new CaptureRecorderException(audioResult.Status, audioResult.HResult));
            }
        }
        catch (Exception exception)
        {
            RootFailedCallback(_audioSampleCallback, nameof(_audioSampleCallback), exception);
        }

        try
        {
            CaptureRecorderResult videoResult = _nativeApi.RegisterVideoFrameCallback(null);
            if (videoResult.IsSuccess)
            {
                _videoFrameCallback = null;
            }
            else
            {
                RootFailedCallback(
                    _videoFrameCallback,
                    nameof(_videoFrameCallback),
                    new CaptureRecorderException(videoResult.Status, videoResult.HResult));
            }
        }
        catch (Exception exception)
        {
            RootFailedCallback(_videoFrameCallback, nameof(_videoFrameCallback), exception);
        }
    }

    private static void RootFailedCallback(
        Delegate? callback,
        string callbackName,
        Exception exception)
    {
        if (callback is not null)
        {
            // A native function pointer may still reference this delegate. A small
            // intentional leak is safer than allowing GC to invalidate that pointer.
            FailedCallbackUnregistrationRoots.Add(callback);
        }

        ReportCallbackException(callbackName, exception);
    }

    private void AcquireProcessOwnership()
    {
        lock (SessionOwnershipLock)
        {
            if (s_activeSession is not null && !ReferenceEquals(s_activeSession, this))
            {
                throw new InvalidOperationException(
                    "Another video capture session is already active in this process.");
            }

            s_activeSession = this;
        }
    }

    private void ReleaseProcessOwnership()
    {
        lock (SessionOwnershipLock)
        {
            if (ReferenceEquals(s_activeSession, this))
            {
                s_activeSession = null;
            }
        }
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(VideoCaptureSession));
        }
    }

    private static void ThrowIfInsideNativeCallback()
    {
        if (s_nativeCallbackDepth != 0)
        {
            throw new InvalidOperationException(
                "Recorder control cannot be invoked synchronously from a capture callback. Dispatch the operation to another thread.");
        }
    }
}

internal interface IVideoCaptureNativeApi
{
    CaptureRecorderResult StartScreenRecording(in NativeVideoCaptureOptions options);
    CaptureRecorderResult PauseScreenRecording();
    CaptureRecorderResult ResumeScreenRecording();
    CaptureRecorderResult StopScreenRecording();
    CaptureRecorderResult SetScreenRecordingAudioEnabled(uint enabled);
    CaptureRecorderResult SetScreenRecordingAudioInputSource(string? sourceId);
    CaptureRecorderResult SetScreenRecordingAudioInputVolume(uint volumePercentage);
    CaptureRecorderResult RegisterVideoFrameCallback(VideoFrameCallback? callback);
    CaptureRecorderResult RegisterAudioSampleCallback(AudioSampleCallback? callback);
}

internal sealed class VideoCaptureNativeApi : IVideoCaptureNativeApi
{
    public CaptureRecorderResult StartScreenRecording(in NativeVideoCaptureOptions options)
        => NativeInterop.StartScreenRecording(in options);

    public CaptureRecorderResult PauseScreenRecording() => NativeInterop.PauseScreenRecording();
    public CaptureRecorderResult ResumeScreenRecording() => NativeInterop.ResumeScreenRecording();
    public CaptureRecorderResult StopScreenRecording() => NativeInterop.StopScreenRecording();

    public CaptureRecorderResult SetScreenRecordingAudioEnabled(uint enabled)
        => NativeInterop.SetScreenRecordingAudioEnabled(enabled);

    public CaptureRecorderResult SetScreenRecordingAudioInputSource(string? sourceId)
        => NativeInterop.SetScreenRecordingAudioInputSource(sourceId);

    public CaptureRecorderResult SetScreenRecordingAudioInputVolume(uint volumePercentage)
        => NativeInterop.SetScreenRecordingAudioInputVolume(volumePercentage);

    public CaptureRecorderResult RegisterVideoFrameCallback(VideoFrameCallback? callback)
        => NativeInterop.RegisterVideoFrameCallback(callback);

    public CaptureRecorderResult RegisterAudioSampleCallback(AudioSampleCallback? callback)
        => NativeInterop.RegisterAudioSampleCallback(callback);
}
