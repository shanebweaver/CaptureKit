using CaptureKit.Abstractions;
using CaptureKit.Windows.DependencyInjection;
using FluentAssertions;
using Microsoft.Extensions.DependencyInjection;

namespace CaptureKit.Windows.Tests;

[TestClass]
public sealed class WindowsVideoCaptureSupportServiceTests
{
    [TestMethod]
    public void GetSupport_Pre1903_DoesNotCallGraphicsCaptureApi()
    {
        bool graphicsCheckCalled = false;
        var service = new WindowsVideoCaptureSupportService(
            () => new Version(10, 0, 17763),
            () =>
            {
                graphicsCheckCalled = true;
                return true;
            });

        VideoCaptureSupportResult result = service.GetSupport();

        result.IsSupported.Should().BeFalse();
        result.Reason.Should().Be(VideoCaptureSupportReason.UnsupportedOperatingSystem);
        graphicsCheckCalled.Should().BeFalse();
    }

    [TestMethod]
    public void GetSupport_GraphicsCaptureUnsupported_ReturnsStableReason()
    {
        var service = new WindowsVideoCaptureSupportService(
            () => new Version(10, 0, WindowsVideoCaptureSupportService.MinimumSupportedBuild),
            () => false);

        VideoCaptureSupportResult result = service.GetSupport();

        result.IsSupported.Should().BeFalse();
        result.Reason.Should().Be(VideoCaptureSupportReason.GraphicsCaptureNotSupported);
    }

    [TestMethod]
    public void GetSupport_SupportedDevice_ReturnsSupported()
    {
        var service = new WindowsVideoCaptureSupportService(
            () => new Version(10, 0, 26100),
            () => true);

        VideoCaptureSupportResult result = service.GetSupport();

        result.IsSupported.Should().BeTrue();
        result.Reason.Should().Be(VideoCaptureSupportReason.Supported);
    }

    [TestMethod]
    public void GetSupport_CheckThrows_ReturnsFailureAndHResult()
    {
        const int failure = unchecked((int)0x80004005);
        var service = new WindowsVideoCaptureSupportService(
            () => new Version(10, 0, 26100),
            () => throw new CaptureRecorderException(CaptureRecorderStatus.StartFailed, failure));

        VideoCaptureSupportResult result = service.GetSupport();

        result.IsSupported.Should().BeFalse();
        result.Reason.Should().Be(VideoCaptureSupportReason.SupportCheckFailed);
        result.HResult.Should().Be(failure);
    }

    [TestMethod]
    public void AddCaptureKitWindows_RegistersSupportService()
    {
        using ServiceProvider provider = new ServiceCollection()
            .AddCaptureKitWindows()
            .BuildServiceProvider();

        provider.GetService<IVideoCaptureSupportService>()
            .Should().BeOfType<WindowsVideoCaptureSupportService>();
    }
}
