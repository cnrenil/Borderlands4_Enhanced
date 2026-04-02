#pragma once

namespace SDK
{
	struct FVector;
}

namespace NativeInterop
{
	bool ReadPointerNoexcept(uintptr_t address, uintptr_t& outValue);
	bool ReadInt32Noexcept(uintptr_t address, int32_t& outValue);
	bool ReadFloatNoexcept(uintptr_t address, float& outValue);
	bool ReadDoubleNoexcept(uintptr_t address, double& outValue);
	bool WriteFloatNoexcept(uintptr_t address, float value);
	bool WriteDoubleNoexcept(uintptr_t address, double value);

	bool ReadVec3Param(const void* param, SDK::FVector& out);
	void WriteVec3Param(void* param, const SDK::FVector& value);
	bool RedirectDirectionFromSource(
		const SDK::FVector& source,
		const SDK::FVector& target,
		void* dirOut,
		SDK::FVector* outDir = nullptr);
}
