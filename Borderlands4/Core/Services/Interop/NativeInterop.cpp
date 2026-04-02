#include "pch.h"
#include "NativeInterop.h"

namespace
{
	struct NativeVec3
	{
		double X;
		double Y;
		double Z;
	};
}

namespace NativeInterop
{
	bool ReadPointerNoexcept(uintptr_t address, uintptr_t& outValue)
	{
		if (!address) return false;
		__try
		{
			outValue = *reinterpret_cast<uintptr_t*>(address);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			outValue = 0;
			return false;
		}
	}

	bool ReadInt32Noexcept(uintptr_t address, int32_t& outValue)
	{
		if (!address) return false;
		__try
		{
			outValue = *reinterpret_cast<int32_t*>(address);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			outValue = 0;
			return false;
		}
	}

	bool ReadFloatNoexcept(uintptr_t address, float& outValue)
	{
		if (!address) return false;
		__try
		{
			outValue = *reinterpret_cast<float*>(address);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			outValue = 0.0f;
			return false;
		}
	}

	bool ReadDoubleNoexcept(uintptr_t address, double& outValue)
	{
		if (!address) return false;
		__try
		{
			outValue = *reinterpret_cast<double*>(address);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			outValue = 0.0;
			return false;
		}
	}

	bool WriteFloatNoexcept(uintptr_t address, float value)
	{
		if (!address) return false;
		__try
		{
			*reinterpret_cast<float*>(address) = value;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool WriteDoubleNoexcept(uintptr_t address, double value)
	{
		if (!address) return false;
		__try
		{
			*reinterpret_cast<double*>(address) = value;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool ReadVec3Param(const void* param, SDK::FVector& out)
	{
		if (!param) return false;
		__try
		{
			const NativeVec3* v = reinterpret_cast<const NativeVec3*>(param);
			if (!std::isfinite(v->X) || !std::isfinite(v->Y) || !std::isfinite(v->Z)) return false;
			out = SDK::FVector{ v->X, v->Y, v->Z };
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	void WriteVec3Param(void* param, const SDK::FVector& value)
	{
		if (!param) return;
		__try
		{
			NativeVec3* v = reinterpret_cast<NativeVec3*>(param);
			v->X = value.X;
			v->Y = value.Y;
			v->Z = value.Z;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}

	bool RedirectDirectionFromSource(const SDK::FVector& source, const SDK::FVector& target, void* dirOut, SDK::FVector* outDir)
	{
		const SDK::FVector toTarget = target - source;
		const float lenSq = static_cast<float>(toTarget.X * toTarget.X + toTarget.Y * toTarget.Y + toTarget.Z * toTarget.Z);
		if (lenSq <= 0.0001f) return false;

		const float len = sqrtf(lenSq);
		const SDK::FVector dir{ toTarget.X / len, toTarget.Y / len, toTarget.Z / len };
		WriteVec3Param(dirOut, dir);
		if (outDir)
		{
			*outDir = dir;
		}
		return true;
	}
}
