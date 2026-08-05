using CaptureKit.Abstractions;
using FluentAssertions;
using System.Reflection;

namespace CaptureKit.Windows.Tests;

[TestClass]
public sealed class CaptureRecorderBoundaryTests
{
    [TestMethod]
    public void EnsureSuccess_Failure_PreservesStatusAndHResult()
    {
        const int accessDenied = unchecked((int)0x80070005);
        var result = new CaptureRecorderResult(CaptureRecorderStatus.StartFailed, accessDenied);

        Action act = result.EnsureSuccess;

        var exception = act.Should().Throw<CaptureRecorderException>().Which;
        exception.Status.Should().Be(CaptureRecorderStatus.StartFailed);
        exception.ErrorCode.Should().Be(accessDenied);
        exception.HResult.Should().Be(accessDenied);
    }

    [TestMethod]
    public void EnsureSuccess_Success_DoesNotThrow()
    {
        var result = new CaptureRecorderResult(CaptureRecorderStatus.Success, 0);

        Action act = result.EnsureSuccess;

        act.Should().NotThrow();
    }

    [TestMethod]
    public void AudioSample_DoesNotReportRecordingStarted()
    {
        using var session = CreateSession();
        int recordingStartedCount = 0;
        int audioSampleCount = 0;
        session.RecordingStarted += (_, _) => recordingStartedCount++;
        session.AudioSampleCaptured += (_, _) => audioSampleCount++;
        var sample = new AudioSampleData();

        session.OnAudioSampleCaptured(ref sample);

        recordingStartedCount.Should().Be(0);
        audioSampleCount.Should().Be(1);
    }

    [TestMethod]
    public void SuccessfulVideoCallback_ReportsRecordingStartedOnce()
    {
        using var session = CreateSession();
        int recordingStartedCount = 0;
        session.RecordingStarted += (_, _) => recordingStartedCount++;
        var frame = new VideoFrameData();

        session.OnVideoFrameCaptured(ref frame);
        session.OnVideoFrameCaptured(ref frame);

        recordingStartedCount.Should().Be(1);
    }

    [TestMethod]
    public void ManagedEventExceptions_DoNotEscapeReversePInvokeCallbacks()
    {
        using var session = CreateSession();
        session.RecordingStarted += (_, _) => throw new InvalidOperationException("recording started subscriber");
        session.FrameCaptured += (_, _) => throw new InvalidOperationException("frame subscriber");
        session.AudioSampleCaptured += (_, _) => throw new InvalidOperationException("audio subscriber");
        var frame = new VideoFrameData();
        var sample = new AudioSampleData();

        Action videoCallback = () => session.OnVideoFrameCaptured(ref frame);
        Action audioCallback = () => session.OnAudioSampleCaptured(ref sample);

        videoCallback.Should().NotThrow();
        audioCallback.Should().NotThrow();
    }

    [TestMethod]
    public void ReentrantStop_FromVideoCallback_PreservesSessionAndCallbackRegistration()
    {
        var nativeApi = new TrackingNativeApi();
        using var session = new VideoCaptureSession(
            new VideoCaptureOptions(CaptureTarget.Monitor((nint)1), "capture.mp4"),
            new StubSupportService(VideoCaptureSupportResult.Supported()),
            nativeApi);
        int frameCount = 0;
        session.FrameCaptured += (_, _) => frameCount++;
        session.FrameCaptured += (_, _) => session.Stop();
        session.Start();

        nativeApi.EmitVideoFrame();
        nativeApi.EmitVideoFrame();

        frameCount.Should().Be(2);
        session.IsRecording.Should().BeTrue();
        nativeApi.StopCallCount.Should().Be(0);
        nativeApi.HasVideoCallback.Should().BeTrue();
    }

    [TestMethod]
    public void ReentrantCancel_FromVideoCallback_PreservesSessionAndCallbackRegistration()
    {
        var nativeApi = new TrackingNativeApi();
        using var session = new VideoCaptureSession(
            new VideoCaptureOptions(CaptureTarget.Monitor((nint)1), "capture.mp4"),
            new StubSupportService(VideoCaptureSupportResult.Supported()),
            nativeApi);
        int frameCount = 0;
        session.FrameCaptured += (_, _) => frameCount++;
        session.FrameCaptured += (_, _) => session.Cancel();
        session.Start();

        nativeApi.EmitVideoFrame();
        nativeApi.EmitVideoFrame();

        frameCount.Should().Be(2);
        session.IsRecording.Should().BeTrue();
        nativeApi.StopCallCount.Should().Be(0);
        nativeApi.HasVideoCallback.Should().BeTrue();
    }

    [TestMethod]
    public void Start_UnsupportedDevice_RejectsBeforeNativeInterop()
    {
        var nativeApi = new TrackingNativeApi();
        using var session = new VideoCaptureSession(
            new VideoCaptureOptions(CaptureTarget.Monitor((nint)1), "capture.mp4"),
            new StubSupportService(VideoCaptureSupportResult.Unsupported(
                VideoCaptureSupportReason.GraphicsCaptureNotSupported)),
            nativeApi);

        Action act = session.Start;

        var exception = act.Should().Throw<VideoCaptureNotSupportedException>().Which;
        exception.Reason.Should().Be(VideoCaptureSupportReason.GraphicsCaptureNotSupported);
        nativeApi.CallCount.Should().Be(0);
    }

