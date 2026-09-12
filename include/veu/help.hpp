#pragma once

#include <windows.h>

#include <string_view>

namespace veu {

void show_help(HWND owner, std::wstring_view context = {});
void show_about(HWND owner);

}  // namespace veu
