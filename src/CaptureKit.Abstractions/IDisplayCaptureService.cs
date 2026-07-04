using System.Drawing;

namespace CaptureKit.Abstractions;

public interface IDisplayCaptureService
{
    IReadOnlyList<DisplayCapture> CaptureDisplays();
    IReadOnlyList<CaptureWindow> GetWindows();
    Bitmap CombineDisplays(IReadOnlyList<DisplayCapture> displays);
    Bitmap CreateBitmap(DisplayCapture display);
    Bitmap CreateCroppedBitmap(Bitmap image, Rectangle area, float scale);
}
