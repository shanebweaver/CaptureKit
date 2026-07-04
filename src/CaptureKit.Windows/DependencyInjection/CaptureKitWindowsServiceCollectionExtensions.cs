using CaptureKit.Abstractions;
using Microsoft.Extensions.DependencyInjection;

namespace CaptureKit.Windows.DependencyInjection;

public static class CaptureKitWindowsServiceCollectionExtensions
{
    public static IServiceCollection AddCaptureKitWindows(this IServiceCollection services)
    {
        services.AddSingleton<IDisplayCaptureService, DisplayCaptureService>();
        services.AddSingleton<IImageCaptureService, ImageCaptureService>();
        services.AddSingleton<IVideoCaptureService, VideoCaptureService>();
        services.AddSingleton<IAudioCaptureService, AudioCaptureService>();
        services.AddSingleton<ITexturePixelConverter, TexturePixelConverter>();

        return services;
    }
}
