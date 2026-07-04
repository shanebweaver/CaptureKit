namespace CaptureKit;

public interface IVideoCaptureService
{
    IVideoCaptureSession CreateSession(VideoCaptureOptions options);
}

public interface IVideoCaptureSession : IDisposable
{
    event EventHandler? RecordingStarted;
    event EventHandler<VideoFrameCapturedEventArgs>? FrameCaptured;
    event EventHandler<AudioSampleCapturedEventArgs>? AudioSampleCaptured;

    bool IsRecording { get; }
    bool IsPaused { get; }
    VideoCaptureOptions Options { get; }

    void Start();
    VideoCaptureResult Stop();
    void Cancel();
    void Pause();
    void Resume();
    void SetAudioCaptureEnabled(bool enabled);
    void SetAudioInputSource(string? sourceId);
    void SetAudioInputVolume(int volumePercentage);
}
