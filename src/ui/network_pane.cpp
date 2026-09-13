#include "veu/network_pane.hpp"
#include "veu/help.hpp"
#include "veu/storage.hpp"
#include "veu/result_grid.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace veu
{
namespace
{
constexpr UINT poll_timer = 1;
constexpr int start_id = 100, stop_id = 101, copy_id = 102, export_id = 103, advanced_id = 104, previous_id = 105,
              next_id = 106, target_id = 107, pause_id = 108;
constexpr size_t page_size = 250;
const wchar_t *class_name = L"VEU.NetworkPane";
const wchar_t *graph_class = L"VEU.NetworkGraph";
struct Field
{
    const char *key;
    const wchar_t *label;
    bool advanced = false;
    const wchar_t *choices = nullptr;
    bool check = false;
};
struct FieldControl
{
    Field spec;
    HWND label = nullptr, control = nullptr;
};
using Fields = std::map<std::string, std::string>;
struct Job
{
    std::atomic_bool cancel = false, done = false, paused = false;
    std::atomic_uint revision = 0;
    std::mutex mutex;
    std::vector<net::Request> requests;
    std::vector<net::Result> results;
};
struct View
{
    Fields fields;
    std::shared_ptr<Job> job;
    size_t selected = 0, page = 0;
    unsigned shown_revision = 0;
    bool inputs_changed = false;
    net::Result raw, displayed;
    std::chrono::steady_clock::time_point received = std::chrono::steady_clock::now();
};
struct Pane
{
    HWND window = nullptr, title = nullptr, description = nullptr, summary = nullptr, list = nullptr, graph = nullptr,
         target = nullptr, page_label = nullptr;
    HWND start = nullptr, stop = nullptr, copy = nullptr, export_button = nullptr, advanced = nullptr,
         previous = nullptr, next = nullptr, pause_button = nullptr;
    HFONT font = nullptr;
    unsigned dpi = 96;
    net::Tool tool = net::Tool::adapters;
    std::array<View, 11> views;
    std::vector<FieldControl> controls;
    bool loading = false;
    int scroll_x = 0, scroll_y = 0, content_width = 0, content_height = 0, viewport_width = 0, viewport_height = 0;
    HWND last_focus = nullptr;
};
std::string utf8(std::wstring_view s)
{
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0,
                                nullptr, nullptr);
    if (!n)
        throw std::runtime_error("Invalid Unicode");
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}
std::wstring wide(std::string_view s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
std::wstring text(HWND control)
{
    int n = GetWindowTextLengthW(control);
    std::wstring s(static_cast<size_t>(n) + 1, 0);
    GetWindowTextW(control, s.data(), n + 1);
    s.resize(n);
    return s;
}
Pane *pane(HWND window)
{
    return reinterpret_cast<Pane *>(GetWindowLongPtrW(window, GWLP_USERDATA));
}
View &view(Pane &p)
{
    return p.views[static_cast<size_t>(p.tool)];
}
int scale(Pane &p, int n)
{
    return MulDiv(n, static_cast<int>(p.dpi), 96);
}
const wchar_t *title(net::Tool t)
{
    switch (t)
    {
    case net::Tool::adapters:
        return L"Local addresses & interfaces";
    case net::Tool::routes:
        return L"Routes";
    case net::Tool::neighbours:
        return L"Neighbours";
    case net::Tool::external_ip:
        return L"External IP · Provided by ipify";
    case net::Tool::ping:
        return L"Visual Ping";
    case net::Tool::traceroute:
        return L"Repeated Traceroute";
    case net::Tool::dns:
        return L"DNS lookup";
    case net::Tool::tcp:
        return L"TCP connectivity";
    case net::Tool::http:
        return L"HTTP / TLS inspection";
    case net::Tool::wake_on_lan:
        return L"Wake-on-LAN";
    case net::Tool::mtu:
        return L"MTU observations";
    }
    return L"Network tools";
}
const wchar_t *help_topic(net::Tool t)
{
    switch (t)
    {
    case net::Tool::ping:
        return L"ping-statistics";
    case net::Tool::traceroute:
        return L"read-ping-and-traceroute-results";
    case net::Tool::dns:
        return L"interpret-a-dns-lookup";
    case net::Tool::tcp:
        return L"tcp-checks";
    case net::Tool::http:
        return L"http-tls";
    case net::Tool::wake_on_lan:
        return L"wake-on-lan";
    case net::Tool::mtu:
        return L"mtu-basics";
    default:
        return L"interfaces-routes";
    }
}
Fields defaults(net::Tool t)
{
    Fields f = {{"target", ""},           {"source", ""},
                {"resolver", ""},         {"dns_type", "A"},
                {"method", "HEAD"},       {"mac", ""},
                {"family", "0"},          {"timeout_ms", "1000"},
                {"deadline_ms", "15000"}, {"count", "20"},
                {"interval_ms", "1000"},  {"payload_bytes", "32"},
                {"max_hops", "30"},       {"probes_per_hop", "3"},
                {"rounds", "1"},          {"interface_index", "0"},
                {"mtu_ceiling", "1500"},  {"wol_burst", "1"},
                {"port", "443"},          {"bypass_cache", "0"},
                {"direct", "0"},          {"follow_redirects", "0"},
                {"continuous", "0"},      {"window", "Session (retained)"}};
    if (t == net::Tool::wake_on_lan)
        f["port"] = "9";
    if (t == net::Tool::traceroute)
        f["deadline_ms"] = "120000";
    if (t == net::Tool::external_ip)
        f["deadline_ms"] = "10000";
    if (t == net::Tool::dns || t == net::Tool::tcp)
        f["deadline_ms"] = "5000";
    if (t == net::Tool::mtu)
        f["deadline_ms"] = "60000";
    return f;
}
std::vector<Field> field_specs(net::Tool t)
{
    Field target{"target", t == net::Tool::ping ? L"Targets (up to 8; separate with ;)" : L"Target"};
    Field family{"family", L"Address family", false, L"Automatic|IPv4|IPv6"};
    Field source{"source", L"Local source address", true};
    Field iface{"interface_index", L"Interface index (0 = selected by Windows)", true};
    Field timeout{"timeout_ms", L"Per-operation timeout (ms)", true};
    Field deadline{"deadline_ms", L"Total deadline (ms)", true};
    Field payload{"payload_bytes", L"Echo data bytes", true};
    switch (t)
    {
    case net::Tool::adapters:
        return {};
    case net::Tool::routes:
    case net::Tool::neighbours:
        return {family};
    case net::Tool::external_ip:
        return {family, {"direct", L"Use direct connection", true, nullptr, true}, timeout, deadline};
    case net::Tool::ping:
        return {target,
                family,
                {"count", L"Count per target (finite mode)"},
                {"interval_ms", L"Interval (ms)"},
                {"continuous", L"Continuous until Stop", false, nullptr, true},
                {"window", L"Statistics / table window", false, L"Session (retained)|Last 60 seconds|Last 5 minutes"},
                timeout,
                payload,
                source,
                iface};
    case net::Tool::traceroute:
        return {target,
                family,
                {"max_hops", L"Maximum hops"},
                {"rounds", L"Rounds"},
                {"probes_per_hop", L"Probes per hop", true},
                timeout,
                deadline,
                payload,
                source,
                iface};
    case net::Tool::dns:
        return {target,
                {"dns_type", L"Record type", false, L"A|AAAA|CNAME|MX|NS|SOA|PTR|TXT|SRV"},
                {"resolver", L"Resolver address (blank = Windows)"},
                {"bypass_cache", L"Bypass cache", true, nullptr, true},
                iface,
                deadline};
    case net::Tool::tcp:
        return {target, {"port", L"TCP port"}, family, source, iface, deadline};
    case net::Tool::http:
        return {{"target", L"HTTP(S) URL"},
                {"method", L"Method", false, L"HEAD|GET"},
                {"direct", L"Use direct connection", true, nullptr, true},
                {"follow_redirects", L"Follow at most 5 redirects", true, nullptr, true},
                timeout,
                deadline};
    case net::Tool::wake_on_lan:
        return {{"target", L"IPv4 destination / broadcast"},
                {"source", L"Local IPv4 source"},
                {"mac", L"MAC address"},
                {"port", L"UDP port"},
                {"wol_burst", L"Packet count (1–3)", true},
                timeout};
    case net::Tool::mtu:
        return {target, family, {"mtu_ceiling", L"IP size ceiling (bytes)"}, timeout, deadline, source, iface};
    }
    return {};
}
HWND child(Pane &p, const wchar_t *type, const wchar_t *caption, DWORD style, int id = 0, DWORD ex = 0)
{
    auto h = CreateWindowExW(ex, type, caption, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, p.window,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(p.font), TRUE);
    return h;
}
void capture(Pane &p)
{
    if (p.loading)
        return;
    auto &fields = view(p).fields;
    for (auto &c : p.controls)
    {
        if (c.spec.check)
            fields[c.spec.key] = SendMessageW(c.control, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0";
        else if (c.spec.choices)
        {
            int selected = static_cast<int>(SendMessageW(c.control, CB_GETCURSEL, 0, 0));
            if (std::string_view(c.spec.key) == "family")
                fields[c.spec.key] = std::to_string(std::max(0, selected));
            else
                fields[c.spec.key] = utf8(text(c.control));
        }
        else
            fields[c.spec.key] = utf8(text(c.control));
    }
}
void place(Pane &p, HWND h, int x, int y, int w, int ht)
{
    MoveWindow(h, scale(p, x - p.scroll_x), scale(p, y - p.scroll_y), scale(p, std::max(1, w)),
               scale(p, std::max(1, ht)), TRUE);
}
void layout(Pane &p)
{
    RECT rect{};
    GetClientRect(p.window, &rect);
    p.viewport_width = std::max(1, MulDiv(rect.right, 96, static_cast<int>(p.dpi)));
    p.viewport_height = std::max(1, MulDiv(rect.bottom, 96, static_cast<int>(p.dpi)));
    const int width = std::max(320, p.viewport_width), height = p.viewport_height;
    int columns = width >= 900 ? 3 : width >= 520 ? 2 : 1;
    bool advanced = SendMessageW(p.advanced, BM_GETCHECK, 0, 0) == BST_CHECKED;
    int count = 0;
    for (auto &c : p.controls)
        if (!c.spec.advanced || advanced)
            ++count;
    int y = 76 + ((count + columns - 1) / columns) * 52;
    std::vector<std::pair<HWND, int>> buttons = {{p.start, 92}, {p.stop, 72}};
    if (p.tool == net::Tool::ping)
        buttons.push_back({p.pause_button, 80});
    buttons.push_back({p.copy, 104});
    buttons.push_back({p.export_button, 96});
    buttons.push_back({p.advanced, 160});
    auto position_buttons = [&](bool apply) {
        int x = 12, by = y;
        for (auto [button, w] : buttons)
        {
            if (x + w > width - 12 && x > 12)
            {
                x = 12;
                by += 34;
            }
            if (apply)
                place(p, button, x, by, w, 28);
            x += w + 8;
        }
        return by + 36;
    };
    int after_buttons = position_buttons(false);
    int navigation_height = width >= 660 ? 32 : 64;
    int result_y = after_buttons + navigation_height;
    bool graph = p.tool == net::Tool::ping;
    int table_y = result_y + 86 + (graph ? 130 : 0);
    int table_height = std::max(180, height - table_y - 12);
    p.content_width = width;
    p.content_height = table_y + table_height + 12;
    p.scroll_x = std::clamp(p.scroll_x, 0, std::max(0, p.content_width - p.viewport_width));
    p.scroll_y = std::clamp(p.scroll_y, 0, std::max(0, p.content_height - p.viewport_height));
    SCROLLINFO vertical{sizeof(vertical),
                        SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL,
                        0,
                        p.content_height - 1,
                        static_cast<UINT>(p.viewport_height),
                        p.scroll_y,
                        0};
    SetScrollInfo(p.window, SB_VERT, &vertical, TRUE);
    SCROLLINFO horizontal{sizeof(horizontal),
                          SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL,
                          0,
                          p.content_width - 1,
                          static_cast<UINT>(p.viewport_width),
                          p.scroll_x,
                          0};
    SetScrollInfo(p.window, SB_HORZ, &horizontal, TRUE);
    place(p, p.title, 12, 8, width - 24, 26);
    place(p, p.description, 12, 36, width - 24, 34);
    int cw = (width - 24 - 12 * (columns - 1)) / columns, index = 0;
    for (auto &c : p.controls)
    {
        bool shown = !c.spec.advanced || advanced;
        ShowWindow(c.control, shown ? SW_SHOW : SW_HIDE);
        ShowWindow(c.label, shown ? SW_SHOW : SW_HIDE);
        if (!shown)
            continue;
        int x = 12 + (index % columns) * (cw + 12), fy = 76 + (index / columns) * 52;
        place(p, c.label, x, fy, cw, 18);
        place(p, c.control, x, fy + 20, cw, c.spec.choices ? 240 : 26);
        ++index;
    }
    ShowWindow(p.pause_button, p.tool == net::Tool::ping ? SW_SHOW : SW_HIDE);
    position_buttons(true);
    y = after_buttons;
    if (width >= 660)
    {
        place(p, p.target, 12, y, 230, 200);
        place(p, p.previous, 250, y, 70, 26);
        place(p, p.next, 328, y, 70, 26);
        place(p, p.page_label, 410, y, width - 422, 26);
    }
    else
    {
        place(p, p.target, 12, y, width - 24, 200);
        place(p, p.previous, 12, y + 32, 70, 26);
        place(p, p.next, 90, y + 32, 70, 26);
        place(p, p.page_label, 168, y + 32, width - 180, 26);
    }
    place(p, p.summary, 12, result_y, width - 24, 78);
    ShowWindow(p.graph, graph ? SW_SHOW : SW_HIDE);
    if (graph)
        place(p, p.graph, 12, result_y + 86, width - 24, 122);
    place(p, p.list, 12, table_y, width - 24, table_height);
}
void scroll(Pane &p, int bar, int code)
{
    SCROLLINFO info{sizeof(info), SIF_ALL};
    GetScrollInfo(p.window, bar, &info);
    int &value = bar == SB_VERT ? p.scroll_y : p.scroll_x;
    int page = bar == SB_VERT ? p.viewport_height : p.viewport_width;
    switch (code)
    {
    case SB_LINEUP:
        value -= 28;
        break;
    case SB_LINEDOWN:
        value += 28;
        break;
    case SB_PAGEUP:
        value -= std::max(28, page - 28);
        break;
    case SB_PAGEDOWN:
        value += std::max(28, page - 28);
        break;
    case SB_TOP:
        value = 0;
        break;
    case SB_BOTTOM:
        value = info.nMax;
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        value = info.nTrackPos;
        break;
    default:
        return;
    }
    layout(p);
}
void focus_visible(Pane &p)
{
    HWND focused = GetFocus();
    if (!focused || focused == p.last_focus || !IsChild(p.window, focused))
        return;
    p.last_focus = focused;
    RECT rect{};
    GetWindowRect(focused, &rect);
    MapWindowPoints(nullptr, p.window, reinterpret_cast<POINT *>(&rect), 2);
    int left = MulDiv(rect.left, 96, static_cast<int>(p.dpi)), right = MulDiv(rect.right, 96, static_cast<int>(p.dpi)),
        top = MulDiv(rect.top, 96, static_cast<int>(p.dpi)), bottom = MulDiv(rect.bottom, 96, static_cast<int>(p.dpi));
    if (top < 0)
        p.scroll_y += top - 8;
    else if (bottom > p.viewport_height)
        p.scroll_y += bottom - p.viewport_height + 8;
    if (left < 0)
        p.scroll_x += left - 8;
    else if (right > p.viewport_width)
        p.scroll_x += right - p.viewport_width + 8;
    layout(p);
}
void fill_fields(Pane &p)
{
    p.loading = true;
    for (auto &c : p.controls)
    {
        DestroyWindow(c.label);
        DestroyWindow(c.control);
    }
    p.controls.clear();
    int id = 1000;
    for (auto spec : field_specs(p.tool))
    {
        FieldControl c;
        c.spec = spec;
        c.label = child(p, L"STATIC", spec.check ? L"" : spec.label, SS_LEFT);
        if (spec.check)
            c.control = child(p, L"BUTTON", spec.label, BS_AUTOCHECKBOX | WS_TABSTOP, id++);
        else if (spec.choices)
            c.control = child(p, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, id++);
        else
            c.control = child(p, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, id++, WS_EX_CLIENTEDGE);
        const auto value = view(p).fields[spec.key];
        if (spec.check)
            SendMessageW(c.control, BM_SETCHECK, value == "1" ? BST_CHECKED : BST_UNCHECKED, 0);
        else if (spec.choices)
        {
            std::wstringstream stream(spec.choices);
            std::wstring choice;
            int index = 0, selected = 0;
            while (std::getline(stream, choice, L'|'))
            {
                SendMessageW(c.control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.c_str()));
                if (utf8(choice) == value)
                    selected = index;
                ++index;
            }
            if (std::string_view(spec.key) == "family" && value.size() == 1 && value[0] >= '0' && value[0] <= '2')
                selected = value[0] - '0';
            SendMessageW(c.control, CB_SETCURSEL, selected, 0);
        }
        else
        {
            SendMessageW(c.control, EM_SETLIMITTEXT, 4096, 0);
            SetWindowTextW(c.control, wide(value).c_str());
        }
        p.controls.push_back(c);
    }
    SetWindowTextW(p.title, title(p.tool));
    const wchar_t *description =
        p.tool == net::Tool::external_ip
            ? L"Manual HTTPS checks: api.ipify.org and api6.ipify.org. No background polling."
        : p.tool == net::Tool::ping
            ? L"Select a target result below. Missing replies remain graph gaps; RTT uses integer-ms ICMP measurements."
        : p.tool == net::Tool::mtu
            ? L"IPv4 DF size evidence; IPv6 shows payload reachability. Silence does not establish an MTU."
            : L"Results are observations at a stated time. Start explicitly; Stop retains completed observations.";
    SetWindowTextW(p.description, description);
    p.loading = false;
    layout(p);
}
void refresh_cohort(Pane &p)
{
    auto &v = view(p);
    v.displayed = v.raw;
    if (p.tool != net::Tool::ping || v.raw.probes.empty())
        return;
    std::uint32_t window = v.fields["window"] == "Last 60 seconds"  ? 60000
                           : v.fields["window"] == "Last 5 minutes" ? 300000
                                                                    : 0;
    double now = v.raw.elapsed_ms;
    if (v.job && !v.job->done)
        now += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count() -
               v.raw.sampled_clock_ms;
    auto &out = v.displayed;
    out.elapsed_ms = now;
    out.probes = net::probe_cohort(v.raw.probes, now, window);
    out.rows.clear();
    size_t selected = 0;
    for (size_t i = 0; i < v.raw.probes.size() && i < v.raw.rows.size() && selected < out.probes.size(); ++i)
        if (v.raw.probes[i].sequence == out.probes[selected].sequence)
        {
            out.rows.push_back(v.raw.rows[i]);
            ++selected;
        }
    out.statistics = net::statistics(out.probes);
    auto &s = out.statistics;
    auto value = [](std::optional<double> n) {
        if (!n)
            return std::wstring(L"unavailable");
        std::wostringstream text;
        text.precision(4);
        text << *n;
        return text.str();
    };
    std::wstring state = v.job && !v.job->done                    ? v.raw.summary
                         : v.raw.status == net::Status::cancelled ? L"Run stopped; completed observations retained."
                                                                  : L"Run completed.";
    out.summary = wide(v.fields["window"]) + L": " + std::to_wstring(s.lost) + L" / " + std::to_wstring(s.completed) +
                  L" completed without Echo Reply (" +
                  (s.loss_percent ? value(s.loss_percent) + L"%" : L"unavailable") + L"); mean=" + value(s.mean) +
                  L" ms; population SD=" + value(s.population_sd) + L"; p95 nearest-rank=" + value(s.p95) +
                  L"; adjacent variation=" + value(s.variation) + L" (" + std::to_wstring(s.variation_pairs) +
                  L" pairs); not sent=" + std::to_wstring(s.not_sent) + L". " + state;
    out.context +=
        L"; cohort=" + wide(v.fields["window"]) +
        (window ? L"; actual start in (now-window,now]; now_ms=" : L"; retained actual starts <= now; now_ms=") +
        std::to_wstring(static_cast<std::uint64_t>(now));
    if (out.evicted_probes)
    {
        out.summary += L" Evicted raw observations=" + std::to_wstring(out.evicted_probes) + L". Session attempts=" +
                       std::to_wstring(out.session_statistics.attempted) + L"; exact session p95 unavailable.";
        if (window && v.raw.probes.front().start_ms > now - window)
            out.summary += L" The requested window may exceed retained coverage.";
    }
}
void update_table(Pane &p)
{
    auto &v = view(p);
    auto &result = v.displayed;
    switch (p.tool)
    {
    case net::Tool::adapters:
        grid::prioritize(result.columns, result.rows, {L"Address", L"Prefix", L"Adapter", L"Kind", L"State", L"Family"});
        break;
    case net::Tool::external_ip:
        grid::prioritize(result.columns, result.rows, {L"Observed address / error", L"State", L"Family"});
        break;
    case net::Tool::dns:
        grid::prioritize(result.columns, result.rows, {L"Data", L"Type", L"Owner", L"TTL (s)"});
        break;
    case net::Tool::routes:
        grid::prioritize(result.columns, result.rows, {L"Destination/prefix", L"Next hop", L"Interface"});
        break;
    case net::Tool::neighbours:
        grid::prioritize(result.columns, result.rows, {L"Address", L"Physical address", L"Reachability state"});
        break;
    case net::Tool::ping:
        grid::prioritize(result.columns, result.rows, {L"Responder", L"Outcome", L"RTT (integer ms)", L"Sequence"});
        break;
    case net::Tool::traceroute:
        grid::prioritize(result.columns, result.rows, {L"TTL/Hop", L"Responder", L"Outcome", L"RTT (integer ms)"});
        break;
    case net::Tool::mtu:
        grid::prioritize(result.columns, result.rows, {L"Data bytes", L"Outcome", L"Responder", L"RTT (integer ms)"});
        break;
    case net::Tool::tcp:
        grid::prioritize(result.columns, result.rows, {L"Attempted address", L"Port", L"Outcome", L"Elapsed ms"});
        break;
    case net::Tool::wake_on_lan:
        grid::prioritize(result.columns, result.rows, {L"Destination", L"Outcome", L"Port", L"Source"});
        break;
    default:
        break;
    }
    SendMessageW(p.list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(p.list);
    while (ListView_DeleteColumn(p.list, 0))
    {
    }
    for (size_t n = 0; n < result.columns.size(); ++n)
    {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<wchar_t *>(result.columns[n].c_str());
        column.cx = scale(p, n == 0 ? 160 : 140);
        ListView_InsertColumn(p.list, static_cast<int>(n), &column);
    }
    size_t pages = std::max<size_t>(1, (result.rows.size() + page_size - 1) / page_size);
    v.page = std::min(v.page, pages - 1);
    for (size_t n = v.page * page_size; n < std::min(result.rows.size(), (v.page + 1) * page_size); ++n)
    {
        auto &row = result.rows[n];
        if (row.empty())
            continue;
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(n - v.page * page_size);
        item.pszText = const_cast<wchar_t *>(row[0].c_str());
        int index = ListView_InsertItem(p.list, &item);
        for (size_t cell = 1; cell < row.size() && cell < result.columns.size(); ++cell)
            ListView_SetItemText(p.list, index, static_cast<int>(cell), const_cast<wchar_t *>(row[cell].c_str()));
    }
    grid::fit(p.list, p.dpi, result.columns);
    SendMessageW(p.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(p.list, nullptr, TRUE);
    std::wstring summary = result.observed_at + L"  " + result.context + L"\r\n" + result.summary;
    if (v.inputs_changed)
        summary = L"Inputs changed; displayed observations use the previous request. " + summary;
    if (v.job && !v.job->done)
        summary = (v.job->paused ? L"Paused — " : L"Running — ") + summary;
    SetWindowTextW(p.summary, summary.c_str());
    auto label = L"Page " + std::to_wstring(v.page + 1) + L" / " + std::to_wstring(pages) + L" · " +
                 std::to_wstring(result.rows.size()) + L" rows";
    SetWindowTextW(p.page_label, label.c_str());
    EnableWindow(p.previous, v.page > 0);
    EnableWindow(p.next, v.page + 1 < pages);
    InvalidateRect(p.graph, nullptr, TRUE);
}
void target_choices(Pane &p)
{
    auto &v = view(p);
    SendMessageW(p.target, CB_RESETCONTENT, 0, 0);
    if (v.job)
    {
        for (auto &request : v.job->requests)
            SendMessageW(p.target, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(
                             (request.target.empty() ? std::wstring(title(request.tool)) : request.target).c_str()));
        v.selected = std::min(v.selected, v.job->requests.size() - 1);
        SendMessageW(p.target, CB_SETCURSEL, v.selected, 0);
    }
    else
    {
        SendMessageW(p.target, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No observations yet"));
        SendMessageW(p.target, CB_SETCURSEL, 0, 0);
    }
}
void poll(Pane &p, bool force = false)
{
    auto &v = view(p);
    bool refresh = force;
    if (v.job)
    {
        unsigned revision = v.job->revision.load();
        if (force || revision != v.shown_revision)
        {
            std::lock_guard lock(v.job->mutex);
            if (v.selected < v.job->results.size())
                v.raw = v.job->results[v.selected];
            v.received = std::chrono::steady_clock::now();
            v.shown_revision = revision;
            refresh = true;
        }
    }
    bool running = v.job && !v.job->done;
    if (running && p.tool == net::Tool::ping && v.fields["window"] != "Session (retained)")
        refresh = true;
    if (refresh)
    {
        refresh_cohort(p);
        update_table(p);
    }
    EnableWindow(p.start, !running);
    EnableWindow(p.stop, running);
    EnableWindow(p.pause_button, running && p.tool == net::Tool::ping && !v.job->cancel);
    SetWindowTextW(p.pause_button, running && v.job->paused ? L"Resume" : L"Pause");
    SetWindowTextW(p.stop, running && v.job->cancel ? L"Stopping…" : L"Stop");
    EnableWindow(p.copy, !v.displayed.rows.empty());
    EnableWindow(p.export_button, !v.displayed.rows.empty());
    focus_visible(p);
}
std::uint32_t integer(const Fields &fields, const char *key)
{
    auto found = fields.find(key);
    if (found == fields.end())
        throw std::runtime_error(std::string("Missing field: ") + key);
    uint32_t value = 0;
    auto [end, error] = std::from_chars(found->second.data(), found->second.data() + found->second.size(), value);
    if (error != std::errc{} || end != found->second.data() + found->second.size())
        throw std::runtime_error(std::string("Enter a whole number for ") + key);
    return value;
}
net::Request request(Pane &p)
{
    capture(p);
    auto &f = view(p).fields;
    net::Request r;
    r.tool = p.tool;
    r.target = wide(f["target"]);
    r.source = wide(f["source"]);
    r.resolver = wide(f["resolver"]);
    r.dns_type = wide(f["dns_type"]);
    r.method = wide(f["method"]);
    r.mac = wide(f["mac"]);
    auto family = integer(f, "family");
    if (family > 2)
        throw std::runtime_error("Invalid address family");
    r.family = static_cast<net::Family>(family);
    r.timeout_ms = integer(f, "timeout_ms");
    r.deadline_ms = integer(f, "deadline_ms");
    r.count = integer(f, "count");
    r.interval_ms = integer(f, "interval_ms");
    r.payload_bytes = integer(f, "payload_bytes");
    r.max_hops = integer(f, "max_hops");
    r.probes_per_hop = integer(f, "probes_per_hop");
    r.rounds = integer(f, "rounds");
    r.interface_index = integer(f, "interface_index");
    r.mtu_ceiling = integer(f, "mtu_ceiling");
    r.wol_burst = integer(f, "wol_burst");
    auto port = integer(f, "port");
    if (port > 65535)
        throw std::runtime_error("Port must be between 1 and 65535");
    r.port = static_cast<uint16_t>(port);
    r.bypass_cache = f["bypass_cache"] == "1";
    r.direct = f["direct"] == "1";
    r.follow_redirects = f["follow_redirects"] == "1";
    r.continuous = f["continuous"] == "1";
    return r;
}
void start(Pane &p)
{
    auto &v = view(p);
    if (v.job && !v.job->done)
        return;
    try
    {
        auto config = request(p);
        std::vector<net::Request> requests;
        if (p.tool == net::Tool::ping)
        {
            std::wstringstream stream(config.target);
            std::wstring target;
            while (std::getline(stream, target, L';'))
            {
                while (!target.empty() && iswspace(target.front()))
                    target.erase(target.begin());
                while (!target.empty() && iswspace(target.back()))
                    target.pop_back();
                if (target.empty())
                    throw std::runtime_error("Enter a target between separators");
                auto item = config;
                item.target = target;
                requests.push_back(item);
            }
            if (requests.empty() || requests.size() > 8)
                throw std::runtime_error("Ping requires one to eight explicit targets");
        }
        else
            requests.push_back(config);
        for (auto &item : requests)
            if (auto error = net::validate(item))
            {
                MessageBoxW(p.window, error->c_str(), L"Check network options", MB_OK | MB_ICONWARNING);
                return;
            }
        auto job = std::make_shared<Job>();
        job->requests = std::move(requests);
        job->results.resize(job->requests.size());
        for (auto &result : job->results)
        {
            result.summary = L"Starting requested operation…";
        }
        v.job = job;
        v.selected = 0;
        v.page = 0;
        v.shown_revision = UINT_MAX;
        v.inputs_changed = false;
        v.raw = {};
        v.displayed = {};
        target_choices(p);
        poll(p, true);
        std::thread([job] {
            try
            {
                std::vector<std::jthread> workers;
                for (size_t i = 0; i < job->requests.size(); ++i)
                {
                    workers.emplace_back([job, i] {
                        try
                        {
                            auto progress = [job, i](const net::Result &result) {
                                std::lock_guard lock(job->mutex);
                                job->results[i] = result;
                                job->revision.fetch_add(1);
                            };
                            auto result = net::run(job->requests[i], job->cancel, progress, &job->paused);
                            {
                                std::lock_guard lock(job->mutex);
                                job->results[i] = std::move(result);
                            }
                            job->revision.fetch_add(1);
                        }
                        catch (...)
                        {
                            job->cancel = true;
                            std::lock_guard lock(job->mutex);
                            job->results[i].status = net::Status::failed;
                            job->revision.fetch_add(1);
                        }
                    });
                }
                for (auto &worker : workers)
                    worker.join();
            }
            catch (...)
            {
                std::lock_guard lock(job->mutex);
                for (auto &result : job->results)
                {
                    result.status = net::Status::failed;
                    result.summary = L"Could not start the bounded background operation.";
                }
            }
            job->done = true;
            job->revision.fetch_add(1);
        }).detach();
    }
    catch (const std::exception &error)
    {
        if (v.job)
            v.job->done = true;
        MessageBoxW(p.window, wide(error.what()).c_str(), L"Check network options", MB_OK | MB_ICONWARNING);
    }
}
std::wstring report(Pane &p, bool csv)
{
    auto &result = view(p).displayed;
    std::wstring out = result.observed_at + L"\r\n" + result.context + L"\r\n" + result.summary + L"\r\n";
    auto append = [&](const std::vector<std::wstring> &row) {
        for (size_t i = 0; i < row.size(); ++i)
        {
            if (i)
                out += csv ? L',' : L'\t';
            out += csv ? wide(storage::csv_cell(utf8(row[i]))) : row[i];
        }
        out += L"\r\n";
    };
    if (csv)
        out.clear();
    if (csv)
    {
        append({L"Observed at", result.observed_at});
        append({L"Context", result.context});
        append({L"Summary", result.summary});
        out += L"\r\n";
    }
    append(result.columns);
    for (auto &row : result.rows)
        append(row);
    return out;
}
void copy(Pane &p)
{
    auto value = report(p, false);
    if (!OpenClipboard(p.window))
    {
        MessageBoxW(p.window, L"Clipboard is busy.", L"Copy results", MB_OK | MB_ICONWARNING);
        return;
    }
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t));
    bool ok = false;
    if (memory)
    {
        auto *ptr = GlobalLock(memory);
        if (ptr)
        {
            memcpy(ptr, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(memory);
            EmptyClipboard();
            ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        }
        if (!ok)
            GlobalFree(memory);
    }
    CloseClipboard();
    if (!ok)
        MessageBoxW(p.window, L"Could not copy results.", L"Copy results", MB_OK | MB_ICONWARNING);
}
void export_csv(Pane &p)
{
    wchar_t path[32768] = L"network-observations.csv";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = p.window;
    dialog.lpstrFilter = L"CSV report (*.csv)\0*.csv\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = _countof(path);
    dialog.lpstrDefExt = L"csv";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog))
        return;
    try
    {
        storage::safe_save(path, utf8(report(p, true)));
    }
    catch (const std::exception &error)
    {
        MessageBoxW(p.window, wide(error.what()).c_str(), L"Export failed", MB_OK | MB_ICONERROR);
    }
}
LRESULT CALLBACK graph_proc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT rect{};
        GetClientRect(window, &rect);
        FillRect(dc, &rect, GetSysColorBrush(COLOR_WINDOW));
        auto *p = pane(GetParent(window));
        if (p)
        {
            auto previous_font = SelectObject(dc, p->font);
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            SetBkMode(dc, TRANSPARENT);
            auto &probes = view(*p).displayed.probes;
            RECT textrect = rect;
            textrect.left += 8;
            textrect.top += 4;
            DrawTextW(dc, L"RTT (ms) · gaps = no measured reply; table contains each outcome", -1, &textrect,
                      DT_SINGLELINE | DT_END_ELLIPSIS);
            size_t begin = probes.size() > 1000 ? probes.size() - 1000 : 0;
            double max_rtt = 1;
            for (size_t i = begin; i < probes.size(); ++i)
                if (probes[i].outcome == net::Outcome::reply && probes[i].rtt_ms)
                    max_rtt = std::max(max_rtt, *probes[i].rtt_ms);
            int left = 60, right = std::max(13, static_cast<int>(rect.right) - 12), top = 30,
                bottom = std::max(31, static_cast<int>(rect.bottom) - 16);
            HPEN pen = CreatePen(PS_SOLID, 2, GetSysColor(COLOR_HIGHLIGHT));
            auto old = SelectObject(dc, pen);
            bool connected = false;
            for (size_t i = begin; i < probes.size(); ++i)
            {
                auto &sample = probes[i];
                double span = std::max(1.0, probes.back().start_ms - probes[begin].start_ms);
                int x = left + static_cast<int>((right - left) * (sample.start_ms - probes[begin].start_ms) / span);
                if (sample.outcome == net::Outcome::reply && sample.rtt_ms)
                {
                    int y = bottom - static_cast<int>((bottom - top) * (*sample.rtt_ms / max_rtt));
                    if (connected)
                        LineTo(dc, x, y);
                    else
                        MoveToEx(dc, x, y, nullptr);
                    Ellipse(dc, x - 2, y - 2, x + 3, y + 3);
                    MoveToEx(dc, x, y, nullptr);
                    connected = true;
                }
                else
                {
                    connected = false;
                    MoveToEx(dc, x - 2, bottom - 2, nullptr);
                    LineTo(dc, x + 2, bottom + 2);
                    MoveToEx(dc, x - 2, bottom + 2, nullptr);
                    LineTo(dc, x + 2, bottom - 2);
                }
            }
            SelectObject(dc, old);
            DeleteObject(pen);
            auto maximum = std::to_wstring(static_cast<unsigned long long>(std::ceil(max_rtt))) + L" ms";
            TextOutW(dc, 4, top, maximum.c_str(), static_cast<int>(maximum.size()));
            TextOutW(dc, 4, bottom - 12, L"0 ms", 4);
            if (!probes.empty())
            {
                auto duration =
                    L"Start-time span: " +
                    std::to_wstring(static_cast<unsigned long long>(probes.back().start_ms - probes[begin].start_ms)) +
                    L" ms";
                TextOutW(dc, left, bottom + 1, duration.c_str(), static_cast<int>(duration.size()));
            }
            SelectObject(dc, previous_font);
            if (begin)
            {
                RECT note = rect;
                note.top = rect.bottom - 16;
                DrawTextW(dc, L"Graph: latest 1,000 probes; table and statistics retain the full bounded run", -1,
                          &note, DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        }
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l)
{
    auto *p = pane(window);
    try
    {
        switch (message)
        {
        case WM_CREATE: {
            auto state = std::make_unique<Pane>();
            state->window = window;
            state->dpi = 96;
            auto getdpi = reinterpret_cast<UINT(WINAPI *)(HWND)>(
                GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
            if (getdpi)
                state->dpi = getdpi(window);
            NONCLIENTMETRICSW metrics{};
            metrics.cbSize = sizeof(metrics);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
            state->font = CreateFontIndirectW(&metrics.lfMessageFont);
            p = state.get();
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
            for (size_t i = 0; i < p->views.size(); ++i)
                p->views[i].fields = defaults(static_cast<net::Tool>(i));
            p->title = child(*p, L"STATIC", L"", SS_LEFT);
            p->description = child(*p, L"STATIC", L"", SS_LEFT);
            p->start = child(*p, L"BUTTON", L"&Start", BS_PUSHBUTTON | WS_TABSTOP, start_id);
            p->stop = child(*p, L"BUTTON", L"S&top", BS_PUSHBUTTON | WS_TABSTOP, stop_id);
            p->pause_button = child(*p, L"BUTTON", L"Pause", BS_PUSHBUTTON | WS_TABSTOP, pause_id);
            p->copy = child(*p, L"BUTTON", L"&Copy results", BS_PUSHBUTTON | WS_TABSTOP, copy_id);
            p->export_button = child(*p, L"BUTTON", L"Export &CSV", BS_PUSHBUTTON | WS_TABSTOP, export_id);
            p->advanced = child(*p, L"BUTTON", L"Advanced options", BS_AUTOCHECKBOX | WS_TABSTOP, advanced_id);
            p->target = child(*p, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, target_id);
            p->previous = child(*p, L"BUTTON", L"Previous", BS_PUSHBUTTON | WS_TABSTOP, previous_id);
            p->next = child(*p, L"BUTTON", L"Next", BS_PUSHBUTTON | WS_TABSTOP, next_id);
            p->page_label = child(*p, L"STATIC", L"", SS_LEFT);
            p->summary = child(*p, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP,
                               0, WS_EX_CLIENTEDGE);
            p->list = child(*p, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SHOWSELALWAYS | WS_TABSTOP, 0, WS_EX_CLIENTEDGE);
            ListView_SetExtendedListViewStyle(p->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
            p->graph = child(*p, graph_class, L"Ping graph", 0);
            fill_fields(*p);
            target_choices(*p);
            poll(*p, true);
            SetTimer(window, poll_timer, 200, nullptr);
            state.release();
            return 0;
        }
        case WM_DPICHANGED:
            if (p)
                network_set_dpi(window, HIWORD(w));
            return 0;
        case WM_SIZE:
            if (p)
            {
                layout(*p);
                grid::fit(p->list, p->dpi, view(*p).displayed.columns);
            }
            return 0;
        case WM_TIMER:
            if (p && w == poll_timer)
                poll(*p);
            return 0;
        case WM_VSCROLL:
            if (p)
                scroll(*p, SB_VERT, LOWORD(w));
            return 0;
        case WM_HSCROLL:
            if (p)
                scroll(*p, SB_HORZ, LOWORD(w));
            return 0;
        case WM_MOUSEWHEEL:
            if (p)
            {
                p->scroll_y -= GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 84;
                layout(*p);
            }
            return 0;
        case WM_COMMAND:
            if (!p)
                break;
            switch (LOWORD(w))
            {
            case start_id:
                start(*p);
                return 0;
            case stop_id:
                if (view(*p).job)
                    view(*p).job->cancel = true;
                poll(*p);
                return 0;
            case pause_id:
                if (view(*p).job)
                {
                    auto &paused = view(*p).job->paused;
                    paused = !paused.load();
                    poll(*p);
                }
                return 0;
            case copy_id:
                copy(*p);
                return 0;
            case export_id:
                export_csv(*p);
                return 0;
            case advanced_id:
                layout(*p);
                return 0;
            case previous_id:
                if (view(*p).page)
                    --view(*p).page;
                update_table(*p);
                return 0;
            case next_id:
                ++view(*p).page;
                update_table(*p);
                return 0;
            case target_id:
                if (HIWORD(w) == CBN_SELCHANGE)
                {
                    view(*p).selected =
                        static_cast<size_t>(std::max<LRESULT>(0, SendMessageW(p->target, CB_GETCURSEL, 0, 0)));
                    view(*p).page = 0;
                    poll(*p, true);
                }
                return 0;
            }
            if (LOWORD(w) >= 1000 && !p->loading &&
                (HIWORD(w) == EN_CHANGE || HIWORD(w) == CBN_SELCHANGE || HIWORD(w) == BN_CLICKED))
            {
                capture(*p);
                size_t field = LOWORD(w) - 1000;
                if (field >= p->controls.size() || std::string_view(p->controls[field].spec.key) != "window")
                    view(*p).inputs_changed = true;
                refresh_cohort(*p);
                update_table(*p);
                PostMessageW(GetParent(window), WM_APP + 10, 0, 0);
            }
            return 0;
        case WM_HELP:
            if (p)
                show_help(window, help_topic(p->tool));
            return TRUE;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
            SetTextColor(reinterpret_cast<HDC>(w), GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(reinterpret_cast<HDC>(w), GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        case WM_DESTROY:
            if (p)
            {
                KillTimer(window, poll_timer);
                for (auto &v : p->views)
                    if (v.job)
                        v.job->cancel = true;
            }
            return 0;
        case WM_NCDESTROY:
            if (p)
            {
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                if (p->font)
                    DeleteObject(p->font);
                delete p;
            }
            return DefWindowProcW(window, message, w, l);
        }
    }
    catch (const std::exception &error)
    {
        if (p)
            SetWindowTextW(p->summary, wide(error.what()).c_str());
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
} // namespace
HWND create_network_pane(HWND parent)
{
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    cls.lpfnWndProc = procedure;
    cls.lpszClassName = class_name;
    RegisterClassExW(&cls);
    cls.lpfnWndProc = graph_proc;
    cls.lpszClassName = graph_class;
    RegisterClassExW(&cls);
    return CreateWindowExW(WS_EX_CONTROLPARENT, class_name, L"Network tools",
                           WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL, 0, 0, 100, 100, parent, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
}
void select_network_tool(HWND window, net::Tool tool)
{
    auto *p = pane(window);
    if (!p || static_cast<size_t>(tool) >= p->views.size())
        return;
    capture(*p);
    p->tool = tool;
    p->scroll_x = 0;
    p->scroll_y = 0;
    p->last_focus = nullptr;
    fill_fields(*p);
    target_choices(*p);
    poll(*p, true);
}
void network_set_dpi(HWND window, unsigned dpi)
{
    auto *p = pane(window);
    if (!p || dpi < 48 || dpi > 768)
        return;
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    HDC screen = GetDC(nullptr);
    int system_dpi = screen ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
    if (screen)
        ReleaseDC(nullptr, screen);
    metrics.lfMessageFont.lfHeight = MulDiv(metrics.lfMessageFont.lfHeight, static_cast<int>(dpi), system_dpi);
    HFONT font = CreateFontIndirectW(&metrics.lfMessageFont);
    if (font)
    {
        auto old = p->font;
        p->font = font;
        EnumChildWindows(
            window,
            [](HWND child, LPARAM value) -> BOOL {
                SendMessageW(child, WM_SETFONT, value, TRUE);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(font));
        if (old)
            DeleteObject(old);
    }
    p->dpi = dpi;
    layout(*p);
    update_table(*p);
}
bool network_busy(HWND window)
{
    auto *p = pane(window);
    if (!p)
        return false;
    return std::any_of(p->views.begin(), p->views.end(), [](const View &v) { return v.job && !v.job->done; });
}
void stop_network(HWND window)
{
    if (auto *p = pane(window))
        for (auto &v : p->views)
            if (v.job)
                v.job->cancel = true;
}
std::map<std::string, std::string> network_plan_fields(HWND window)
{
    std::map<std::string, std::string> result;
    if (auto *p = pane(window))
    {
        capture(*p);
        for (size_t i = 0; i < p->views.size(); ++i)
            for (auto &[key, value] : p->views[i].fields)
                result["network." + std::to_string(i) + "." + key] = value;
    }
    return result;
}
void load_network_plan_fields(HWND window, const std::map<std::string, std::string> &fields)
{
    if (auto *p = pane(window))
    {
        for (size_t i = 0; i < p->views.size(); ++i)
        {
            auto defaults_map = defaults(static_cast<net::Tool>(i));
            for (auto &[key, value] : defaults_map)
            {
                auto found = fields.find("network." + std::to_string(i) + "." + key);
                if (found != fields.end() && found->second.size() <= 4096)
                    value = found->second;
            }
            p->views[i].fields = std::move(defaults_map);
            p->views[i].inputs_changed = true;
        }
        fill_fields(*p);
    }
}
} // namespace veu
