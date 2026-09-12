#pragma once
#include <map>
#include <string>
#include <windows.h>
namespace veu {
HWND create_subnet_pane(HWND parent);
void stop_subnet(HWND);
bool subnet_busy(HWND);
void select_subnet_tool(
    HWND, unsigned); // calculator, split, VLSM, aggregate, compare
std::map<std::string, std::string> subnet_plan_fields(HWND);
void load_subnet_plan_fields(HWND, const std::map<std::string, std::string> &);
} // namespace veu
