namespace CaptureKit.Abstractions;

public interface ITexturePixelConverter
{
    bool ConvertTextureToPixelBuffer(
        nint texturePointer,
        nint devicePointer,
        nint contextPointer,
        byte[] outputBuffer,
        out uint rowPitch);
}
