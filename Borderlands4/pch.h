#pragma once
#ifndef PCH_H
#define PCH_H

// Windows and Graphics
#include <Windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>

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
#include "Utils/Localization.h"
#include "Utils/Utils.h"
#include "Cheats.h"

#endif //PCH_H
