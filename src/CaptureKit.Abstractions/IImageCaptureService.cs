namespace CaptureKit.Abstractions;

public interface IImageCaptureService
{
    ImageCaptureResult Capture(ImageCaptureRequest request);
}
