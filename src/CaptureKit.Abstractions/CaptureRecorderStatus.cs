namespace CaptureKit.Abstractions;

public enum CaptureRecorderStatus
{
    Success = 0,
    InvalidArgument = 1,
    InvalidState = 2,
    StartFailed = 3,
    NoActiveSession = 4
}
