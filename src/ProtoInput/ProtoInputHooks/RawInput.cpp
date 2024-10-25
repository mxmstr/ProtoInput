#include "RawInput.h"
#include "Gui.h"
#include <cassert>
#include <windows.h>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <format>
#include <string>
#include <coroutine>
#include "HookManager.h"
#include "FakeMouseKeyboard.h"
#include "HwndSelector.h"
#include "MouseWheelFilter.h"
#include "MouseButtonFilter.h"
#include "StateInfo.h"
#include "FakeCursor.h"
#include "FocusHook.h"
#include "protoinpututil.h"
#include "KeyboardButtonFilter.h"
#include "MessageFilterHook.h"

namespace Proto
{

RawInputState RawInput::rawInputState{};
std::bitset<9> RawInput::usages{};
std::vector<HWND> RawInput::forwardingWindows{};
bool RawInput::forwardRawInput = true;
bool RawInput::lockInputToggleEnabled = false;
bool RawInput::rawInputBypass = false;
RAWINPUT RawInput::inputBuffer[RawInputBufferSize]{};
std::vector<RAWINPUT> RawInput::rawinputs{};

static std::vector<JoyToKeyBind> joyToKeyBinds{};
static std::vector<JoyToMouseBind> joyToMouseBinds{};
static JoyToKeyBind toggleContextBind{};
static bool joyToKeyEnabled = true;
static int controllerIndex = 0;
static int defaultContext = 0;
static int autoSwitchContext = 0;
static int thumbstickToMouseDelta = 0;
static float mouseSpeed = 1.0f;
static float mouseAcceleration = 1.0f;
static float mouseSmoothing = 0.0f;

const std::vector<USAGE> RawInput::usageTypesOfInterest
{
		HID_USAGE_GENERIC_POINTER,
		HID_USAGE_GENERIC_MOUSE,
		//HID_USAGE_GENERIC_JOYSTICK,
		//HID_USAGE_GENERIC_GAMEPAD,
		HID_USAGE_GENERIC_KEYBOARD,
		HID_USAGE_GENERIC_KEYPAD,
		//HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER
};
const std::unordered_map<std::string, USHORT> xinputvkeys = {
	{"A", VK_PAD_A},
	{"B", VK_PAD_B},
	{"X", VK_PAD_X},
	{"Y", VK_PAD_Y},
	{"RSHOULDER", VK_PAD_RSHOULDER},
	{"LSHOULDER", VK_PAD_LSHOULDER},
	{"LTRIGGER", VK_PAD_LTRIGGER},
	{"RTRIGGER", VK_PAD_RTRIGGER},
	{"DPAD_UP", VK_PAD_DPAD_UP},
	{"DPAD_DOWN", VK_PAD_DPAD_DOWN},
	{"DPAD_LEFT", VK_PAD_DPAD_LEFT},
	{"DPAD_RIGHT", VK_PAD_DPAD_RIGHT},
	{"START", VK_PAD_START},
	{"BACK", VK_PAD_BACK},
	{"LTHUMB_PRESS", VK_PAD_LTHUMB_PRESS},
	{"RTHUMB_PRESS", VK_PAD_RTHUMB_PRESS},
	{"LTHUMB_UP", VK_PAD_LTHUMB_UP},
	{"LTHUMB_DOWN", VK_PAD_LTHUMB_DOWN},
	{"LTHUMB_RIGHT", VK_PAD_LTHUMB_RIGHT},
	{"LTHUMB_LEFT", VK_PAD_LTHUMB_LEFT},
	{"LTHUMB_UPLEFT", VK_PAD_LTHUMB_UPLEFT},
	{"LTHUMB_UPRIGHT", VK_PAD_LTHUMB_UPRIGHT},
	{"LTHUMB_DOWNRIGHT", VK_PAD_LTHUMB_DOWNRIGHT},
	{"LTHUMB_DOWNLEFT", VK_PAD_LTHUMB_DOWNLEFT},
	{"RTHUMB_UP", VK_PAD_RTHUMB_UP},
	{"RTHUMB_DOWN", VK_PAD_RTHUMB_DOWN},
	{"RTHUMB_RIGHT", VK_PAD_RTHUMB_RIGHT},
	{"RTHUMB_LEFT", VK_PAD_RTHUMB_LEFT},
	{"RTHUMB_UPLEFT", VK_PAD_RTHUMB_UPLEFT},
	{"RTHUMB_UPRIGHT", VK_PAD_RTHUMB_UPRIGHT},
	{"RTHUMB_DOWNRIGHT", VK_PAD_RTHUMB_DOWNRIGHT},
	{"RTHUMB_DOWNLEFT", VK_PAD_RTHUMB_DOWNLEFT}
};
const std::unordered_map<USHORT, USHORT> xinputwbuttons = {
	{ VK_PAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_UP },
	{ VK_PAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_DOWN },
	{ VK_PAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_LEFT },
	{ VK_PAD_DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_RIGHT },
	{ VK_PAD_START, XINPUT_GAMEPAD_START },
	{ VK_PAD_BACK, XINPUT_GAMEPAD_BACK },
	{ VK_PAD_LTHUMB_PRESS, XINPUT_GAMEPAD_LEFT_THUMB },
	{ VK_PAD_RTHUMB_PRESS, XINPUT_GAMEPAD_RIGHT_THUMB },
	{ VK_PAD_LSHOULDER, XINPUT_GAMEPAD_LEFT_SHOULDER },
	{ VK_PAD_RSHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER },
	{ VK_PAD_A, XINPUT_GAMEPAD_A },
	{ VK_PAD_B, XINPUT_GAMEPAD_B },
	{ VK_PAD_X, XINPUT_GAMEPAD_X },
	{ VK_PAD_Y, XINPUT_GAMEPAD_Y }
};
const std::unordered_map<std::string, USHORT> vkeys = {
	{"LBUTTON", VK_LBUTTON},
	{"RBUTTON", VK_RBUTTON},
	{"CANCEL", VK_CANCEL},
	{"MBUTTON", VK_MBUTTON},
	{"XBUTTON1", VK_XBUTTON1},
	{"XBUTTON2", VK_XBUTTON2},
	{"BACK", VK_BACK},
	{"TAB", VK_TAB},
	{"CLEAR", VK_CLEAR},
	{"RETURN", VK_RETURN},
	{"SHIFT", VK_SHIFT},
	{"CONTROL", VK_CONTROL},
	{"MENU", VK_MENU},
	{"PAUSE", VK_PAUSE},
	{"CAPITAL", VK_CAPITAL},
	{"KANA", VK_KANA},
	{"HANGUL", VK_HANGUL},
	{"JUNJA", VK_JUNJA},
	{"FINAL", VK_FINAL},
	{"HANJA", VK_HANJA},
	{"KANJI", VK_KANJI},
	{"ESCAPE", VK_ESCAPE},
	{"CONVERT", VK_CONVERT},
	{"NONCONVERT", VK_NONCONVERT},
	{"ACCEPT", VK_ACCEPT},
	{"MODECHANGE", VK_MODECHANGE},
	{"SPACE", VK_SPACE},
	{"PRIOR", VK_PRIOR},
	{"NEXT", VK_NEXT},
	{"END", VK_END},
	{"HOME", VK_HOME},
	{"LEFT", VK_LEFT},
	{"UP", VK_UP},
	{"RIGHT", VK_RIGHT},
	{"DOWN", VK_DOWN},
	{"SELECT", VK_SELECT},
	{"PRINT", VK_PRINT},
	{"EXECUTE", VK_EXECUTE},
	{"SNAPSHOT", VK_SNAPSHOT},
	{"INSERT", VK_INSERT},
	{"DELETE", VK_DELETE},
	{"HELP", VK_HELP},
	{"0", 0x30},
	{"1", 0x31},
	{"2", 0x32},
	{"3", 0x33},
	{"4", 0x34},
	{"5", 0x35},
	{"6", 0x36},
	{"7", 0x37},
	{"8", 0x38},
	{"9", 0x39},
	{"A", 0x41},
	{"B", 0x42},
	{"C", 0x43},
	{"D", 0x44},
	{"E", 0x45},
	{"F", 0x46},
	{"G", 0x47},
	{"H", 0x48},
	{"I", 0x49},
	{"J", 0x4A},
	{"K", 0x4B},
	{"L", 0x4C},
	{"M", 0x4D},
	{"N", 0x4E},
	{"O", 0x4F},
	{"P", 0x50},
	{"Q", 0x51},
	{"R", 0x52},
	{"S", 0x53},
	{"T", 0x54},
	{"U", 0x55},
	{"V", 0x56},
	{"W", 0x57},
	{"X", 0x58},
	{"Y", 0x59},
	{"Z", 0x5A},
	{"LWIN", VK_LWIN},
	{"RWIN", VK_RWIN},
	{"APPS", VK_APPS},
	{"SLEEP", VK_SLEEP},
	{"NUMPAD0", VK_NUMPAD0},
	{"NUMPAD1", VK_NUMPAD1},
	{"NUMPAD2", VK_NUMPAD2},
	{"NUMPAD3", VK_NUMPAD3},
	{"NUMPAD4", VK_NUMPAD4},
	{"NUMPAD5", VK_NUMPAD5},
	{"NUMPAD6", VK_NUMPAD6},
	{"NUMPAD7", VK_NUMPAD7},
	{"NUMPAD8", VK_NUMPAD8},
	{"NUMPAD9", VK_NUMPAD9},
	{"MULTIPLY", VK_MULTIPLY},
	{"ADD", VK_ADD},
	{"SEPARATOR", VK_SEPARATOR},
	{"SUBTRACT", VK_SUBTRACT},
	{"DECIMAL", VK_DECIMAL},
	{"DIVIDE", VK_DIVIDE},
	{"F1", VK_F1},
	{"F2", VK_F2},
	{"F3", VK_F3},
	{"F4", VK_F4},
	{"F5", VK_F5},
	{"F6", VK_F6},
	{"F7", VK_F7},
	{"F8", VK_F8},
	{"F9", VK_F9},
	{"F10", VK_F10},
	{"F11", VK_F11},
	{"F12", VK_F12},
	{"F13", VK_F13},
	{"F14", VK_F14},
	{"F15", VK_F15},
	{"F16", VK_F16},
	{"F17", VK_F17},
	{"F18", VK_F18},
	{"F19", VK_F19},
	{"F20", VK_F20},
	{ "F21", VK_F21 },
	{ "F22", VK_F22 },
	{ "F23", VK_F23 },
	{ "F24", VK_F24 },
	{ "NUMLOCK", VK_NUMLOCK },
	{ "SCROLL", VK_SCROLL },
	{ "LSHIFT", VK_LSHIFT },
	{ "RSHIFT", VK_RSHIFT },
	{ "LCONTROL", VK_LCONTROL },
	{ "RCONTROL", VK_RCONTROL },
	{ "LMENU", VK_LMENU },
	{ "RMENU", VK_RMENU },
	{ "BROWSER_BACK", VK_BROWSER_BACK },
	{ "BROWSER_FORWARD", VK_BROWSER_FORWARD },
	{ "BROWSER_REFRESH", VK_BROWSER_REFRESH },
	{ "BROWSER_STOP", VK_BROWSER_STOP },
	{ "BROWSER_SEARCH", VK_BROWSER_SEARCH },
	{ "BROWSER_FAVORITES", VK_BROWSER_FAVORITES },
	{ "BROWSER_HOME", VK_BROWSER_HOME },
	{ "VOLUME_MUTE", VK_VOLUME_MUTE },
	{ "VOLUME_DOWN", VK_VOLUME_DOWN },
	{ "VOLUME_UP", VK_VOLUME_UP },
	{ "MEDIA_NEXT_TRACK", VK_MEDIA_NEXT_TRACK },
	{ "MEDIA_PREV_TRACK", VK_MEDIA_PREV_TRACK },
	{ "MEDIA_STOP", VK_MEDIA_STOP },
	{ "MEDIA_PLAY_PAUSE", VK_MEDIA_PLAY_PAUSE },
	{ "LAUNCH_MAIL", VK_LAUNCH_MAIL },
	{ "LAUNCH_MEDIA_SELECT", VK_LAUNCH_MEDIA_SELECT },
	{ "LAUNCH_APP1", VK_LAUNCH_APP1 },
	{ "LAUNCH_APP2", VK_LAUNCH_APP2 },
	{ "OEM_1", VK_OEM_1 },
	{ "OEM_PLUS", VK_OEM_PLUS },
	{ "OEM_COMMA", VK_OEM_COMMA },
	{ "OEM_MINUS", VK_OEM_MINUS },
	{ "OEM_PERIOD", VK_OEM_PERIOD },
	{ "OEM_2", VK_OEM_2 },
	{ "OEM_3", VK_OEM_3 },
	{ "OEM_4", VK_OEM_4 },
	{ "OEM_5", VK_OEM_5 },
	{ "OEM_6", VK_OEM_6 },
	{ "OEM_7", VK_OEM_7 },
	{ "OEM_8", VK_OEM_8 },
	{ "OEM_102", VK_OEM_102 },
	{ "PROCESSKEY", VK_PROCESSKEY },
	{ "PACKET", VK_PACKET },
	{ "ATTN", VK_ATTN },
	{ "CRSEL", VK_CRSEL },
	{ "EXSEL", VK_EXSEL },
	{ "EREOF", VK_EREOF },
	{ "PLAY", VK_PLAY },
	{ "ZOOM", VK_ZOOM },
	{ "NONAME", VK_NONAME },
	{ "PA1", VK_PA1 },
	{ "OEM_CLEAR", VK_OEM_CLEAR }
};

HWND RawInput::rawInputHwnd = nullptr;

std::vector<std::string> split(std::string& s, const std::string& delimiter) {
	std::vector<std::string> tokens;
	size_t pos = 0;
	std::string token;
	while ((pos = s.find(delimiter)) != std::string::npos) {
		token = s.substr(0, pos);
		tokens.push_back(token);
		s.erase(0, pos + delimiter.length());
	}
	tokens.push_back(s);

	return tokens;
}

void RawInput::ProcessMouseInput(const RAWMOUSE& data, HANDLE deviceHandle)
{	
	// Update fake mouse position
	if ((data.usFlags & MOUSE_MOVE_ABSOLUTE) == MOUSE_MOVE_ABSOLUTE)
	{
		const bool isVirtualDesktop = (data.usFlags & MOUSE_VIRTUAL_DESKTOP) == MOUSE_VIRTUAL_DESKTOP;

		// const int width = GetSystemMetrics(isVirtualDesktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
		// const int height = GetSystemMetrics(isVirtualDesktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);

		static int widthVirtual = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		static int widthNonVirtual = GetSystemMetrics(SM_CXSCREEN);
		static int heightVirtual = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		static int heightNonVirtual = GetSystemMetrics(SM_CYSCREEN);
		
		const int absoluteX = int((data.lLastX / 65535.0f) * (isVirtualDesktop ? widthVirtual : widthNonVirtual));
		const int absoluteY = int((data.lLastY / 65535.0f) * (isVirtualDesktop ? heightVirtual : heightNonVirtual));

		static std::unordered_map<HANDLE, std::pair<int, int>> oldPositions{};
		
		if (const auto find = oldPositions.find(deviceHandle); find != oldPositions.end())
		{
			FakeMouseKeyboard::AddMouseDelta(absoluteX - find->second.first, absoluteY - find->second.second);
		}
		else
		{
			oldPositions.emplace(std::make_pair( deviceHandle, std::pair<int, int>{ absoluteX, absoluteY } ));
		}		
	}
	else if (data.lLastX != 0 || data.lLastY != 0)
	{
		const int relativeX = data.lLastX;
		const int relativeY = data.lLastY;
		FakeMouseKeyboard::AddMouseDelta(relativeX, relativeY);
	}

	// Set vkeys (GetKeyState/etc can be used to get the mouse buttons state)
	if ((data.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) != 0)
		FakeMouseKeyboard::ReceivedKeyPressOrRelease(VK_LBUTTON, true);
	if ((data.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP) != 0)
		FakeMouseKeyboard::ReceivedKeyPressOrRelease(VK_LBUTTON, false);

	if ((data.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN) != 0)
		FakeMouseKeyboard::ReceivedKeyPressOrRelease(VK_MBUTTON, true);
	if ((data.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP) != 0)
		FakeMouseKeyboard::ReceivedKeyPressOrRelease(VK_MBUTTON, false);

	if ((data.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) != 0)
		FakeMouseKeyboard::ReceivedKeyPressOrRelease(VK_RBUTTON, true);
	if ((data.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP) != 0)
		FakeMouseKeyboard::ReceivedKeyPressOrRelease(VK_RBUTTON, false);

	if ((data.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN) != 0)
		FakeMouseKeyboard::ReceivedKeyPressOrRelease(VK_XBUTTON1, true);
	if ((data.usButtonFlags & RI_MOUSE_BUTTON_4_UP) != 0)
		FakeMouseKeyboard::ReceivedKeyPressOrRelease(VK_XBUTTON1, false);

	if ((data.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN) != 0)
		FakeMouseKeyboard::ReceivedKeyPressOrRelease(VK_XBUTTON2, true);
	if ((data.usButtonFlags & RI_MOUSE_BUTTON_5_UP) != 0)
		FakeMouseKeyboard::ReceivedKeyPressOrRelease(VK_XBUTTON2, false);


	// This is used a lot in sending messages
	const unsigned int mouseMkFlags = FakeMouseKeyboard::GetMouseMkFlags();
	const unsigned int mousePointLparam = MAKELPARAM(FakeMouseKeyboard::GetMouseState().x, FakeMouseKeyboard::GetMouseState().y);
	
	
	// Send mouse wheel
	if (rawInputState.sendMouseWheelMessages)
	{
		if((data.usButtonFlags & RI_MOUSE_WHEEL) != 0)
		{
			const unsigned int wparam = (data.usButtonData << 16)
				| MouseWheelFilter::protoInputSignature
				| mouseMkFlags;
						
			PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_MOUSEWHEEL, wparam, mousePointLparam);

		}
	}


