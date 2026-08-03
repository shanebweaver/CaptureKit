#include "pch.h"
#include "MediaFoundationLifecycleManager.h"

// Initialize static reference counter
std::atomic<int> MediaFoundationLifecycleManager::s_refCount{0};
std::mutex MediaFoundationLifecycleManager::s_lifecycleMutex;

MediaFoundationLifecycleManager::MediaFoundationLifecycleManager()
    : m_initialized(false)
    , m_initHr(S_OK)
{
    std::lock_guard<std::mutex> lock(s_lifecycleMutex);
    const int currentCount = s_refCount.load(std::memory_order_acquire);

    if (currentCount == 0)
    {
        m_initHr = MFStartup(MF_VERSION);
        m_initialized = SUCCEEDED(m_initHr);
        if (m_initialized)
        {
            s_refCount.store(1, std::memory_order_release);
        }
    }
    else
    {
        s_refCount.fetch_add(1, std::memory_order_acq_rel);
        m_initialized = true;
        m_initHr = S_OK;
    }
}

MediaFoundationLifecycleManager::~MediaFoundationLifecycleManager()
{
    if (m_initialized)
    {
        std::lock_guard<std::mutex> lock(s_lifecycleMutex);
        int prevCount = s_refCount.fetch_sub(1, std::memory_order_acq_rel);
        
        if (prevCount == 1)
        {
            MFShutdown();
        }
    }
}
