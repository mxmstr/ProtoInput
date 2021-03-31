#pragma once
#include "Hook.h"
#include "InstallHooks.h"

namespace Proto
{


class ClipCursorHook final : public Hook
{
private:
	HookInfo hookInfo{};

public:
	const char* GetHookName() const override { return "Clip Cursor"; }
	const char* GetHookDescription() const override
	{
		return "Prevents the game from setting a cursor clipping, which restricts the real cursor to an area. "
				"Without this, some games will repeatedly clip the cursor when they receive fake focus, which can be an inconvenience when testing. ";
	}
	bool HasGuiStatus() const override { return false; }
	void InstallImpl() override;
	void UninstallImpl() override;
};

}