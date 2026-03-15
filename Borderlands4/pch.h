#pragma once
#ifndef PCH_H
#define PCH_H

#define _CRT_SECURE_NO_WARNINGS

// Windows and Graphics
#include <Windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <tlhelp32.h>
#include <winternl.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

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
#include <unordered_set>
#include <map>
#include <functional>
#include <iomanip>
#include <sstream>
#include <cstdarg>

// MinHook
#include "minhook/include/MinHook.h"

// ImGui Headers
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"
#include <imgui_internal.h>

// SDK Headers
#include "Borderlands4_SDK/SDK/Engine_classes.hpp"
#include "Borderlands4_SDK/SDK/CoreUObject_classes.hpp"
#include "Borderlands4_SDK/SDK/CoreUObject_parameters.hpp"
#include "Borderlands4_SDK/SDK/Engine_parameters.hpp"
#include "Borderlands4_SDK/SDK/OakGame_classes.hpp"
#include "Borderlands4_SDK/SDK/GbxGame_classes.hpp"
#include "Borderlands4_SDK/SDK/GbxAI_classes.hpp"
#include "Borderlands4_SDK/SDK/AIModule_classes.hpp"
#include "Borderlands4_SDK/SDK/Basic.hpp"

using namespace SDK;

// Project Core Headers
#include "Utils/Logger.h"
#include "Config/ConfigManager.h"
#include "Utils/Localization.h"
#include "Utils/Hotkey.h"
#include "Utils/Memory.h"
#include "Utils/Utils.h"
#include "Engine.h"
#include "Cheats.h"
#include "GUI/Menu.h"
#include "Hooks/D3D12Hook.h"
#include "Hooks/EngineHooks.h"

#endif //PCH_H
