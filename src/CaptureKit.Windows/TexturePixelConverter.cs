using CaptureKit;

namespace CaptureKit.Windows;

public sealed class TexturePixelConverter : ITexturePixelConverter
{
    public bool ConvertTextureToPixelBuffer(
        nint texturePointer,
        nint devicePointer,
        nint contextPointer,
        byte[] outputBuffer,
        out uint rowPitch)
    {
        return NativeInterop.ConvertTextureToPixelBuffer(
            texturePointer,
            devicePointer,
            contextPointer,
            outputBuffer,
            (uint)outputBuffer.Length,
            out rowPitch);
    }
}
