#include "pch.h"
#include "CppUnitTest.h"
#include "RecorderCallbackContext.h"
#include "../../src/CaptureKit.Windows.Native.Recording/RecorderExceptionGuard.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CaptureKitWindowsNativeTests
{
    TEST_CLASS(RecorderCallbackContextTests)
    {
    public:
        TEST_METHOD(Scope_MarksCurrentThreadActiveAndRestoresState)
        {
            Assert::IsFalse(CaptureKit::Native::RecorderCallbackScope::IsActive());

            {
                CaptureKit::Native::RecorderCallbackScope scope;
                Assert::IsTrue(CaptureKit::Native::RecorderCallbackScope::IsActive());
            }

            Assert::IsFalse(CaptureKit::Native::RecorderCallbackScope::IsActive());
        }

        TEST_METHOD(NestedScopes_KeepContextActiveUntilOutermostScopeEnds)
        {
            CaptureKit::Native::RecorderCallbackScope outerScope;
            {
                CaptureKit::Native::RecorderCallbackScope innerScope;
                Assert::IsTrue(CaptureKit::Native::RecorderCallbackScope::IsActive());
            }

            Assert::IsTrue(CaptureKit::Native::RecorderCallbackScope::IsActive());
        }

        TEST_METHOD(RecorderGuard_RejectsControlCallsInsideCallback)
        {
            CaptureKit::Native::RecorderCallbackScope callbackScope;
            bool actionInvoked = false;

            CaptureRecorderResult result = GuardRecorderCall(
                L"RecorderCallbackContextTests",
                CaptureRecorderStatus::StartFailed,
                [&]() {
                    actionInvoked = true;
                    return CaptureRecorderResult{ CaptureRecorderStatus::Success, S_OK };
                });

            Assert::IsFalse(actionInvoked);
            Assert::AreEqual(
                static_cast<int>(CaptureRecorderStatus::InvalidState),
                static_cast<int>(result.status));
            Assert::AreEqual(
                static_cast<long>(E_ILLEGAL_METHOD_CALL),
                static_cast<long>(result.hresult));
        }
    };
}
