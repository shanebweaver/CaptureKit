using CaptureKit.Abstractions;
using FluentAssertions;
using System.Reflection;

namespace CaptureKit.Windows.Tests;

[TestClass]
public sealed class AudioCaptureCallbackBoundaryTests
{
    [TestMethod]
    public void Stop_CallbackUnregistrationFailure_PreservesManagedDelegateRoot()
    {
        var nativeApi = new TrackingAudioNativeApi();
        using var session = new AudioCaptureSession(
            new AudioCaptureOptions("capture.wav"),
            nativeApi);
        session.Start();
        nativeApi.FailCallbackUnregistration = true;

        session.Stop();

        nativeApi.HasAudioCallback.Should().BeTrue();
        typeof(AudioCaptureSession)
            .GetField("_audioSampleCallback", BindingFlags.Instance | BindingFlags.NonPublic)!
            .GetValue(session)
            .Should()
            .NotBeNull();
    }

    private sealed class TrackingAudioNativeApi : IAudioCaptureNativeApi
    {
        private AudioSampleCallback? _audioCallback;

        public bool FailCallbackUnregistration { get; set; }
        public bool HasAudioCallback => _audioCallback is not null;

        public CaptureRecorderResult StartAudioRecording(in NativeAudioCaptureOptions options)
            => Success();

        public CaptureRecorderResult PauseAudioRecording() => Success();
        public CaptureRecorderResult ResumeAudioRecording() => Success();
        public CaptureRecorderResult StopAudioRecording() => Success();
        public CaptureRecorderResult SetAudioRecordingEnabled(uint enabled) => Success();
        public CaptureRecorderResult SetAudioRecordingInputSource(string? sourceId) => Success();
        public CaptureRecorderResult SetAudioRecordingInputVolume(uint volumePercentage) => Success();

        public CaptureRecorderResult RegisterAudioRecordingSampleCallback(AudioSampleCallback? callback)
        {
            if (callback is null && FailCallbackUnregistration)
            {
                return new CaptureRecorderResult(
                    CaptureRecorderStatus.InvalidState,
                    unchecked((int)0x80070005));
            }

            _audioCallback = callback;
            return Success();
        }

        private static CaptureRecorderResult Success()
            => new(CaptureRecorderStatus.Success, 0);
    }
}