    [TestMethod]
    public void Start_WhenAnotherSessionOwnsRecorder_RejectsBeforeNativeInterop()
    {
        var firstNativeApi = new TrackingNativeApi();
        var secondNativeApi = new TrackingNativeApi();
        using var firstSession = new VideoCaptureSession(
            new VideoCaptureOptions(CaptureTarget.Monitor((nint)1), "first.mp4"),
            new StubSupportService(VideoCaptureSupportResult.Supported()),
            firstNativeApi);
        using var secondSession = new VideoCaptureSession(
            new VideoCaptureOptions(CaptureTarget.Monitor((nint)1), "second.mp4"),
            new StubSupportService(VideoCaptureSupportResult.Supported()),
            secondNativeApi);
        firstSession.Start();

        Action act = secondSession.Start;

        act.Should().Throw<InvalidOperationException>()
            .WithMessage("*already active*");
        firstSession.IsRecording.Should().BeTrue();
        firstNativeApi.HasVideoCallback.Should().BeTrue();
        secondNativeApi.CallCount.Should().Be(0);
    }

    [TestMethod]
    public void Stop_CallbackUnregistrationFailure_PreservesManagedDelegateRoots()
    {
        var nativeApi = new TrackingNativeApi();
        using var session = new VideoCaptureSession(
            new VideoCaptureOptions(CaptureTarget.Monitor((nint)1), "capture.mp4"),
            new StubSupportService(VideoCaptureSupportResult.Supported()),
            nativeApi);
        session.Start();
        nativeApi.FailCallbackUnregistration = true;

        session.Stop();

        nativeApi.HasVideoCallback.Should().BeTrue();
        nativeApi.HasAudioCallback.Should().BeTrue();
        GetCallbackField(session, "_videoFrameCallback").Should().NotBeNull();
        GetCallbackField(session, "_audioSampleCallback").Should().NotBeNull();
    }

    private static VideoCaptureSession CreateSession()
        => new(
            new VideoCaptureOptions(CaptureTarget.Monitor((nint)1), "capture.mp4"),
            new StubSupportService(VideoCaptureSupportResult.Supported()),
            new TrackingNativeApi());

    private static object? GetCallbackField(VideoCaptureSession session, string fieldName)
        => typeof(VideoCaptureSession)
            .GetField(fieldName, BindingFlags.Instance | BindingFlags.NonPublic)!
            .GetValue(session);

    private sealed class StubSupportService(VideoCaptureSupportResult result) : IVideoCaptureSupportService
    {
        public VideoCaptureSupportResult GetSupport() => result;
    }

    private sealed class TrackingNativeApi : IVideoCaptureNativeApi
    {
        private VideoFrameCallback? _videoCallback;
        private AudioSampleCallback? _audioCallback;

        public int CallCount { get; private set; }
        public int StopCallCount { get; private set; }
        public bool FailCallbackUnregistration { get; set; }
        public bool HasVideoCallback => _videoCallback is not null;
        public bool HasAudioCallback => _audioCallback is not null;

        public CaptureRecorderResult StartScreenRecording(in NativeVideoCaptureOptions options) => Called();
        public CaptureRecorderResult PauseScreenRecording() => Called();
        public CaptureRecorderResult ResumeScreenRecording() => Called();
        public CaptureRecorderResult StopScreenRecording()
        {
            StopCallCount++;
            return Called();
        }
        public CaptureRecorderResult SetScreenRecordingAudioEnabled(uint enabled) => Called();
        public CaptureRecorderResult SetScreenRecordingAudioInputSource(string? sourceId) => Called();
        public CaptureRecorderResult SetScreenRecordingAudioInputVolume(uint volumePercentage) => Called();
        public CaptureRecorderResult RegisterVideoFrameCallback(VideoFrameCallback? callback)
        {
            if (callback is null && FailCallbackUnregistration)
            {
                return FailedCallbackUnregistration();
            }

            _videoCallback = callback;
            return Called();
        }

        public CaptureRecorderResult RegisterAudioSampleCallback(AudioSampleCallback? callback)
        {
            if (callback is null && FailCallbackUnregistration)
            {
                return FailedCallbackUnregistration();
            }

            _audioCallback = callback;
            return Called();
        }

        private CaptureRecorderResult Called()
        {
            CallCount++;
            return new CaptureRecorderResult(CaptureRecorderStatus.Success, 0);
        }

        private CaptureRecorderResult FailedCallbackUnregistration()
        {
            CallCount++;
            return new CaptureRecorderResult(
                CaptureRecorderStatus.InvalidState,
                unchecked((int)0x80070005));
        }

        public void EmitVideoFrame()
        {
            var frame = new VideoFrameData();
            _videoCallback?.Invoke(ref frame);
        }
    }
}
