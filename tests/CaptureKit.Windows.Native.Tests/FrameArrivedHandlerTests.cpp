#include "pch.h"
#include "CppUnitTest.h"
#include <windows.foundation.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.directx.direct3d11.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "FrameArrivedHandler.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CaptureInteropTests
{
    TEST_CLASS(FrameArrivedHandlerTests)
    {
    public:
        TEST_METHOD(QueryInterface_ExposesAgileObjectForFreeThreadedFramePool)
        {
            FrameArrivedHandler* handler = new FrameArrivedHandler(
                [](const VideoFrameReadyEventArgs&) {},
                nullptr);

            wil::com_ptr<IAgileObject> agileHandler;
            const HRESULT hr = handler->QueryInterface(IID_PPV_ARGS(agileHandler.put()));

            Assert::AreEqual(S_OK, hr);
            Assert::IsNotNull(agileHandler.get());

            wil::com_ptr<IUnknown> canonicalIdentity;
            const HRESULT identityHr = agileHandler->QueryInterface(
                IID_PPV_ARGS(canonicalIdentity.put()));

            Assert::AreEqual(S_OK, identityHr);
            Assert::IsNotNull(canonicalIdentity.get());
            handler->Release();
        }
    };
}
