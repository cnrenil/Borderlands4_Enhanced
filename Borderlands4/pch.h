#pragma once
#ifndef PCH_H
#define PCH_H

#define _CRT_SECURE_NO_WARNINGS
#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef BL4_DEBUG_BUILD
#error BL4_DEBUG_BUILD must be defined by the project configuration.
#endif

// Windows and Graphics
#include <Windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <Psapi.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Psapi.lib")

// Standard Library
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <atomic>
#include <fstream>
#include <unordered_map>
#include <numbers>
#include <mutex>
#include <cmath>
#include <algorithm>
#include <array>
#include <unordered_set>
#include <map>
#include <functional>
#include <iomanip>
#include <sstream>
#include <cstdarg>

// MinHook
#include "ThirdParty/MinHook/include/MinHook.h"

// ImGui Headers
#include "ThirdParty/ImGui/imgui.h"
#include "ThirdParty/ImGui/misc/cpp/imgui_stdlib.h"
#include "ThirdParty/ImGui/backends/imgui_impl_win32.h"
#include "ThirdParty/ImGui/backends/imgui_impl_dx12.h"
#include "ThirdParty/ImGui/imgui_internal.h"

// SDK Headers
#include "ThirdParty/SDK/SDK.hpp"
#include "ThirdParty/SDK/SDK/Engine_classes.hpp"
#include "ThirdParty/SDK/SDK/CoreUObject_classes.hpp"
#include "ThirdParty/SDK/SDK/CoreUObject_parameters.hpp"
#include "ThirdParty/SDK/SDK/Engine_parameters.hpp"
#include "ThirdParty/SDK/SDK/OakGame_classes.hpp"
#include "ThirdParty/SDK/SDK/OakGame_parameters.hpp"
#include "ThirdParty/SDK/SDK/GbxGame_classes.hpp"
#include "ThirdParty/SDK/SDK/GbxGame_parameters.hpp"
#include "ThirdParty/SDK/SDK/GbxAI_classes.hpp"
#include "ThirdParty/SDK/SDK/AIModule_classes.hpp"
#include "ThirdParty/SDK/SDK/TemplateSequence_classes.hpp"
#include "ThirdParty/SDK/SDK/Basic.hpp"
extern "C" {
#include "ThirdParty/MinHook/src/hde/hde64.h"
}

using namespace SDK;

// Project Core Headers
#include "Core/Services/Logger/Logger.h"
#include "Core/Config/ConfigManager.h"
#include "Core/Services/Localization/Localization.h"
#include "Core/Services/Hotkey/Hotkey.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/SignatureRegistry.h"
#include "Core/Security/AntiDebug.h"
#include "Core/Services/Interop/NativeInterop.h"
#include "Core/Framework/Variables.h"
#include "Core/Framework/Utils.h"
#include "Core/Hooks/StealthHook.h"
#include "Core/Framework/Scheduler.h"

// Features
#include "Features/Data/FeatureData.h"
#include "Features/Core/FeatureRegistry.h"
#include "Features/Aimbot/AimbotFeature.h"
#include "Features/Camera/CameraFeature.h"
#include "Features/Debug/DebugFeature.h"
#include "Features/Player/PlayerFeature.h"
#include "Features/Movement/MovementFeature.h"
#include "Features/Weapon/WeaponFeature.h"
#include "Features/World/WorldFeature.h"

// Interface
#include "Interface/Drawing/Draw.h"
#include "Interface/Menu/Menu.h"
#include "Interface/Overlay/BackdropBlur.h"
#include "Interface/Overlay/Overlay.h"
#include "Interface/Theme/Themes.h"

// Hooks
#include "Core/Hooks/D3D12Hook.h"
#include "Core/Hooks/EngineHooks.h"

#endif //PCH_H
