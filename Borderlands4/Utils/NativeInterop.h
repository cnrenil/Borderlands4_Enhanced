#pragma once

namespace SDK
{
	struct FVector;
}

namespace NativeInterop
{
	bool ReadVec3Param(const void* param, SDK::FVector& out);
	void WriteVec3Param(void* param, const SDK::FVector& value);
	bool RedirectDirectionFromSource(
		const SDK::FVector& source,
		const SDK::FVector& target,
		void* dirOut,
		SDK::FVector* outDir = nullptr);
}
