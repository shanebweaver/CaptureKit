using System.Reflection;
using System.Runtime.InteropServices;

namespace CaptureKit.Windows;

internal static class NativeScreenshotLibrary
{
    private static readonly string[] RequiredExports =
    [
        nameof(NativeInterop.CaptureMonitorScreenshot),
        nameof(NativeInterop.CaptureAllMonitorsScreenshot),
        nameof(NativeInterop.GetScreenshotInfo),
        nameof(NativeInterop.CopyScreenshotPixels),
        nameof(NativeInterop.SaveScreenshotToPng),
        nameof(NativeInterop.FreeScreenshot),
        nameof(NativeInterop.CombineScreenshots),
    ];

    private static readonly Lazy<nint> LibraryHandle = new(
        LoadAndValidate,
        LazyThreadSafetyMode.ExecutionAndPublication);

    internal static void EnsureAvailable()
        => _ = LibraryHandle.Value;

    private static nint LoadAndValidate()
    {
        nint handle = nint.Zero;

        try
        {
            Assembly assembly = typeof(NativeScreenshotLibrary).Assembly;
            handle = NativeLibrary.Load(
                NativeInterop.ScreenshotNativeLibraryName,
                assembly,
                DllImportSearchPath.ApplicationDirectory | DllImportSearchPath.AssemblyDirectory);

            foreach (string export in RequiredExports)
            {
                if (!NativeLibrary.TryGetExport(handle, export, out _))
                {
                    throw new EntryPointNotFoundException(
                        $"The native screenshot library does not export the required function '{export}'.");
                }
            }

            return handle;
        }
        catch (Exception exception) when (exception is DllNotFoundException
            or BadImageFormatException
            or EntryPointNotFoundException)
        {
            if (handle != nint.Zero)
            {
                NativeLibrary.Free(handle);
            }

            throw new PlatformNotSupportedException(
                $"CaptureKit could not load a compatible '{NativeInterop.ScreenshotNativeLibraryName}'. " +
                "Verify that the native runtime asset matches the process architecture. " +
                "The underlying loader error identifies any missing Windows component.",
                exception);
        }
    }
}
