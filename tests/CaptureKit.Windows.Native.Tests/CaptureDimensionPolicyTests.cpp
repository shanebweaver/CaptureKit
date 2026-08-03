#include "pch.h"
#include "CppUnitTest.h"
#include "CaptureDimensionPolicy.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CaptureKitWindowsNativeTests
{
    TEST_CLASS(CaptureDimensionPolicyTests)
    {
    public:
        TEST_METHOD(PreservesEvenDimensionsWithinBounds)
        {
            NormalizedCaptureDimensions result{};

            Assert::IsTrue(TryNormalizeCaptureDimensions(1280, 720, 1920, 1080, &result));
            Assert::AreEqual(uint32_t{1280}, result.width);
            Assert::AreEqual(uint32_t{720}, result.height);
        }

        TEST_METHOD(TrimsOddDimensionsForEncoderCompatibility)
        {
            NormalizedCaptureDimensions result{};

            Assert::IsTrue(TryNormalizeCaptureDimensions(1279, 719, 1279, 719, &result));
            Assert::AreEqual(uint32_t{1278}, result.width);
            Assert::AreEqual(uint32_t{718}, result.height);
        }

        TEST_METHOD(ClampsSinglePixelDpiRoundingOverrun)
        {
            NormalizedCaptureDimensions result{};

            Assert::IsTrue(TryNormalizeCaptureDimensions(1921, 1081, 1920, 1080, &result));
            Assert::AreEqual(uint32_t{1920}, result.width);
            Assert::AreEqual(uint32_t{1080}, result.height);
        }

        TEST_METHOD(RejectsLargerOutOfBoundsTarget)
        {
            NormalizedCaptureDimensions result{};

            Assert::IsFalse(TryNormalizeCaptureDimensions(1922, 1080, 1920, 1080, &result));
        }

        TEST_METHOD(RejectsDimensionsTooSmallForEvenEncoding)
        {
            NormalizedCaptureDimensions result{};

            Assert::IsFalse(TryNormalizeCaptureDimensions(1, 100, 1, 100, &result));
            Assert::IsFalse(TryNormalizeCaptureDimensions(100, 1, 100, 1, &result));
        }
    };
}
