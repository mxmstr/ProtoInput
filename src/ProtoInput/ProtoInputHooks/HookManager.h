#pragma once
#include <vector>
#include "Hook.h"
#include "MessageBoxHook.h"

namespace Proto
{

//TODO: move to an include header so can be called externally
enum ProtoHookIDs : unsigned int
{
	MessageBoxHookID = 0
};


class HookManager
{
private:
	std::vector<std::unique_ptr<Hook>> hooks{};

	static HookManager hookManagerInstance;
	
	template<typename T>
	void AddHook(ProtoHookIDs index)
	{
		if (hooks.size() != index)
			std::cerr << "Trying to call AddHook with an invalid ID" << std::endl;
		else
		{
			hooks.push_back(std::make_unique<T>());
		}
	}
	
	HookManager();
	
public:
	static const std::vector<std::unique_ptr<Hook>>& GetHooks()
	{
		return hookManagerInstance.hooks;
	}

	static void InstallHook(ProtoHookIDs hookID);
	static void UninstallHook(ProtoHookIDs hookID);
	static bool IsInstalled(ProtoHookIDs hookID);
};

}
