#pragma once

namespace Features::Camera
{
	void ToggleThirdPerson();
	void ToggleFreecam();
	void SetFOV(float fov);
	void Update();
	void Shutdown();
	bool ShouldTraceNative();
	void ApplyNativePostUpdate(uintptr_t cameraContext, float deltaSeconds);
}
