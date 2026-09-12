
#include "veu/network_pane.hpp"
#include <commctrl.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace veu
{
void show_help(HWND, std::wstring_view)
{
}
} // namespace veu
void check(bool ok, const char *what)
{
    if (!ok)
        throw std::runtime_error(what);
}
void pump()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
int main()
{
    try
    {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&controls);
        HWND parent = CreateWindowExW(0, L"STATIC", L"VEU native smoke", WS_OVERLAPPEDWINDOW, 0, 0, 1000, 720, nullptr,
                                      nullptr, GetModuleHandleW(nullptr), nullptr);
        check(parent != nullptr, "parent window");
        HWND pane = veu::create_network_pane(parent);
        check(pane != nullptr, "network pane creation");
        MoveWindow(pane, 0, 0, 400, 300, TRUE);
        veu::network_set_dpi(pane, 192);
        SCROLLINFO scroll{sizeof(scroll), SIF_ALL};
        GetScrollInfo(pane, SB_VERT, &scroll);
        check(scroll.nMax > static_cast<int>(scroll.nPage), "small high-DPI pane exposes vertical scroll range");
        check((GetWindowLongPtrW(pane, GWL_STYLE) & WS_VSCROLL) != 0, "pane has native vertical scrollbar");
        GetScrollInfo(pane, SB_HORZ, &scroll);
        check(scroll.nMax > static_cast<int>(scroll.nPage),
              "narrow high-DPI pane exposes horizontal access when needed");
        SendMessageW(pane, WM_VSCROLL, SB_BOTTOM, 0);
        GetScrollInfo(pane, SB_VERT, &scroll);
        check(scroll.nPos > 0, "native vertical scrolling reaches lower controls");
        HWND scroll_list = FindWindowExW(pane, nullptr, WC_LISTVIEWW, nullptr);
        check(scroll_list != nullptr, "scrollable table exists");
        RECT list_bounds{}, pane_bounds{};
        GetWindowRect(scroll_list, &list_bounds);
        MapWindowPoints(nullptr, pane, reinterpret_cast<POINT *>(&list_bounds), 2);
        GetClientRect(pane, &pane_bounds);
        check(list_bounds.bottom > 0 && list_bounds.bottom <= pane_bounds.bottom,
              "bottom result controls reachable at scroll end");
        SendMessageW(pane, WM_VSCROLL, SB_TOP, 0);
        veu::network_set_dpi(pane, 144);
        GetScrollInfo(pane, SB_VERT, &scroll);
        check(scroll.nMax > static_cast<int>(scroll.nPage), "150-percent small pane has reachable overflow");
        veu::network_set_dpi(pane, 96);
        MoveWindow(pane, 0, 0, 960, 650, TRUE);
        auto fields = veu::network_plan_fields(pane);
        check(!fields.empty(), "default plan fields");
        fields["network.4.target"] = "127.0.0.1; ::1";
        fields["network.4.count"] = "2";
        fields["network.4.interval_ms"] = "0";
        veu::load_network_plan_fields(pane, fields);
        check(!veu::network_busy(pane), "loading does not run probes");
        veu::select_network_tool(pane, veu::net::Tool::ping);
        check(veu::network_plan_fields(pane).at("network.4.target") == "127.0.0.1; ::1", "loaded targets retained");
        SendMessageW(pane, WM_COMMAND, 100, 0);
        check(veu::network_busy(pane), "explicit Start starts owned job");
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        while (veu::network_busy(pane) && std::chrono::steady_clock::now() < deadline)
        {
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check(!veu::network_busy(pane), "multi-target ping completes");
        SendMessageW(pane, WM_TIMER, 1, 0);
        check(SendMessageW(GetDlgItem(pane, 107), CB_GETCOUNT, 0, 0) == 2, "two separately selectable targets");
        veu::select_network_tool(pane, veu::net::Tool::dns);
        veu::select_network_tool(pane, veu::net::Tool::ping);
        check(veu::network_plan_fields(pane).at("network.4.target") == "127.0.0.1; ::1",
              "switching tools preserves fields");
        veu::network_set_dpi(pane, 144);
        MoveWindow(pane, 0, 0, 1200, 900, TRUE);
        fields = veu::network_plan_fields(pane);
        fields["network.4.count"] = "1";
        fields["network.4.interval_ms"] = "100";
        fields["network.4.continuous"] = "1";
        fields["network.4.window"] = "Last 60 seconds";
        veu::load_network_plan_fields(pane, fields);
        SendMessageW(pane, WM_COMMAND, 100, 0);
        auto wait_for = [&](int milliseconds) {
            auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
            while (std::chrono::steady_clock::now() < until)
            {
                pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            SendMessageW(pane, WM_TIMER, 1, 0);
        };
        wait_for(500);
        check(veu::network_busy(pane), "continuous ping ignores finite count and remains active");
        HWND list = FindWindowExW(pane, nullptr, WC_LISTVIEWW, nullptr);
        check(list != nullptr, "native result table exists");
        check(ListView_GetItemCount(list) >= 2, "continuous ping records multiple actual attempts");
        SendMessageW(pane, WM_COMMAND, 108, 0);
        wait_for(300);
        int paused_rows = ListView_GetItemCount(list);
        wait_for(300);
        check(ListView_GetItemCount(list) == paused_rows, "pause stops scheduling additional probes");
        bool gap = false;
        for (int i = 0; i < paused_rows; ++i)
        {
            wchar_t outcome[100]{};
            ListView_GetItemText(list, i, 5, outcome, 100);
            if (std::wstring(outcome) == L"Not sent")
                gap = true;
        }
        check(gap, "pause records an explicit not-sent gap");
        SendMessageW(pane, WM_COMMAND, 108, 0);
        wait_for(400);
        check(ListView_GetItemCount(list) > paused_rows, "resume schedules new observations");
        check(veu::network_plan_fields(pane).at("network.4.window") == "Last 60 seconds",
              "rolling selector persists in plan");
        veu::stop_network(pane);
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (veu::network_busy(pane) && std::chrono::steady_clock::now() < deadline)
        {
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check(!veu::network_busy(pane), "stop drains finite workers");
        DestroyWindow(pane);
        DestroyWindow(parent);
        std::cout << "native network pane smoke passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
