namespace CaptureKit.Abstractions;

public sealed record VideoCaptureOptions(
    CaptureTarget Target,
    string OutputPath,
    bool CaptureAudio = false,
    uint FrameRate = 30,
    uint VideoBitrate = 5_000_000,
    uint AudioBitrate = 128_000,
    string? AudioInputSourceId = null,
    int AudioInputVolumePercentage = 100);