	// Send mouse button messages
	if (rawInputState.sendMouseButtonMessages)
	{		
		if ((data.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) != 0)
			PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_LBUTTONDOWN, mouseMkFlags | MouseButtonFilter::signature, mousePointLparam);
		if ((data.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP) != 0)
			PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_LBUTTONUP, mouseMkFlags | MouseButtonFilter::signature, mousePointLparam);

		if ((data.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN) != 0)
			PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_MBUTTONDOWN, mouseMkFlags | MouseButtonFilter::signature, mousePointLparam);
		if ((data.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP) != 0)
			PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_MBUTTONUP, mouseMkFlags | MouseButtonFilter::signature, mousePointLparam);

		if ((data.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) != 0)
			PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_RBUTTONDOWN, mouseMkFlags | MouseButtonFilter::signature, mousePointLparam);
		if ((data.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP) != 0)
			PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_RBUTTONUP, mouseMkFlags | MouseButtonFilter::signature, mousePointLparam);

		if ((data.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN) != 0)
			PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_XBUTTONDOWN, mouseMkFlags | (XBUTTON1 << 4) | MouseButtonFilter::signature, mousePointLparam);
		if ((data.usButtonFlags & RI_MOUSE_BUTTON_4_UP) != 0)
			PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_XBUTTONUP, mouseMkFlags | (XBUTTON1 << 4) | MouseButtonFilter::signature, mousePointLparam);

		if ((data.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN) != 0)
			PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_XBUTTONDOWN, mouseMkFlags | (XBUTTON2 << 4) | MouseButtonFilter::signature, mousePointLparam);
		if ((data.usButtonFlags & RI_MOUSE_BUTTON_5_UP) != 0)
			PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_XBUTTONUP, mouseMkFlags | (XBUTTON2 << 4) | MouseButtonFilter::signature, mousePointLparam);
	}



	// WM_MOUSEMOVE
	if (rawInputState.sendMouseMoveMessages)
	{
		PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_MOUSEMOVE, mouseMkFlags, mousePointLparam);
	}



	// Fake cursor
	FakeCursor::NotifyUpdatedCursorPosition();
}

