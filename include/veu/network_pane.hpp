#pragma once
#include "veu/network.hpp"
#include <windows.h>
#include <map>
#include <string>
namespace veu
{
HWND create_network_pane(HWND parent);
void select_network_tool(HWND pane, net::Tool tool);
bool network_busy(HWND pane);
void network_set_dpi(HWND pane, unsigned dpi);
void stop_network(HWND pane);
std::map<std::string, std::string> network_plan_fields(HWND pane);
void load_network_plan_fields(HWND pane, const std::map<std::string, std::string> &fields);
// WM_APP + 10 is posted to parent for explicit form edits only. Loading does not start requests.
} // namespace veu
