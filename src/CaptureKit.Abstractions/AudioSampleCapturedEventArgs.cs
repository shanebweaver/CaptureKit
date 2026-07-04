namespace CaptureKit.Abstractions;

public sealed class AudioSampleCapturedEventArgs : EventArgs
{
    public AudioSampleCapturedEventArgs(AudioSampleData sampleData)
    {
        SampleData = sampleData;
    }

    public AudioSampleData SampleData { get; }
}
