namespace CaptureKit.Abstractions;

public readonly record struct CaptureOptions(CaptureMode CaptureMode, CaptureTargetKind TargetKind)
{
    public static CaptureOptions ImageDefault => new(CaptureMode.Image, CaptureTargetKind.Rectangle);
    public static CaptureOptions VideoDefault => new(CaptureMode.Video, CaptureTargetKind.Monitor);
}
