# CaptureKit

Windows-native screen, video, and audio capture for .NET apps.

The repo produces two packages:

- `CaptureKit.Abstractions`: public capture contracts and data types.
- `CaptureKit.Windows`: Windows implementation plus native Windows assets for x64 and ARM64.

The native implementation uses Windows Graphics Capture, Media Foundation, WASAPI, Direct3D 11, WIC, and WIL.
