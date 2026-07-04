namespace CaptureKit.Abstractions;

public sealed record ImageCaptureRequest(CaptureTarget Target, string OutputPath);
