#include "pch.h"
#include "CppUnitTest.h"
#include "WindowsScreenshotCapture.h"
#include "NativeExceptionBoundary.h"
#include "../../src/CaptureKit.Windows.Native.Recording/ScreenshotExports.h"
#include "../../src/CaptureKit.Windows.Native.Recording/ScreenshotHandle.h"

#include <array>
#include <future>
#include <limits>
#include <memory>
#include <new>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CaptureInteropTests
{
    namespace
    {
        MonitorScreenshot MakeMonitor(int left = 0, int top = 0, uint8_t value = 127)
        {
            MonitorScreenshot monitor;
            monitor.width = 1;
            monitor.height = 2;
            monitor.left = left;
            monitor.top = top;
            monitor.pixelData.assign(8, value);
            return monitor;
        }

        using ScreenshotPtr = std::unique_ptr<ScreenshotHandle, decltype(&FreeScreenshot)>;
    }

    TEST_CLASS(ScreenshotSafetyTests)
    {
    public:
        TEST_METHOD(NativeGuard_ContainsAllocationAndUnknownExceptions)
        {
            using CaptureKit::Native::GuardNativeCall;
            Assert::IsNull(GuardNativeCall(L"allocation test", static_cast<void*>(nullptr), []() -> void* {
                throw std::bad_alloc();
            }));
            Assert::IsFalse(GuardNativeCall(L"unknown exception test", false, []() -> bool {
                throw 42;
            }));
            Assert::IsTrue(GuardNativeCall(L"success test", false, [] { return true; }));
        }

        TEST_METHOD(Combine_RejectsTruncatedPixelBuffer)
        {
            WindowsScreenshotCapture capture;
            auto monitor = MakeMonitor();
            monitor.pixelData.resize(4); // The second row is missing.
            auto result = capture.CombineMonitors({monitor});
            Assert::IsTrue(result.IsError());
            Assert::AreEqual(E_INVALIDARG, result.Error().hr);
        }

        TEST_METHOD(Combine_RejectsInvalidDimensionsBeforeArithmetic)
        {
            WindowsScreenshotCapture capture;
            for (int width : {0, -1, (std::numeric_limits<int>::max)()})
            {
                auto monitor = MakeMonitor();
                monitor.width = width;
                auto result = capture.CombineMonitors({monitor});
                Assert::IsTrue(result.IsError());
                Assert::AreEqual(E_INVALIDARG, result.Error().hr);
            }
        }

        TEST_METHOD(Combine_RejectsExtremeCoordinateSpanWithoutOverflow)
        {
            WindowsScreenshotCapture capture;
            auto result = capture.CombineMonitors({
                MakeMonitor((std::numeric_limits<int>::min)()),
                MakeMonitor((std::numeric_limits<int>::max)())});
            Assert::IsTrue(result.IsError());
        }

        TEST_METHOD(Combine_PreservesRowsAndGapsAtNegativeCoordinates)
        {
            WindowsScreenshotCapture capture;
            auto result = capture.CombineMonitors({MakeMonitor(-2, -1, 10), MakeMonitor(0, -1, 20)});
            Assert::IsTrue(result.IsOk());
            const auto& combined = result.Value();
            Assert::AreEqual(3, combined.width);
            Assert::AreEqual(2, combined.height);
            Assert::AreEqual(-2, combined.left);
            Assert::AreEqual(-1, combined.top);
            const std::vector<uint8_t> expected{
                10, 10, 10, 10, 0, 0, 0, 0, 20, 20, 20, 20,
                10, 10, 10, 10, 0, 0, 0, 0, 20, 20, 20, 20};
            Assert::IsTrue(expected == combined.pixelData);
        }

        TEST_METHOD(CombineHandles_RepeatedUsePreservesInputPixels)
        {
            ScreenshotHandle first;
            ScreenshotHandle second;
            first.monitorData = MakeMonitor(0, 0, 10);
            second.monitorData = MakeMonitor(1, 0, 20);
            ScreenshotHandle* inputs[]{&first, &second};

            for (int iteration = 0; iteration < 100; ++iteration)
            {
                ScreenshotPtr combined(CombineScreenshots(inputs, 2), FreeScreenshot);
                Assert::IsNotNull(combined.get());
                Assert::AreEqual(size_t(16), combined->combinedData.pixelData.size());
                std::array<uint8_t, 8> original{};
                Assert::IsTrue(CopyScreenshotPixels(&first, original.data(), int(original.size())));
                Assert::IsTrue(first.monitorData.pixelData == std::vector<uint8_t>(8, 10));
                Assert::IsTrue(second.monitorData.pixelData == std::vector<uint8_t>(8, 20));
            }
        }

        TEST_METHOD(CombineHandles_FailedCombinePreservesEarlierInputs)
        {
            ScreenshotHandle first;
            first.monitorData = MakeMonitor();
            ScreenshotHandle* inputs[]{&first, nullptr};
            ScreenshotPtr combined(CombineScreenshots(inputs, 2), FreeScreenshot);
            Assert::IsNull(combined.get());
            Assert::IsTrue(first.monitorData.pixelData == std::vector<uint8_t>(8, 127));
        }

        TEST_METHOD(CombineHandles_AcceptsRepeatedReferenceWithoutMovingPixels)
        {
            ScreenshotHandle first;
            first.monitorData = MakeMonitor();
            ScreenshotHandle* inputs[]{&first, &first};
            ScreenshotPtr combined(CombineScreenshots(inputs, 2), FreeScreenshot);
            Assert::IsNotNull(combined.get());
            Assert::IsTrue(combined->combinedData.pixelData == first.monitorData.pixelData);
        }

        TEST_METHOD(Save_RejectsIncompletePixelsAndOverflowingWicSize)
        {
            ScreenshotHandle handle;
            handle.monitorData = MakeMonitor();
            handle.monitorData.pixelData.resize(1);
            Assert::IsFalse(SaveScreenshotToPng(&handle, L"?:\\capturekit-invalid.png"));

            WindowsScreenshotCapture capture;
            const uint8_t pixel = 0;
            auto result = capture.SaveToPng(&pixel, 32768, 32768, L"?:\\capturekit-invalid.png");
            Assert::IsTrue(result.IsError());
            Assert::AreEqual(E_INVALIDARG, result.Error().hr);
        }

        TEST_METHOD(Save_RepeatedFailuresBalanceExistingStaInitialization)
        {
            // A dedicated thread lets us detect a leaked S_FALSE initialization
            // by changing apartments after the owner's CoUninitialize.
            const HRESULT changedApartment = std::async(std::launch::async, [] {
                HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                if (FAILED(hr)) return hr;
                WindowsScreenshotCapture capture;
                const std::array<uint8_t, 4> pixel{0, 0, 0, 255};
                for (int i = 0; i < 20; ++i)
                {
                    (void)capture.SaveToPng(pixel.data(), 1, 1, L"?:\\capturekit-invalid.png");
                }
                CoUninitialize();
                hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                if (SUCCEEDED(hr)) CoUninitialize();
                return hr;
            }).get();
            Assert::AreEqual(S_OK, changedApartment);
        }
    };
}
