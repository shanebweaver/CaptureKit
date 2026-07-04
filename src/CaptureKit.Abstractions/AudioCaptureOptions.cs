namespace CaptureKit.Abstractions;

public sealed record AudioCaptureOptions(
    string OutputPath,
    bool CaptureAudio = true,
    string? AudioInputSourceId = null,
    int AudioInputVolumePercentage = 100);
