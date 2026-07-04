namespace CaptureKit.Abstractions;

public sealed class VideoFrameCapturedEventArgs : EventArgs
{
    public VideoFrameCapturedEventArgs(VideoFrameData frameData)
    {
        FrameData = frameData;
    }

    public VideoFrameData FrameData { get; }
}
