namespace CaptureKit.Abstractions;

public interface IAudioCaptureService
{
    IAudioCaptureSession CreateSession(AudioCaptureOptions options);
}

public interface IAudioCaptureSession : IDisposable
{
    event EventHandler<AudioSampleCapturedEventArgs>? SampleCaptured;

    bool IsRecording { get; }
    bool IsPaused { get; }
    AudioCaptureOptions Options { get; }

    void Start();
    AudioCaptureResult Stop();
    void Cancel();
    void Pause();
    void Resume();
    void SetAudioCaptureEnabled(bool enabled);
    void SetAudioInputSource(string? sourceId);
    void SetAudioInputVolume(int volumePercentage);
}
