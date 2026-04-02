#pragma once
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace Core
{
	struct SchedulerGameEvent
	{
		const SDK::UObject* Object = nullptr;
		SDK::UFunction* Function = nullptr;
		void* Params = nullptr;
	};

	struct RuntimeState
	{
		std::atomic<bool> IsLoading{ false };
		std::atomic<bool> IsInGame{ false };
		std::atomic<bool> IsCinematicMode{ false };

		bool ShouldSuspendOverlayRendering() const
		{
			return IsCinematicMode.load(std::memory_order_relaxed);
		}

		bool CanRunGameplay() const
		{
			return IsInGame.load(std::memory_order_relaxed) && !IsLoading.load(std::memory_order_relaxed);
		}

		bool CanRenderOverlay() const
		{
			return !ShouldSuspendOverlayRendering();
		}
	};

	class Scheduler
	{
	public:
		using CallbackFn = std::function<void()>;
		using EventHandlerFn = std::function<bool(const SchedulerGameEvent&)>;

		template <typename Handler>
		static void RegisterHandler(std::unordered_map<std::string, Handler>& registry, const std::string& name, Handler handler)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			registry[name] = std::move(handler);
		}

		template <typename Handler>
		static void RemoveHandler(std::unordered_map<std::string, Handler>& registry, const std::string& name)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			registry.erase(name);
		}

		static void RegisterGameUpdateCallback(const std::string& name, CallbackFn callback);
		static void RegisterGameTickCallback(const std::string& name, CallbackFn callback);
		static void RegisterGameplayTickCallback(const std::string& name, CallbackFn callback);
		static void RegisterRenderCallback(const std::string& name, CallbackFn callback);
		static void RegisterOverlayRenderCallback(const std::string& name, CallbackFn callback);
		static void RegisterEventHandler(const std::string& name, EventHandlerFn handler);
		
		static void RemoveGameUpdateCallback(const std::string& name);
		static void RemoveGameTickCallback(const std::string& name);
		static void RemoveRenderCallback(const std::string& name);
		static void RemoveEventHandler(const std::string& name);

		static void OnGameUpdate();
		static void OnRenderFrame();
		static bool DispatchEvent(const SchedulerGameEvent& event);
		static RuntimeState& State();

	private:
		using CallbackRegistry = std::unordered_map<std::string, CallbackFn>;
		using EventRegistry = std::unordered_map<std::string, EventHandlerFn>;
		friend RuntimeState& GetRuntimeState();

		static void ExecuteCallbacks(const CallbackRegistry& registry);

		static inline CallbackRegistry m_GameUpdateCallbacks;
		static inline CallbackRegistry m_RenderCallbacks;
		static inline EventRegistry m_EventHandlers;
		static inline RuntimeState m_RuntimeState;
		static inline std::mutex m_Mutex;
	};

	RuntimeState& GetRuntimeState();
}