void RawInput::ProcessKeyboardInput(const RAWKEYBOARD& data, HANDLE deviceHandle)
{
	const bool released = (data.Flags & RI_KEY_BREAK) != 0;
	const bool pressed = !released;

	if (pressed && FakeCursor::GetToggleVisilbityShorcutEnabled() &&  data.VKey == FakeCursor::GetToggleVisibilityVkey())
	{
		FakeCursor::SetCursorVisibility(!FakeCursor::GetCursorVisibility());
		return;
	}
	
	if (rawInputState.sendKeyboardPressMessages)
	{
		if (pressed)
		{
			unsigned int lparam = 0;
			
			lparam |= 1; // Repeat bit
			lparam |= (data.MakeCode << 16); // Scan code
			
			if (FakeMouseKeyboard::IsKeyStatePressed(data.VKey))
			{
				lparam |= (1 << 30);
			}
			
			PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_KEYDOWN, 
				MessageFilterHook::IsKeyboardButtonFilterEnabled() ? data.VKey | KeyboardButtonFilter::signature : data.VKey, 
				lparam);

			// if (data.VKey == VK_SHIFT || data.VKey == VK_LSHIFT)
			// {
			// 	PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_KEYDOWN, VK_SHIFT, lparam);
			// 	PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_KEYDOWN, VK_LSHIFT, lparam);
			// }
			// else
			// {
			// 	PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_KEYDOWN, data.VKey, lparam);
			// }
		}
		else if (released)
		{
			unsigned int lparam = 0;
			lparam |= 1; // Repeat count (always 1 for key up)
			lparam |= (data.MakeCode << 16); // Scan code
			lparam |= (1 << 30); // Previous key state (always 1 for key up)
			lparam |= (1 << 31); // Transition state (always 1 for key up)

			PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_KEYUP, 
				MessageFilterHook::IsKeyboardButtonFilterEnabled() ? data.VKey | KeyboardButtonFilter::signature : data.VKey,
				lparam);

			// if (data.VKey == VK_SHIFT || data.VKey == VK_LSHIFT)
			// {
			// 	PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_KEYUP, VK_SHIFT, lparam);
			// 	PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_KEYUP, VK_LSHIFT, lparam);
			// }
			// else
			// {
			// 	PostMessageW((HWND)HwndSelector::GetSelectedHwnd(), WM_KEYUP, data.VKey, lparam);
			// }
			
		}
	}
	
	FakeMouseKeyboard::ReceivedKeyPressOrRelease(data.VKey, pressed);
}

