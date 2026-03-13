#include "pch.h"


void Cheats::DumpObjects()
{
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	std::string dir = std::string(path).substr(0, std::string(path).find_last_of("\\/"));
	std::string logPath = dir + "\\GObjectsDump.txt";

	std::ofstream file(logPath);
	if (!file.is_open()) {
		LOG_ERROR("Dump", "Failed to open file for dumping: %s", logPath.c_str());
		return;
	}

	LOG_INFO("Dump", "Dumping all GObjects to %s...", logPath.c_str());

	int32_t count = 0;
	auto& GObjects = SDK::UObject::GObjects;

	for (int32_t i = 0; i < GObjects->Num(); i++)
	{
		SDK::UObject* Obj = GObjects->GetByIndex(i);
		if (!Obj) continue;

		try {
			std::string FullName = Obj->GetFullName();
			file << "[" << i << "] " << (void*)Obj << " | " << FullName << "\n";
			count++;
		}
		catch (...) {
		}
	}
	file.close();
	LOG_INFO("Dump", "Finished dumping %d objects.", count);
}

bool Cheats::HandleDebugEvents(const SDK::UObject* Object, SDK::UFunction* Function, void* Params)
{
    if (Logger::IsRecording())
    {
        const std::string FuncName = Function->GetName();
        const std::string ClassName = Object->Class ? Object->Class->GetName() : "None";
        const std::string ObjName = Object->GetName();

        // Filter out noise
        if (ClassName.find("Widget") == std::string::npos && ClassName.find("Menu") == std::string::npos)
        {
            Logger::LogEvent(ClassName, FuncName, ObjName);
        }
    }

    return false; // Return true if we want to skip oProcessEvent
}
