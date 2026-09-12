#include "veu/subnet_pane.hpp"
#include "veu/ui_common.hpp"
#include <commctrl.h>
#include <iostream>
#include <windows.h>
namespace veu {
void show_help(HWND, std::wstring_view) {}
} // namespace veu
namespace {
int checks = 0;
int navigated = -1;
LRESULT CALLBACK host_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_APP + 11) {
    navigated = int(wp);
    return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}
void check(bool ok, const char *message) {
  ++checks;
  if (!ok)
    throw std::runtime_error(message);
}
void pump() {
  MSG m;
  while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
}
template <class P> void wait(P predicate, const char *message) {
  auto end = GetTickCount64() + 10000;
  do {
    pump();
    if (predicate())
      return;
    Sleep(5);
  } while (GetTickCount64() < end);
  throw std::runtime_error(message);
}
std::wstring summary(HWND pane) { return veu::ui::text(GetDlgItem(pane, 11)); }
std::wstring field(HWND pane, int index) {
  return veu::ui::text(GetDlgItem(pane, 1000 + index));
}
std::wstring cell(HWND pane, int row, int col) {
  wchar_t b[4096]{};
  ListView_GetItemText(GetDlgItem(pane, 12), row, col, b, 4096);
  return b;
}
void click(HWND pane, int id) {
  SendMessageW(pane, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED),
               reinterpret_cast<LPARAM>(GetDlgItem(pane, id)));
}
void set(HWND pane, int index, const wchar_t *value) {
  SetWindowTextW(GetDlgItem(pane, 1000 + index), value);
}
void completed(HWND pane) {
  wait(
      [&] {
        return !veu::subnet_busy(pane) &&
               summary(pane).find(L"Calculating") == std::wstring::npos;
      },
      "worker completion reaches the UI");
  pump();
}
void calculator(HWND pane) {
  wait(
      [&] {
        return summary(pane).find(L"Exact calculation") != std::wstring::npos;
      },
      "initial production calculation completes");
  check(cell(pane, 1, 1) == L"192.168.10.0/26",
        "known network reaches actual ListView");
  set(pane, 0, L"invalid");
  check(ListView_GetItemCount(GetDlgItem(pane, 12)) == 0,
        "invalid edit immediately clears stale rows");
  check(!IsWindowEnabled(GetDlgItem(pane, 101)),
        "invalid edit immediately disables Copy");
  wait([&] { return summary(pane).find(L"Input error") != std::wstring::npos; },
       "invalid auto calculation reports inline error");
  set(pane, 0, L"192.168.10.42/26");
  click(pane, 100);
  completed(pane);
  check(cell(pane, 1, 1) == L"192.168.10.0/26",
        "valid edit restores known result");
  check(field(pane, 0) == L"192.168.10.42" && field(pane, 1) == L"26",
        "CIDR paste updates address and prefix together");
  click(pane, 105);
  completed(pane);
  check(cell(pane, 0, 1) == L"192.168.10.106",
        "next subnet preserves host offset");
}
void scrolling(HWND pane) {
  veu::select_subnet_tool(pane, 2);
  MoveWindow(pane, 0, 0, 320, 400, FALSE);
  SendMessageW(pane, WM_DPICHANGED, MAKEWPARAM(192, 192), 0);
  SCROLLINFO si{sizeof(si), SIF_ALL};
  GetScrollInfo(pane, SB_VERT, &si);
  check(si.nMax > int(si.nPage),
        "narrow high-DPI pane provides vertical scrolling");
  RECT bounds{}, child{};
  GetClientRect(pane, &bounds);
  GetWindowRect(GetDlgItem(pane, 1000), &child);
  MapWindowPoints(nullptr, pane, reinterpret_cast<POINT *>(&child), 2);
  check(child.left >= 0 && child.right <= bounds.right,
        "narrow input stays inside viewport width");
  SendMessageW(pane, WM_VSCROLL, SB_BOTTOM, 0);
  GetWindowRect(GetDlgItem(pane, 12), &child);
  MapWindowPoints(nullptr, pane, reinterpret_cast<POINT *>(&child), 2);
  check(child.bottom <= bounds.bottom && child.bottom > 0,
        "scroll reaches results at high DPI");
  auto font = reinterpret_cast<HFONT>(
      SendMessageW(GetDlgItem(pane, 1000), WM_GETFONT, 0, 0));
  LOGFONTW high{};
  GetObjectW(font, sizeof(high), &high);
  SendMessageW(pane, WM_DPICHANGED, MAKEWPARAM(96, 96), 0);
  font = reinterpret_cast<HFONT>(
      SendMessageW(GetDlgItem(pane, 1000), WM_GETFONT, 0, 0));
  LOGFONTW low{};
  GetObjectW(font, sizeof(low), &low);
  check(abs(high.lfHeight) >= abs(low.lfHeight) * 2 - 1,
        "DPI changes resize control fonts");
  check(GetNextDlgTabItem(pane, GetDlgItem(pane, 10), FALSE) ==
            GetDlgItem(pane, 1000),
        "keyboard order reaches inputs immediately after tabs");
  MoveWindow(pane, 0, 0, 800, 800, FALSE);
}
void split(HWND pane) {
  veu::select_subnet_tool(pane, 1);
  set(pane, 0, L"::/0");
  set(pane, 1, L"128");
  set(pane, 2, L"340282366920938463463374607431768211255");
  click(pane, 100);
  completed(pane);
  check(ListView_GetItemCount(GetDlgItem(pane, 12)) == 200,
        "split page renders only 200 children");
  check(IsWindowEnabled(GetDlgItem(pane, 105)),
        "split next enabled beyond current materialized page");
  click(pane, 105);
  completed(pane);
  check(field(pane, 2) == L"340282366920938463463374607431768211455",
        "next split page updates exact wide index");
  check(ListView_GetItemCount(GetDlgItem(pane, 12)) == 1,
        "last split page has one child");
  check(cell(pane, 0, 1) == L"ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/128",
        "last IPv6 child displayed exactly");
}
void allocation(HWND pane) {
  veu::select_subnet_tool(pane, 2);
  set(pane, 0, L"192.0.2.0/24");
  set(pane, 1, L"a | First | 50 | LAN\r\nb | Second | 50 | LAN");
  set(pane, 2, L"");
  auto before = field(pane, 1);
  click(pane, 100);
  completed(pane);
  check(IsWindowEnabled(GetDlgItem(pane, 107)), "valid proposal enables Apply");
  check(cell(pane, 0, 1) == L"192.0.2.0/26", "first stable allocation");
  set(pane, 1,
      L"b | Renamed second | 50 | LAN\r\na | Renamed first | 50 | LAN");
  click(pane, 100);
  completed(pane);
  check(cell(pane, 0, 0) == L"a" && cell(pane, 0, 1) == L"192.0.2.0/26",
        "view order and rename do not change tie priority");
  before = field(pane, 1);
  auto saved_order = veu::subnet_plan_fields(pane);
  click(pane, 107);
  check(field(pane, 1).find(L"192.0.2.0/26") != std::wstring::npos,
        "Apply stores assignments in actual input");
  check(IsWindowEnabled(GetDlgItem(pane, 111)),
        "Apply enables transaction Undo");
  auto applied = field(pane, 1);
  click(pane, 111);
  check(field(pane, 1) == before,
        "Undo restores complete prior requirement text");
  click(pane, 112);
  check(field(pane, 1) == applied, "Redo restores applied assignments");
  auto saved = veu::subnet_plan_fields(pane);
  veu::load_subnet_plan_fields(pane, saved);
  check(ListView_GetItemCount(GetDlgItem(pane, 12)) == 0 &&
            !IsWindowEnabled(GetDlgItem(pane, 107)),
        "loading plan leaves derived results unavailable until recalculation");
  veu::load_subnet_plan_fields(pane, saved_order);
  click(pane, 100);
  completed(pane);
  check(cell(pane, 0, 0) == L"a" && cell(pane, 0, 1) == L"192.0.2.0/26",
        "saved unassigned plan retains insertion priority after reload");
}
void worker_revision(HWND pane) {
  veu::select_subnet_tool(pane, 2);
  set(pane, 0, L"10.0.0.0/16");
  std::wstring rows;
  for (unsigned i = 0; i < 7000; ++i) {
    rows += L"r" + std::to_wstring(i) + L" | host | 1 | HOST | 10.0." +
            std::to_wstring(i / 256) + L"." + std::to_wstring(i % 256) +
            L"/32\r\n";
  }
  set(pane, 1, rows.c_str());
  set(pane, 2, L"");
  auto start = GetTickCount64();
  click(pane, 100);
  check(GetTickCount64() - start < 1000,
        "large VLSM command returns without running allocation on UI thread");
  set(pane, 0, L"invalid");
  check(ListView_GetItemCount(GetDlgItem(pane, 12)) == 0,
        "editing during work clears old results");
  wait([&] { return !veu::subnet_busy(pane); },
       "editing cancels obsolete work without an explicit Stop");
  for (int i = 0; i < 15; ++i) {
    pump();
    Sleep(10);
  }
  check(ListView_GetItemCount(GetDlgItem(pane, 12)) == 0 &&
            !IsWindowEnabled(GetDlgItem(pane, 107)),
        "superseded completion cannot restore rows or Apply");
  set(pane, 0, L"10.0.0.0/16");
  click(pane, 100);
  click(pane, 110);
  wait([&] { return !veu::subnet_busy(pane); },
       "Stop button drains the worker");
  check(summary(pane).find(L"Cancelled") != std::wstring::npos &&
            !IsWindowEnabled(GetDlgItem(pane, 107)),
        "Stop button cancels the proposal and disables Apply");
}
void comparison(HWND pane) {
  veu::select_subnet_tool(pane, 4);
  set(pane, 0, L"192.0.2.5 - 192.0.2.14");
  set(pane, 1, L"192.0.2.10 - 192.0.2.20");
  click(pane, 100);
  completed(pane);
  check(summary(pane).find(L"Intersection: 5; A only: 5; B only: 6") !=
            std::wstring::npos,
        "directional counts reach production comparison UI");
  set(pane, 0, L"\r\n");
  click(pane, 100);
  completed(pane);
  check(summary(pane).find(L"Input error") != std::wstring::npos,
        "blank comparison field is incomplete rather than an intentional empty "
        "set");
  set(pane, 0, L"EMPTY");
  set(pane, 1, L"EMPTY");
  click(pane, 100);
  completed(pane);
  check(summary(pane).find(L"Equal.") != std::wstring::npos &&
            IsWindowEnabled(GetDlgItem(pane, 101)),
        "explicit empty sets yield a copyable valid summary");
}
void internal_navigation(HWND pane) {
  veu::select_subnet_tool(pane, 1);
  click(pane, 109);
  pump();
  check(navigated == 2, "internal VLSM toggle notifies the frame");
  auto tabs = GetDlgItem(pane, 10);
  TabCtrl_SetCurSel(tabs, 3);
  NMHDR changed{tabs, 10, TCN_SELCHANGE};
  SendMessageW(pane, WM_NOTIFY, 10, reinterpret_cast<LPARAM>(&changed));
  pump();
  check(navigated == 4, "internal tab selection notifies the frame");
  veu::select_subnet_tool(pane, 0);
  set(pane, 0, L"0.0.0.0/0");
  click(pane, 106);
  completed(pane);
  check(ListView_GetItemCount(GetDlgItem(pane, 12)) == 200 &&
            IsWindowEnabled(GetDlgItem(pane, 114)),
        "large calculator classification has accessible result paging");
  auto first = cell(pane, 0, 1);
  click(pane, 114);
  check(ListView_GetItemCount(GetDlgItem(pane, 12)) > 0 &&
            cell(pane, 0, 1) != first,
        "calculator second result page is reachable even for /0");
}
void close_during_work(HWND parent) {
  for (int run = 0; run < 3; ++run) {
    auto pane = veu::create_subnet_pane(parent);
    check(pane != nullptr, "close-stress pane creates");
    veu::select_subnet_tool(pane, 2);
    set(pane, 0, L"10.0.0.0/16");
    std::wstring rows;
    for (unsigned i = 0; i < 7000; ++i)
      rows += L"r" + std::to_wstring(i) + L" | host | 1 | HOST | 10.0." +
              std::to_wstring(i / 256) + L"." + std::to_wstring(i % 256) +
              L"/32\r\n";
    set(pane, 1, rows.c_str());
    set(pane, 2, L"");
    click(pane, 100);
    auto start = GetTickCount64();
    DestroyWindow(pane);
    check(GetTickCount64() - start < 1000,
          "destroying a busy pane does not block on worker completion");
    for (int i = 0; i < 20; ++i) {
      pump();
      Sleep(5);
    }
  }
}
} // namespace
int main() {
  HWND parent{}, pane{};
  try {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX init{sizeof(init), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&init);
    WNDCLASSW host{};
    host.lpfnWndProc = host_proc;
    host.hInstance = GetModuleHandleW(nullptr);
    host.lpszClassName = L"VEU.SubnetPaneTests";
    RegisterClassW(&host);
    parent = CreateWindowExW(0, host.lpszClassName,
                             L"Hidden subnet integration tests",
                             WS_OVERLAPPEDWINDOW, 0, 0, 900, 900, nullptr,
                             nullptr, GetModuleHandleW(nullptr), nullptr);
    check(parent != nullptr, "hidden host creates");
    pane = veu::create_subnet_pane(parent);
    check(pane != nullptr, "production subnet pane creates");
    MoveWindow(pane, 0, 0, 800, 800, FALSE);
    calculator(pane);
    scrolling(pane);
    split(pane);
    allocation(pane);
    worker_revision(pane);
    comparison(pane);
    internal_navigation(pane);
    DestroyWindow(pane);
    pane = nullptr;
    close_during_work(parent);
    DestroyWindow(parent);
    parent = nullptr;
    CoUninitialize();
    std::cout << "PASS: " << checks << " native subnet pane checks\n";
    return 0;
  } catch (const std::exception &e) {
    if (pane) {
      veu::stop_subnet(pane);
      DestroyWindow(pane);
    }
    if (parent)
      DestroyWindow(parent);
    std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
    return 1;
  }
}