void RawInput::PostRawInputMessage(const RAWINPUT& rawinput)
{
	//TODO: handle forwarding HID input

	if (
		(
			(!rawInputState.externalFreezeInput && !rawInputState.freezeInput && (!rawInputState.freezeInputWhileGuiOpened || !rawInputState.guiOpened))
			||
			rawInputBypass
			)
		&&
		forwardRawInput)
	{
		const bool dataIsMouse = rawinput.header.dwType == RIM_TYPEMOUSE;
		const bool dataIsKeyboard = rawinput.header.dwType == RIM_TYPEKEYBOARD;
		const bool selectedThisMouse = joyToKeyEnabled || std::find(rawInputState.selectedMouseHandles.begin(), rawInputState.selectedMouseHandles.end(), rawinput.header.hDevice) != rawInputState.selectedMouseHandles.end();
		const bool selectedThisKeyboard = joyToKeyEnabled || std::find(rawInputState.selectedKeyboardHandles.begin(), rawInputState.selectedKeyboardHandles.end(), rawinput.header.hDevice) != rawInputState.selectedKeyboardHandles.end();
		const bool allowMouse = dataIsMouse && (rawInputBypass || selectedThisMouse);
		const bool allowKeyboard = dataIsKeyboard && (rawInputBypass || selectedThisKeyboard);

		if (allowMouse || allowKeyboard)
		{
			if (!rawInputBypass)
			{
				if (allowMouse)
					ProcessMouseInput(rawinput.data.mouse, rawinput.header.hDevice);
				else if (allowKeyboard)
					ProcessKeyboardInput(rawinput.data.keyboard, rawinput.header.hDevice);
			}

			if ((allowMouse && usages[HID_USAGE_GENERIC_MOUSE]) || (allowKeyboard && usages[HID_USAGE_GENERIC_KEYBOARD]))
			{
				for (const auto& hwnd : forwardingWindows)
				{
					static size_t inputBufferCounter = 0;

					inputBufferCounter = (inputBufferCounter + 1) % RawInputBufferSize;
					inputBuffer[inputBufferCounter] = rawinput;

					const LPARAM x = (inputBufferCounter) | 0xAB000000;
					BOOL result = PostMessageW(hwnd, WM_INPUT, RIM_INPUT, x);
					//OutputDebugStringA("PROTOINPUT: PostMessage ");
					if (!result)
					{
						OutputDebugStringA("PROTOINPUT: PostMessage failed");
						DWORD error = GetLastError();
						OutputDebugStringA(("PROTOINPUT: Error code: " + std::to_string(error)).c_str());
					}
				}
			}
		}
	}
}

void RawInput::ProcessRawInput(HRAWINPUT rawInputHandle, bool inForeground, const MSG& msg)
{
	// if (rawInputBypass)
	// {
	// 	for (const auto& hwnd : forwardingWindows)
	// 	{
	// 		PostMessageW(hwnd, WM_INPUT, msg.wParam, msg.lParam);
	// 	}
	// 	
	// 	return;
	// }
	
	RAWINPUT rawinput;
	UINT cbSize;

	if (0 != GetRawInputData(rawInputHandle, RID_INPUT, nullptr, &cbSize, sizeof(RAWINPUTHEADER)))
		return;

	// This seems to happen with a PS4 controller plugged in, giving a stack corruption (yay)
	// If we ever need HID input, create a large memory buffer, then reinterpret the pointer to the buffer as a RAWINPUT*
	// (HID input has variable size)
	if (cbSize > sizeof(RAWINPUT))
		return;

	if (cbSize != GetRawInputData(rawInputHandle, RID_INPUT, &rawinput, &cbSize, sizeof(RAWINPUTHEADER)))
		return;

	rawinput.header.wParam = RIM_INPUT; // Sent in the foreground

	const int index = StateInfo::info.instanceIndex;
	
	// Shortcut to open UI (doesn't care about what keyboard is attached)
	if (rawinput.header.dwType == RIM_TYPEKEYBOARD && index >= 1 && index <= 9 && (rawinput.data.keyboard.VKey == 0x30 + index))
	{
		static bool keyDown = false;
		if (rawinput.data.keyboard.Flags == RI_KEY_MAKE && !keyDown)
		{
			keyDown = true;
	
			// Key just pressed
			if ((GetAsyncKeyState(VK_RCONTROL) & ~1) != 0 && (GetAsyncKeyState(VK_RMENU) & ~1) != 0)
			// if ((GetAsyncKeyState(VK_LCONTROL) & ~1) != 0 && (GetAsyncKeyState(VK_LMENU) & ~1) != 0)
			{
				Proto::ToggleWindow();
			}			
		}
	
		if (rawinput.data.keyboard.Flags == RI_KEY_BREAK && keyDown)
		{
			keyDown = false;
		}
	}
	
	// Need to occasionally update the window is case the main window changes (e.g. because of a launcher) or the main window is resized
	if (rawinput.header.dwType == RIM_TYPEMOUSE && (rawinput.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP) != 0)
	{
		//TODO: This may waste CPU? (But need a way to update window otherwise)
		//if (HwndSelector::GetSelectedHwnd() == 0)
			HwndSelector::UpdateMainHwnd(false);
	
		HwndSelector::UpdateWindowBounds();
	}
	
	
	// Lock input toggle
	if (lockInputToggleEnabled && rawinput.header.dwType == RIM_TYPEKEYBOARD && rawinput.data.keyboard.VKey == VK_HOME && rawinput.data.keyboard.Message == WM_KEYUP)
	{
		static bool locked = false;
		locked = !locked;
		printf(locked ? "Locking input\n" : "Unlocking input\n");
	
		// Add the looping thread to the ACL so it can still use ClipCursor, etc
		static unsigned int loopThreadId = 0;
		static bool alreadyAddToACL = false;
		loopThreadId = LockInput(locked);
		if (!alreadyAddToACL && loopThreadId != 0)
		{
			alreadyAddToACL = true;
			printf("Adding loop thread %d to ACL\n", loopThreadId);
			AddThreadToACL(loopThreadId);
		}
		
		if (locked)
			SuspendExplorer();
		else
			RestartExplorer();
	}
	
	PostRawInputMessage(rawinput);

}

