#include "pch.h"
#include "Scheduler.h"

namespace Core
{
    RuntimeState& GetRuntimeState()
    {
        return Scheduler::m_RuntimeState;
    }

    RuntimeState& Scheduler::State()
    {
        return GetRuntimeState();
    }

    void Scheduler::RegisterGameUpdateCallback(const std::string& name, CallbackFn callback)
    {
        RegisterHandler(m_GameUpdateCallbacks, name, std::move(callback));
        LOG_DEBUG("Scheduler", "[Scheduler] Registered GameUpdate callback: '%s' (total: %zu)",
            name.c_str(), m_GameUpdateCallbacks.size());
    }

    void Scheduler::RegisterGameTickCallback(const std::string& name, CallbackFn callback)
    {
        RegisterGameUpdateCallback(name, std::move(callback));
    }

    void Scheduler::RegisterGameplayTickCallback(const std::string& name, CallbackFn callback)
    {
        RegisterGameUpdateCallback(name, [cb = std::move(callback)]()
        {
            if (!State().CanRunGameplay())
                return;

            cb();
        });
    }

    void Scheduler::RegisterRenderCallback(const std::string& name, CallbackFn callback)
    {
        RegisterHandler(m_RenderCallbacks, name, std::move(callback));
    }

    void Scheduler::RegisterOverlayRenderCallback(const std::string& name, CallbackFn callback)
    {
        RegisterRenderCallback(name, [cb = std::move(callback)]()
        {
            if (!State().CanRenderOverlay())
                return;

            cb();
        });
    }

    void Scheduler::RegisterEventHandler(const std::string& name, EventHandlerFn handler)
    {
        RegisterHandler(m_EventHandlers, name, std::move(handler));
        LOG_DEBUG("Scheduler", "[Scheduler] Registered Event handler: '%s' (total: %zu)",
            name.c_str(), m_EventHandlers.size());
    }

    void Scheduler::RemoveGameUpdateCallback(const std::string& name)
    {
        RemoveHandler(m_GameUpdateCallbacks, name);
    }

    void Scheduler::RemoveGameTickCallback(const std::string& name)
    {
        RemoveGameUpdateCallback(name);
    }

    void Scheduler::RemoveRenderCallback(const std::string& name)
    {
        RemoveHandler(m_RenderCallbacks, name);
    }

    void Scheduler::RemoveEventHandler(const std::string& name)
    {
        RemoveHandler(m_EventHandlers, name);
    }

    void Scheduler::ExecuteCallbacks(const CallbackRegistry& registry)
    {
        std::vector<CallbackFn> callbacks;
        callbacks.reserve(registry.size());

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            for (const auto& [name, callback] : registry)
            {
                (void)name;
                callbacks.push_back(callback);
            }
        }

        for (const auto& callback : callbacks)
        {
            if (!callback)
                continue;

            try
            {
                callback();
            }
            catch (...)
            {
            }
        }
    }

    void Scheduler::OnGameUpdate()
    {
        Logger::LogThrottled(Logger::Level::Debug, "Scheduler", 5000,
            "[Scheduler] OnGameUpdate tick: %zu callbacks, CanRunGameplay=%d",
            m_GameUpdateCallbacks.size(), (int)m_RuntimeState.CanRunGameplay());
        ExecuteCallbacks(m_GameUpdateCallbacks);
    }

    void Scheduler::OnRenderFrame()
    {
        ExecuteCallbacks(m_RenderCallbacks);
    }

    bool Scheduler::DispatchEvent(const SchedulerGameEvent& event)
    {
        std::vector<std::pair<std::string, EventHandlerFn>> handlers;
        handlers.reserve(m_EventHandlers.size());

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            for (const auto& [name, handler] : m_EventHandlers)
            {
                handlers.push_back({ name, handler });
            }
        }

        for (const auto& [name, handler] : handlers)
        {
            if (!handler)
                continue;

            try
            {
                if (handler(event))
                {
                    LOG_DEBUG("Scheduler", "[Scheduler] Event '%s' intercepted by handler: '%s'",
                        event.Function ? event.Function->GetName().c_str() : "null", name.c_str());
                    return true;
                }
            }
            catch (...)
            {
                LOG_ERROR("Scheduler", "[Scheduler] Fatal error in handler: '%s'", name.c_str());
            }
        }

        return false;
    }
}
