namespace CaptureKit;

public interface IImageCaptureService
{
    ImageCaptureResult Capture(ImageCaptureRequest request);
}