LRESULT WINAPI RawInputWindowWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	// case WM_SYSCOMMAND:
	// 	if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
	// 		return 0;
	// 	break;
	case WM_DESTROY:
	{
		printf("Raw input window posting quit message\n");
		PostQuitMessage(0);
		return 0;
	}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void InitJoyToKeyEmulation()
{
	std::string iniFilePath = "";
	char executablePath[MAX_PATH];
	if (GetCurrentDirectoryA(MAX_PATH, executablePath)) {
		iniFilePath = std::string(executablePath) + "\\JoyToKey.ini";
	}

	if (GetFileAttributesA(iniFilePath.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		joyToKeyEnabled = false;
		return;
	}

	char setting[256];
	controllerIndex = GetPrivateProfileIntA("Settings", "ControllerIndex", 0, iniFilePath.c_str());
	defaultContext = (JoyToKeyContext)GetPrivateProfileIntA("Settings", "DefaultContext", 0, iniFilePath.c_str());
	autoSwitchContext = GetPrivateProfileIntA("Settings", "AutoSwitchContext", 0, iniFilePath.c_str());
	thumbstickToMouseDelta = GetPrivateProfileIntA("Settings.Default", "ThumbstickToMouseDelta", 0, iniFilePath.c_str());
	GetPrivateProfileStringA("Settings.Default", "MouseSpeed", "500.0", setting, sizeof(setting), iniFilePath.c_str());
	mouseSpeed = std::stof(setting);
	GetPrivateProfileStringA("Settings.Default", "MouseAcceleration", "1.0", setting, sizeof(setting), iniFilePath.c_str());
	mouseAcceleration = std::stof(setting);
	GetPrivateProfileStringA("Settings.Default", "MouseSmoothing", "1.0", setting, sizeof(setting), iniFilePath.c_str());
	mouseSmoothing = std::stof(setting);

	const char* mouseSectionNames[] = { "MouseBinds.Default", "MouseBinds.Gameplay", "MouseBinds.Menu" };
	const char* keyboardSectionNames[] = { "KeyboardBinds.Default", "KeyboardBinds.Gameplay", "KeyboardBinds.Menu" };

	for (size_t i = 0; i < 3; i++)
	{
		char msection[256];
		DWORD charsRead = GetPrivateProfileSectionA(mouseSectionNames[i], msection, sizeof(msection), iniFilePath.c_str());
		std::string line = "";
		std::vector<std::string> sectionSplit = {};

		for (size_t i = 0; i < charsRead; i++)
		{
			if (msection[i] == '\0')
			{
				sectionSplit.push_back(line);
				line = "";
			}
			else
				line += msection[i];
		}

		for (auto& line : sectionSplit)
		{
			std::vector<std::string> lineSplit = split(line, "=");
			if (lineSplit.size() < 2)
				continue;

			std::string inputKeysStr = lineSplit[0];
			std::string outputValuesStr = lineSplit[1];

			std::vector<std::string> keySplit = split(inputKeysStr, ",");
			std::vector<USHORT> vkeyValues;
			for (auto& vkey : keySplit)
				vkeyValues.push_back(xinputvkeys.at(vkey));

			std::vector<std::string> outputValuesSplit = split(outputValuesStr, ",");
			size_t numValues = outputValuesSplit.size();
			std::string outputMouseEvent = numValues >= 1 ? outputValuesSplit[0] : "";

			JoyToMouseBind bind;
			bind.context = (JoyToKeyContext)i;
			bind.xinputVKeys = vkeyValues;
			bind.outputMouseEvent = outputMouseEvent;
			bind.isToggle = numValues >= 3 ? std::stoi(outputValuesSplit[1]) : 0;

			joyToMouseBinds.push_back(bind);
		}

		char ksection[256];
		charsRead = GetPrivateProfileSectionA(keyboardSectionNames[i], ksection, sizeof(ksection), iniFilePath.c_str());
		line = "";
		sectionSplit = {};

		for (size_t i = 0; i < charsRead; i++)
		{
			if (ksection[i] == '\0')
			{
				sectionSplit.push_back(line);
				line = "";
			}
			else
				line += ksection[i];
		}

		for (auto& line : sectionSplit)
		{
			std::vector<std::string> lineSplit = split(line, "=");
			if (lineSplit.size() < 2)
				continue;

			std::string inputKeysStr = lineSplit[0];
			std::string outputValuesStr = lineSplit[1];

			std::vector<std::string> keySplit = split(inputKeysStr, ","); 
			std::vector<USHORT> vkeyValues;
			for (auto& vkey : keySplit)
				vkeyValues.push_back(xinputvkeys.at(vkey));

			if (outputValuesStr == "ToggleContext")
			{
				JoyToKeyBind bind;
				bind.context = (JoyToKeyContext)i;
				bind.xinputVKeys = vkeyValues;
				toggleContextBind = bind;
				continue;
			}

			std::vector<std::string> outputValuesSplit = split(outputValuesStr, ",");
			size_t numValues = outputValuesSplit.size();
			std::string outputVKey = numValues >= 1 ? outputValuesSplit[0] : "";

			JoyToKeyBind bind;
			bind.context = (JoyToKeyContext)i;
			bind.xinputVKeys = { vkeyValues };
			bind.outputVKey = vkeys.at(outputVKey);
			bind.isToggle = numValues >= 3 ? std::stoi(outputValuesSplit[1]) : 0;

			joyToKeyBinds.push_back(bind);
		}
	}
}

struct MouseSmoothState
{
	double speed_pos = 0;
	double speed_neg = 0;
	double accel_multiplier = 1;
	double accel = 0.01;
	double deaccel = 1.0;
	double rotate_sensitivity_mult = 2.5;
};

void JoyToKeyThread() 
{
	DWORD(WINAPI * XInputGetKeystrokeProc)(DWORD, DWORD, PXINPUT_KEYSTROKE) = nullptr;
	DWORD(WINAPI * XInputGetStateProc)(DWORD, XINPUT_STATE*) = nullptr;

	OutputDebugStringA("PROTOINPUT: Checking for xinput dlls");
	bool xinputFound = false;
	const wchar_t* xinputNames[] = { L"xinput1_3.dll", L"xinput1_4.dll", L"xinput1_2.dll", L"xinput1_1.dll", L"xinput9_1_0.dll" };
	for (const auto xinputName : xinputNames)
	{
		if (GetModuleHandleW(xinputName) != nullptr)
		{
			std::wstring message = L"PROTOINPUT: XInput dll loaded ";
			OutputDebugStringW((message + xinputName).c_str());
			XInputGetKeystrokeProc = (DWORD(WINAPI*)(DWORD, DWORD, PXINPUT_KEYSTROKE))GetProcAddress(GetModuleHandleW(xinputName), "XInputGetKeystroke");
			XInputGetStateProc = (DWORD(WINAPI*)(DWORD, XINPUT_STATE*))GetProcAddress(GetModuleHandleW(xinputName), "XInputGetState");
			break;
		}
	}

	if (!xinputFound && LoadLibraryW(L"xinput1_3.dll"))
	{
		OutputDebugStringA("PROTOINPUT: XInput dll loaded xinput1_3.dll");
		XInputGetKeystrokeProc = (DWORD(WINAPI*)(DWORD, DWORD, PXINPUT_KEYSTROKE))GetProcAddress(GetModuleHandleW(L"xinput1_3.dll"), "XInputGetKeystroke");
		XInputGetStateProc = (DWORD(WINAPI*)(DWORD, XINPUT_STATE*))GetProcAddress(GetModuleHandleW(L"xinput1_3.dll"), "XInputGetState");
	}

	InitJoyToKeyEmulation();

	if (!joyToKeyEnabled)
		return;

	std::chrono::steady_clock::time_point lastUpdateTime = std::chrono::steady_clock::now();
	XINPUT_STATE lastXinputState;
	ZeroMemory(&lastXinputState, sizeof(XINPUT_STATE));

	RAWINPUT rawInputMouse;
	ZeroMemory(&rawInputMouse, sizeof(RAWINPUT));
	rawInputMouse.header.dwType = RIM_TYPEMOUSE;
	rawInputMouse.header.hDevice = (HANDLE)0x00010041;
	rawInputMouse.header.wParam = RIM_INPUT;
	rawInputMouse.data.mouse.lLastX = 0;
	rawInputMouse.data.mouse.lLastY = 0;

	int currentContext = defaultContext;

	MouseSmoothState mouseSmoothStateX;
	mouseSmoothStateX.accel = mouseAcceleration;
	mouseSmoothStateX.deaccel = mouseSmoothing;
	MouseSmoothState mouseSmoothStateY;
	mouseSmoothStateY.accel = mouseAcceleration;
	mouseSmoothStateY.deaccel = mouseSmoothing;

	while (true)
	{
		auto currentTime = std::chrono::steady_clock::now();
		std::chrono::duration<float> deltaTime = currentTime - lastUpdateTime;
		lastUpdateTime = currentTime;

		XINPUT_STATE xinputState;
		ZeroMemory(&xinputState, sizeof(XINPUT_STATE));
		if (ERROR_SUCCESS != XInputGetStateProc(controllerIndex, &xinputState)) continue;

		rawInputMouse.data.mouse.lLastX = 0;
		rawInputMouse.data.mouse.lLastY = 0;
		rawInputMouse.data.mouse.usButtonFlags = 0;

		if (thumbstickToMouseDelta > 0)
		{
			double deltax = thumbstickToMouseDelta == 1 ? lastXinputState.Gamepad.sThumbLX : lastXinputState.Gamepad.sThumbRX;
			double deltay = thumbstickToMouseDelta == 1 ? lastXinputState.Gamepad.sThumbLY : lastXinputState.Gamepad.sThumbRY;
			double mouseDelta[2] = { deltax, deltay };

			for (int i = 0; i < 2; i++)
			{
				MouseSmoothState& mss = i == 0 ? mouseSmoothStateX : mouseSmoothStateY;

				double delta = mouseDelta[i] / 32767.0 * mouseSpeed;// *deltaTime.count();

				double new_speed_pos = delta > 0 ? delta : 0;
				double new_speed_pos_delta = abs(new_speed_pos) - abs(mss.speed_pos);
				double new_speed_neg = delta < 0 ? delta : 0;
				double new_speed_neg_delta = abs(new_speed_neg) - abs(mss.speed_neg);

				mss.speed_pos += new_speed_pos_delta * min(
					(new_speed_pos_delta >= 0 ? mss.accel : mss.deaccel), 1.0
					//* deltaTime.count(), 1.0
				);
				mss.speed_neg += new_speed_neg_delta * min(
					(new_speed_neg_delta >= 0 ? mss.accel : mss.deaccel), 1.0
					//* deltaTime.count(), 1.0
				);

				mouseDelta[i] = round(mss.speed_pos - mss.speed_neg);
			}

			rawInputMouse.data.mouse.lLastX = mouseDelta[0];
			rawInputMouse.data.mouse.lLastY = -mouseDelta[1];
		}

		XINPUT_KEYSTROKE keystroke;
		bool xinputEvent = XInputGetKeystrokeProc != nullptr ? ERROR_SUCCESS == XInputGetKeystrokeProc(controllerIndex, 0, &keystroke) : false;

		if (xinputEvent)
		{
			if (toggleContextBind.xinputVKeys.size())
			{
				bool notInContext = toggleContextBind.context > JoyToKeyContext::DEFAULT &&
					((autoSwitchContext && !FocusHook::captureState) ||
					(toggleContextBind.context != currentContext));
				bool notIsFirstKey = keystroke.VirtualKey != toggleContextBind.xinputVKeys.front();

				if (notInContext || notIsFirstKey)
				{
					bool allPressed = true;
					for (size_t i = 0; i < toggleContextBind.xinputVKeys.size(); i++)
					{
						if (!xinputwbuttons.contains(toggleContextBind.xinputVKeys[i]) ||
							!(xinputState.Gamepad.wButtons & xinputwbuttons.at(toggleContextBind.xinputVKeys[i])))
						{
							allPressed = false;
							break;
						}
					}

					if (allPressed) currentContext = currentContext == JoyToKeyContext::MENU ? JoyToKeyContext::GAMEPLAY : JoyToKeyContext::MENU;
				}
			}

			for (const auto& bind : joyToKeyBinds)
			{
				if (bind.context > JoyToKeyContext::DEFAULT && 
					((autoSwitchContext && !FocusHook::captureState) || 
					(toggleContextBind.context != currentContext)))
					continue;

				if (keystroke.VirtualKey != bind.xinputVKeys.front())
					continue;

				if (bind.xinputVKeys.size() > 1)
				{
					bool allPressed = true;
					for (size_t i = 1; i < bind.xinputVKeys.size(); i++)
					{
						if (ERROR_SUCCESS != XInputGetStateProc(controllerIndex, &xinputState))
						{
							allPressed = false;
							break;
						}

						if (!xinputwbuttons.contains(bind.xinputVKeys[i]) ||
							!(xinputState.Gamepad.wButtons & xinputwbuttons.at(bind.xinputVKeys[i])))
						{
							allPressed = false;
							break;
						}
					}

					if (!allPressed)
						continue;
				}

				RAWINPUT rawInputKeyboard;

				bool keyDown = keystroke.Flags == XINPUT_KEYSTROKE_KEYDOWN;// || keystroke.Flags == 0x0005;//XINPUT_KEYSTROKE_REPEAT;

				USHORT vkey = bind.outputVKey;
				UINT scanCode = MapVirtualKey(vkey, MAPVK_VK_TO_VSC);
				USHORT flags = keyDown ? RI_KEY_MAKE : RI_KEY_BREAK;
				UINT message = keyDown ? WM_KEYDOWN : WM_KEYUP;

				rawInputKeyboard.header.dwType = RIM_TYPEKEYBOARD;
				rawInputKeyboard.header.dwSize = sizeof(RAWINPUT);
				rawInputKeyboard.header.hDevice = (HANDLE)0x00010041;
				rawInputKeyboard.header.wParam = RIM_INPUT;
				rawInputKeyboard.data.keyboard.MakeCode = scanCode;
				rawInputKeyboard.data.keyboard.Flags = flags;
				rawInputKeyboard.data.keyboard.Reserved = 0;
				rawInputKeyboard.data.keyboard.VKey = vkey;
				rawInputKeyboard.data.keyboard.Message = message;
				rawInputKeyboard.data.keyboard.ExtraInformation = 0;

				RawInput::ProcessKeyboardInput(rawInputKeyboard.data.keyboard, nullptr);
			}

			for (const auto& bind : joyToMouseBinds)
			{
				if (bind.context > JoyToKeyContext::DEFAULT &&
					((autoSwitchContext && !FocusHook::captureState) ||
					(toggleContextBind.context != currentContext)))
					continue;

				if (keystroke.VirtualKey != bind.xinputVKeys.front())
					continue;

				if (bind.xinputVKeys.size() > 1)
				{
					bool allPressed = true;
					for (size_t i = 1; i < bind.xinputVKeys.size(); i++)
					{
						if (ERROR_SUCCESS != XInputGetStateProc(controllerIndex, &xinputState))
						{
							allPressed = false;
							break;
						}

						if (!xinputwbuttons.contains(bind.xinputVKeys[i]) ||
							!(xinputState.Gamepad.wButtons & xinputwbuttons.at(bind.xinputVKeys[i])))
						{
							allPressed = false;
							break;
						}
					}

					if (!allPressed)
						continue;
				}

				bool keyDown = keystroke.Flags == XINPUT_KEYSTROKE_KEYDOWN;// || keystroke.Flags == 0x0005;//XINPUT_KEYSTROKE_REPEAT;
				USHORT state = 0;

				if (bind.outputMouseEvent == "MOUSE_LEFT_BUTTON")
					rawInputMouse.data.mouse.usButtonFlags |= keyDown ? RI_MOUSE_LEFT_BUTTON_DOWN : RI_MOUSE_LEFT_BUTTON_UP;
				else if (bind.outputMouseEvent == "MOUSE_RIGHT_BUTTON")
					rawInputMouse.data.mouse.usButtonFlags |= keyDown ? RI_MOUSE_RIGHT_BUTTON_DOWN : RI_MOUSE_RIGHT_BUTTON_UP;
				else if (bind.outputMouseEvent == "MOUSE_MIDDLE_BUTTON")
					rawInputMouse.data.mouse.usButtonFlags |= keyDown ? RI_MOUSE_MIDDLE_BUTTON_DOWN : RI_MOUSE_MIDDLE_BUTTON_UP;
				else if (bind.outputMouseEvent == "MOUSE_BUTTON_4")
					rawInputMouse.data.mouse.usButtonFlags |= keyDown ? RI_MOUSE_BUTTON_4_DOWN : RI_MOUSE_BUTTON_4_UP;
				else if (bind.outputMouseEvent == "MOUSE_BUTTON_5")
					rawInputMouse.data.mouse.usButtonFlags |= keyDown ? RI_MOUSE_BUTTON_5_DOWN : RI_MOUSE_BUTTON_5_UP;
				else if (bind.outputMouseEvent == "MOUSE_WHEEL")
					rawInputMouse.data.mouse.usButtonFlags |= keyDown ? RI_MOUSE_WHEEL : 0;
			}
		}

		RawInput::ProcessMouseInput(rawInputMouse.data.mouse, nullptr);

		lastXinputState = xinputState;

		std::this_thread::sleep_for(std::chrono::milliseconds(8));
	}
}

DWORD WINAPI RawInputWindowThread(LPVOID lpParameter)
{
	printf("Starting Raw Input window thread\n");

	AddThreadToACL(GetCurrentThreadId());
	
	const auto hinstance = GetModuleHandle(nullptr);
	
	WNDCLASS wc = { 0 };
	wc.lpfnWndProc = RawInputWindowWndProc;
	wc.hInstance = hinstance;
	wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND);
	const wchar_t* className = L"PROTORAWINPUT";
	wc.lpszClassName = className;
	wc.style = CS_OWNDC;

	if (RegisterClass(&wc))
	{
		RawInput::rawInputHwnd = CreateWindowW(
			wc.lpszClassName,
			L"Proto Input: Raw Input window",
			0,
			0, 0, 300, 300,
			nullptr, nullptr,
			hinstance,
			nullptr);

		UpdateWindow(RawInput::rawInputHwnd);

		//TODO: cleanup window
		// DestroyWindow(hwnd);
		// UnregisterClassW(className, wc.hInstance);
	}

	MSG msg;
	ZeroMemory(&msg, sizeof(msg));

	std::thread joyToKeyThread(JoyToKeyThread);

	while (msg.message != WM_QUIT)
	{
		//if (PeekMessage(&msg, RawInput::rawInputHwnd, 0U, 0U, PM_REMOVE))
		//if (GetMessage(&msg, RawInput::rawInputHwnd, 0U, 0U))
		if (GetMessage(&msg, RawInput::rawInputHwnd, WM_INPUT, WM_INPUT))
		{
			// if (msg.message == WM_INPUT)
			{
				RawInput::ProcessRawInput((HRAWINPUT)msg.lParam, GET_RAWINPUT_CODE_WPARAM(msg.wParam) == RIM_INPUT, msg);
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}
	}

	printf("End of raw input window thread\n");

	return 0;
}

void RawInput::RefreshDevices()
{
	unsigned int numDevices = 0;
	GetRawInputDeviceList(NULL, &numDevices, sizeof(RAWINPUTDEVICELIST));
	auto deviceArray = std::make_unique<RAWINPUTDEVICELIST[]>(numDevices);
	GetRawInputDeviceList(&deviceArray[0], &numDevices, sizeof(RAWINPUTDEVICELIST));

	const auto oldKbCount = rawInputState.keyboardHandles.size();
	const auto oldMouseCount = rawInputState.mouseHandles.size();
	
	rawInputState.keyboardHandles.clear();
	rawInputState.mouseHandles.clear();
	
	std::cout << "Raw input devices:\n";
	for (unsigned int i = 0; i < numDevices; ++i)
	{
		auto* device = &deviceArray[i];
		std::cout << (device->dwType == RIM_TYPEHID ? "HID" : device->dwType == RIM_TYPEKEYBOARD ? "Keyboard" : "Mouse")
			<< ": " << device->hDevice << std::endl;
		
		if (device->dwType == RIM_TYPEKEYBOARD)
		{
			rawInputState.keyboardHandles.push_back(device->hDevice);
		}
		else if (device->dwType == RIM_TYPEMOUSE)
		{
			rawInputState.mouseHandles.push_back(device->hDevice);
		}
	}

	// Device handle 0
	rawInputState.keyboardHandles.push_back(0);
	rawInputState.mouseHandles.push_back(0);

	// Add the new devices to the deselected list
	for (const auto mouse : rawInputState.mouseHandles)
	{
		if (std::find(rawInputState.selectedMouseHandles.begin(), rawInputState.selectedMouseHandles.end(), mouse) == rawInputState.selectedMouseHandles.end()
			&& std::find(rawInputState.deselectedMouseHandles.begin(), rawInputState.deselectedMouseHandles.end(), mouse) == rawInputState.deselectedMouseHandles.end())
		{
			rawInputState.deselectedMouseHandles.push_back(mouse);
		}
	}
	for (const auto keyboard : rawInputState.keyboardHandles)
	{
		if (std::find(rawInputState.selectedKeyboardHandles.begin(), rawInputState.selectedKeyboardHandles.end(), keyboard) == rawInputState.selectedKeyboardHandles.end()
			&& std::find(rawInputState.deselectedKeyboardHandles.begin(), rawInputState.deselectedKeyboardHandles.end(), keyboard) == rawInputState.deselectedKeyboardHandles.end())
		{
			rawInputState.deselectedKeyboardHandles.push_back(keyboard);
		}
	}

	// If any devices are unplugged, remove them
	for (int i = rawInputState.selectedMouseHandles.size() - 1; i >= 0; --i)
	{
		if (std::find(rawInputState.mouseHandles.begin(), rawInputState.mouseHandles.end(), rawInputState.selectedMouseHandles[i]) == rawInputState.mouseHandles.end())
			rawInputState.selectedMouseHandles.erase(rawInputState.selectedMouseHandles.begin() + i);
	}
	for (int i = rawInputState.deselectedMouseHandles.size() - 1; i >= 0; --i)
	{
		if (std::find(rawInputState.mouseHandles.begin(), rawInputState.mouseHandles.end(), rawInputState.deselectedMouseHandles[i]) == rawInputState.mouseHandles.end())
			rawInputState.deselectedMouseHandles.erase(rawInputState.deselectedMouseHandles.begin() + i);
	}
	
	for (int i = rawInputState.selectedKeyboardHandles.size() - 1; i >= 0; --i)
	{
		if (std::find(rawInputState.keyboardHandles.begin(), rawInputState.keyboardHandles.end(), rawInputState.selectedKeyboardHandles[i]) == rawInputState.keyboardHandles.end())
			rawInputState.selectedMouseHandles.erase(rawInputState.selectedMouseHandles.begin() + i);
	}
	for (int i = rawInputState.deselectedKeyboardHandles.size() - 1; i >= 0; --i)
	{
		if (std::find(rawInputState.keyboardHandles.begin(), rawInputState.keyboardHandles.end(), rawInputState.deselectedKeyboardHandles[i]) == rawInputState.keyboardHandles.end())
			rawInputState.deselectedKeyboardHandles.erase(rawInputState.deselectedKeyboardHandles.begin() + i);
	}
}

void RawInput::AddSelectedMouseHandle(unsigned handle)
{
	if (std::find(rawInputState.selectedMouseHandles.begin(), rawInputState.selectedMouseHandles.end(), (void*)handle) == rawInputState.selectedMouseHandles.end())
		rawInputState.selectedMouseHandles.push_back((void*)handle);

	for (int i = rawInputState.deselectedMouseHandles.size() - 1; i >= 0; --i)
	{
		if (rawInputState.deselectedMouseHandles[i] == (void*)handle)
			rawInputState.deselectedMouseHandles.erase(rawInputState.deselectedMouseHandles.begin() + i);
	}
}

void RawInput::AddSelectedKeyboardHandle(unsigned handle)
{
	if (std::find(rawInputState.selectedKeyboardHandles.begin(), rawInputState.selectedKeyboardHandles.end(), (void*)handle) == rawInputState.selectedKeyboardHandles.end())
		rawInputState.selectedKeyboardHandles.push_back((void*)handle);

	for (int i = rawInputState.deselectedKeyboardHandles.size() - 1; i >= 0; --i)
	{
		if (rawInputState.deselectedKeyboardHandles[i] == (void*)handle)
			rawInputState.deselectedKeyboardHandles.erase(rawInputState.deselectedKeyboardHandles.begin() + i);
	}
}

void RawInput::AddWindowToForward(HWND hwnd)
{
	if (auto find = std::find(forwardingWindows.begin(), forwardingWindows.end(), hwnd) == forwardingWindows.end())
	{
		printf("Adding hwnd 0x%X to forwarding list\n", hwnd);
		forwardingWindows.push_back(hwnd);
	}
}

void RawInput::SetUsageBitField(std::bitset<9> _usages)
{	
	usages = _usages;
}

std::bitset<9> RawInput::GetUsageBitField()
{
	return usages;
}

void RawInput::InitialiseRawInput()
{
	RefreshDevices();

	HANDLE hThread = CreateThread(nullptr, 0,
								  (LPTHREAD_START_ROUTINE)RawInputWindowThread, GetModuleHandle(nullptr), 0, 0);
	if (hThread != nullptr)
		CloseHandle(hThread);
}

void RawInput::UnregisterGameFromRawInput()
{
	printf("Unregistering game from raw input\n");

	std::vector<RAWINPUTDEVICE> devices{};

	for (const auto& usage : usageTypesOfInterest)
	{
		RAWINPUTDEVICE dev;
		dev.usUsagePage = HID_USAGE_PAGE_GENERIC;
		dev.usUsage = usage;
		dev.dwFlags = RIDEV_REMOVE;
		dev.hwndTarget = NULL;
		devices.push_back(dev);

		auto res = RegisterRawInputDevices(&dev, 1, sizeof(RAWINPUTDEVICE));
		printf("Deregister usage 0x%X: Result 0x%X\n", usage, res);
	}
}

void RawInput::RegisterProtoForRawInput()
{
	int tryCount = 0;

	//TODO: use a mutex or something (this holds up the UI from updating). (Although it isn't really that important)
	do
	{
		if (rawInputHwnd == nullptr)
		{
			printf("Trying to register for raw input, but window hasn't opened yet. Waiting 0.5s\n");
			Sleep(500);
		}
	}
	while (tryCount++ < 20);

	if (rawInputHwnd == nullptr)
	{
		fprintf(stderr, "Raw input window hasn't opened. Giving up registering raw input\n");
		return;
	}

	
	std::vector<RAWINPUTDEVICE> devices{};
	
	for (const auto& usage : usageTypesOfInterest)
	{
		RAWINPUTDEVICE dev;

		dev.usUsagePage = HID_USAGE_PAGE_GENERIC;
		dev.usUsage = usage;
		dev.dwFlags = RIDEV_INPUTSINK;
		dev.hwndTarget = rawInputHwnd;		
		
		devices.push_back(dev);
	}

	if (RegisterRawInputDevices(&devices[0], devices.size(), sizeof(RAWINPUTDEVICE)) == FALSE)
	{
		fprintf(stderr, "Failed to register raw input, last error = 0x%X\n", GetLastError());
	}
	else
		printf("Successfully register raw input\n");
}

}
