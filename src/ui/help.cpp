#include "veu/help.hpp"

#include "../../resources/resource.h"

#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <shellapi.h>
#include <windowsx.h>
#include <winver.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Version.lib")

namespace veu {
namespace {

constexpr wchar_t kHelpClass[] = L"VEU.VelocityNetTools.Help";
constexpr wchar_t kAboutClass[] = L"VEU.VelocityNetTools.About";
constexpr UINT kIndexResource = IDR_HELP_INDEX;
constexpr UINT kLicenceResource = IDR_LICENSE;

enum ControlId : int {
    kBack = 100, kForward, kContents, kSearch, kTree, kHits, kArticle,
    kStatus, kCopy, kPrint, kClose, kAboutCopy, kAboutLicence, kAboutHelp,
    kAboutClose, kAboutLinks,
};

struct Topic {
    UINT resource_id{};
    std::wstring id;
    std::wstring title;
    std::wstring keywords;
    std::wstring text;
};

struct ResourceView {
    const std::byte* data{};
    DWORD size{};
};

struct VersionDetails {
    std::wstring version = L"0.1.0-dev";
    std::wstring build = L"local";
};

HWND g_help_window{};
HWND g_about_window{};
HMODULE g_richedit_module{};
unsigned g_help_window_count{};

int scaled(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), 96);
}

UINT window_dpi(HWND window) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto get_dpi_for_window = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (get_dpi_for_window) {
        const UINT dpi = get_dpi_for_window(window);
        if (dpi != 0) return dpi;
    }
    HDC dc = GetDC(window);
    const UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96;
    if (dc) ReleaseDC(window, dc);
    return dpi ? dpi : 96;
}

std::optional<ResourceView> resource_view(UINT id) {
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource) return std::nullopt;
    HGLOBAL loaded = LoadResource(module, resource);
    const DWORD size = SizeofResource(module, resource);
    const void* data = loaded ? LockResource(loaded) : nullptr;
    if (!data || size == 0) return std::nullopt;
    return ResourceView{static_cast<const std::byte*>(data), size};
}

bool read_u32(ResourceView view, std::size_t& position, std::uint32_t& value) {
    if (position > view.size || view.size - position < sizeof(value)) return false;
    std::memcpy(&value, view.data + position, sizeof(value));
    position += sizeof(value);
    return true;
}

bool utf8_to_wide(const std::byte* data, std::uint32_t size, std::wstring& result) {
    if (size == 0) {
        result.clear();
        return true;
    }
    if (size > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) return false;
    const char* bytes = reinterpret_cast<const char*>(data);
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, bytes, static_cast<int>(size), nullptr, 0);
    if (required <= 0) return false;
    result.resize(static_cast<std::size_t>(required));
    return MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, bytes, static_cast<int>(size),
        result.data(), required) == required;
}

bool read_utf8(ResourceView view, std::size_t& position, std::uint32_t length,
               std::wstring& value) {
    if (position > view.size || view.size - position < length) return false;
    if (!utf8_to_wide(view.data + position, length, value)) return false;
    position += length;
    return true;
}

bool load_topics(std::vector<Topic>& topics) {
    const auto resource = resource_view(kIndexResource);
    if (!resource || resource->size < 12) return false;
    constexpr unsigned char signature[] = {'V', 'E', 'U', 'H', 'L', 'P', '1', 0};
    if (std::memcmp(resource->data, signature, sizeof(signature)) != 0) return false;
    std::size_t position = sizeof(signature);
    std::uint32_t count{};
    if (!read_u32(*resource, position, count) || count == 0 || count > 512) return false;
    topics.clear();
    topics.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        Topic topic;
        std::uint32_t lengths[4]{};
        if (!read_u32(*resource, position, topic.resource_id)) return false;
        for (auto& length : lengths) {
            if (!read_u32(*resource, position, length) ||
                length > 16U * 1024U * 1024U) return false;
        }
        if (!read_utf8(*resource, position, lengths[0], topic.id) ||
            !read_utf8(*resource, position, lengths[1], topic.title) ||
            !read_utf8(*resource, position, lengths[2], topic.keywords) ||
            !read_utf8(*resource, position, lengths[3], topic.text)) return false;
        topics.push_back(std::move(topic));
    }
    return position == resource->size;
}

std::wstring lower(std::wstring_view value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(),
        [](wchar_t character) {
            return static_cast<wchar_t>(std::towlower(character));
        });
    return result;
}

bool is_https(std::wstring_view target) {
    constexpr std::wstring_view scheme = L"https://";
    if (target.size() <= scheme.size() ||
        lower(target.substr(0, scheme.size())) != scheme) return false;
    const std::wstring_view authority = target.substr(scheme.size());
    return authority.find_first_of(L"/?#") != 0 &&
           authority.find(L'@') == std::wstring_view::npos &&
           authority.find_first_of(L" \t\r\n") == std::wstring_view::npos;
}

void open_https(HWND owner, std::wstring_view target) {
    if (!is_https(target)) {
        MessageBoxW(owner, L"Only plain HTTPS links can be opened from help.",
                    L"Velocity NetTools", MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring url(target);
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.hwnd = owner;
    info.lpVerb = L"open";
    info.lpFile = url.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        MessageBoxW(owner, L"Windows could not open the selected online resource.",
                    L"Velocity NetTools", MB_OK | MB_ICONWARNING);
    }
}

bool copy_text(HWND owner, std::wstring_view text) {
    if (!OpenClipboard(owner)) {
        MessageBoxW(owner, L"The clipboard is busy. Try Copy again.",
                    L"Velocity NetTools", MB_OK | MB_ICONWARNING);
        return false;
    }
    bool success = false;
    if (EmptyClipboard()) {
        const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory) {
            void* destination = GlobalLock(memory);
            if (destination) {
                std::memcpy(destination, text.data(),
                            text.size() * sizeof(wchar_t));
                static_cast<wchar_t*>(destination)[text.size()] = L'\0';
                GlobalUnlock(memory);
                if (SetClipboardData(CF_UNICODETEXT, memory)) {
                    success = true;
                    memory = nullptr;
                }
            }
            if (memory) GlobalFree(memory);
        }
    }
    CloseClipboard();
    if (!success) {
        MessageBoxW(owner, L"Velocity NetTools could not copy the text.",
                    L"Velocity NetTools", MB_OK | MB_ICONWARNING);
    }
    return success;
}

std::wstring window_text(HWND window) {
    const int length = GetWindowTextLengthW(window);
    std::wstring result(static_cast<std::size_t>(std::max(length, 0)) + 1, L'\0');
    if (length > 0) GetWindowTextW(window, result.data(), length + 1);
    result.resize(static_cast<std::size_t>(std::max(length, 0)));
    return result;
}

VersionDetails version_details() {
    VersionDetails details;
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return details;
    path.resize(length);
    DWORD ignored{};
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (size == 0) return details;
    std::vector<std::byte> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return details;
    auto query = [&](const wchar_t* name, std::wstring& output) {
        void* value{};
        UINT value_length{};
        std::wstring key = L"\\StringFileInfo\\040904B0\\";
        key += name;
        if (VerQueryValueW(data.data(), key.c_str(), &value, &value_length) &&
            value && value_length > 1) {
            output.assign(static_cast<const wchar_t*>(value), value_length - 1);
        }
    };
    query(L"ProductVersion", details.version);
    query(L"BuildId", details.build);
    return details;
}

bool acquire_richedit() {
    if (!g_richedit_module) {
        g_richedit_module = LoadLibraryExW(
            L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (!g_richedit_module) return false;
    ++g_help_window_count;
    return true;
}

void release_richedit() {
    if (g_help_window_count > 0) --g_help_window_count;
    if (g_help_window_count == 0 && g_richedit_module) {
        FreeLibrary(g_richedit_module);
        g_richedit_module = nullptr;
    }
}

void set_font(HWND window, HFONT font) {
    if (window) SendMessageW(window, WM_SETFONT,
                             reinterpret_cast<WPARAM>(font), TRUE);
}

struct HelpState {
    HWND window{}, back{}, forward{}, contents{}, search{}, tree{}, hits{};
    HWND article{}, status{}, copy{}, print{}, close{};
    HFONT font{};
    UINT dpi{96};
    bool suppress_tree{};
    std::vector<Topic> topics;
    std::vector<std::size_t> history;
    std::size_t history_position{}, current{};
    std::wstring pending_context;

    ~HelpState() {
        if (font) DeleteObject(font);
    }

    void create_children();
    void update_font();
    void layout();
    void populate_tree();
    void navigate(std::size_t topic, bool add_history, bool move_focus);
    void search_changed();
    void show_contents();
    void copy_article();
    void print_article();
    void follow_link(const ENLINK& link);
    void apply_context(std::wstring_view context);
    void update_buttons();
};

LRESULT CALLBACK help_control_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR reference) {
    auto* state = reinterpret_cast<HelpState*>(reference);
    if (message == WM_KEYDOWN) {
        if ((GetKeyState(VK_CONTROL) & 0x8000) &&
            (wparam == 'F' || wparam == 'f')) {
            SetFocus(state->search);
            SendMessageW(state->search, EM_SETSEL, 0, -1);
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) &&
            (wparam == 'P' || wparam == 'p')) {
            state->print_article();
            return 0;
        }
        if ((GetKeyState(VK_MENU) & 0x8000) && wparam == VK_LEFT &&
            state->history_position > 0) {
            --state->history_position;
            state->navigate(
                state->history[state->history_position], false, true);
            return 0;
        }
        if ((GetKeyState(VK_MENU) & 0x8000) && wparam == VK_RIGHT &&
            state->history_position + 1 < state->history.size()) {
            ++state->history_position;
            state->navigate(
                state->history[state->history_position], false, true);
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            DestroyWindow(state->window);
            return 0;
        }
        if (wparam == VK_TAB) {
            const BOOL previous = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            HWND next = GetNextDlgTabItem(state->window, window, previous);
            if (next) SetFocus(next);
            return 0;
        }
        if (window == state->hits && wparam == VK_RETURN) {
            const int row = ListView_GetNextItem(
                state->hits, -1, LVNI_SELECTED);
            if (row >= 0) {
                LVITEMW item{};
                item.mask = LVIF_PARAM;
                item.iItem = row;
                if (ListView_GetItem(state->hits, &item)) {
                    state->navigate(
                        static_cast<std::size_t>(item.lParam), true, true);
                }
            }
            return 0;
        }
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

void HelpState::update_font() {
    if (font) DeleteObject(font);
    font = CreateFontW(
        -scaled(15, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    for (HWND child : {back, forward, contents, search, tree, hits, article,
                       status, copy, print, close}) {
        set_font(child, font);
    }
}

void HelpState::create_children() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    auto child = [&](const wchar_t* klass, const wchar_t* text,
                     DWORD style, int id) {
        DWORD child_style = WS_CHILD | WS_VISIBLE | style;
        if (id != kStatus) child_style |= WS_TABSTOP;
        HWND control = CreateWindowExW(
            0, klass, text, child_style,
            0, 0, 1, 1, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance, nullptr);
        if (control) {
            SetWindowSubclass(control, help_control_proc, 1,
                              reinterpret_cast<DWORD_PTR>(this));
        }
        return control;
    };
    back = child(WC_BUTTONW, L"Back", BS_PUSHBUTTON, kBack);
    forward = child(WC_BUTTONW, L"Forward", BS_PUSHBUTTON, kForward);
    contents = child(WC_BUTTONW, L"Contents", BS_PUSHBUTTON, kContents);
    search = child(WC_EDITW, L"", WS_BORDER | ES_AUTOHSCROLL, kSearch);
    tree = child(
        WC_TREEVIEWW, L"",
        WS_BORDER | TVS_HASBUTTONS | TVS_HASLINES |
            TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        kTree);
    hits = child(
        WC_LISTVIEWW, L"",
        WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        kHits);
    article = child(
        MSFTEDIT_CLASS, L"",
        WS_BORDER | ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL | WS_VSCROLL,
        kArticle);
    status = child(WC_STATICW, L"", SS_LEFT | SS_CENTERIMAGE, kStatus);
    copy = child(WC_BUTTONW, L"Copy", BS_PUSHBUTTON, kCopy);
    print = child(WC_BUTTONW, L"Print...", BS_PUSHBUTTON, kPrint);
    close = child(WC_BUTTONW, L"Close", BS_DEFPUSHBUTTON, kClose);
    if (hits) {
        ListView_SetExtendedListViewStyle(
            hits, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<wchar_t*>(L"Search results");
        column.cx = scaled(280, dpi);
        ListView_InsertColumn(hits, 0, &column);
        ShowWindow(hits, SW_HIDE);
    }
    if (article) {
        SendMessageW(article, EM_SETREADONLY, TRUE, 0);
        SendMessageW(article, EM_AUTOURLDETECT, TRUE, 0);
        const LRESULT mask = SendMessageW(article, EM_GETEVENTMASK, 0, 0);
        SendMessageW(article, EM_SETEVENTMASK, 0, mask | ENM_LINK);
    }
    populate_tree();
    update_font();
}

void HelpState::populate_tree() {
    TVINSERTSTRUCTW item{};
    item.hParent = TVI_ROOT;
    item.hInsertAfter = TVI_LAST;
    item.item.mask = TVIF_TEXT | TVIF_PARAM;
    item.item.pszText = const_cast<wchar_t*>(L"Contents");
    item.item.lParam = -1;
    HTREEITEM root = TreeView_InsertItem(tree, &item);
    for (std::size_t index = 0; index < topics.size(); ++index) {
        item.hParent = root;
        item.item.pszText = topics[index].title.data();
        item.item.lParam = static_cast<LPARAM>(index);
        TreeView_InsertItem(tree, &item);
    }
    TreeView_Expand(tree, root, TVE_EXPAND);
}

struct StreamState {
    const std::byte* data;
    std::size_t remaining;
};

DWORD CALLBACK stream_in(
    DWORD_PTR cookie, LPBYTE buffer, LONG requested, LONG* copied) {
    auto* stream = reinterpret_cast<StreamState*>(cookie);
    const std::size_t count = std::min(
        stream->remaining,
        static_cast<std::size_t>(std::max(requested, 0L)));
    if (count) {
        std::memcpy(buffer, stream->data, count);
        stream->data += count;
        stream->remaining -= count;
    }
    *copied = static_cast<LONG>(count);
    return 0;
}

void HelpState::navigate(
    std::size_t topic_index, bool add_history, bool move_focus) {
    if (topic_index >= topics.size()) return;
    const auto resource = resource_view(topics[topic_index].resource_id);
    if (!resource) {
        SetWindowTextW(
            article,
            L"Embedded topic content is unavailable. Regenerate resources "
            L"and rebuild Velocity NetTools.");
    } else {
        StreamState stream{resource->data, resource->size};
        EDITSTREAM edit_stream{};
        edit_stream.dwCookie = reinterpret_cast<DWORD_PTR>(&stream);
        edit_stream.pfnCallback = stream_in;
        SendMessageW(article, WM_SETREDRAW, FALSE, 0);
        SendMessageW(
            article, EM_STREAMIN, SF_RTF,
            reinterpret_cast<LPARAM>(&edit_stream));
        SendMessageW(article, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(article, nullptr, TRUE);
        if (edit_stream.dwError) {
            SetWindowTextW(
                article, L"Windows could not load this embedded help topic.");
        }
    }
    current = topic_index;
    if (add_history) {
        if (!history.empty() && history_position + 1 < history.size()) {
            history.erase(
                history.begin() +
                    static_cast<std::ptrdiff_t>(history_position + 1),
                history.end());
        }
        if (history.empty() || history.back() != topic_index) {
            history.push_back(topic_index);
        }
        history_position = history.size() - 1;
    }
    suppress_tree = true;
    HTREEITEM root = TreeView_GetRoot(tree);
    for (HTREEITEM item = root ? TreeView_GetChild(tree, root) : nullptr;
         item; item = TreeView_GetNextSibling(tree, item)) {
        TVITEMW info{};
        info.mask = TVIF_PARAM;
        info.hItem = item;
        if (TreeView_GetItem(tree, &info) &&
            static_cast<std::size_t>(info.lParam) == topic_index) {
            TreeView_SelectItem(tree, item);
            TreeView_EnsureVisible(tree, item);
            break;
        }
    }
    suppress_tree = false;
    const VersionDetails version = version_details();
    std::wstring status_text =
        topics[topic_index].title +
        L"    Manual for Velocity NetTools " + version.version;
    SetWindowTextW(status, status_text.c_str());
    update_buttons();
    SendMessageW(article, EM_SETSEL, 0, 0);
    SendMessageW(article, EM_SCROLLCARET, 0, 0);
    if (move_focus) SetFocus(article);
}

void HelpState::update_buttons() {
    EnableWindow(back, history_position > 0);
    EnableWindow(
        forward,
        !history.empty() && history_position + 1 < history.size());
}

void HelpState::show_contents() {
    SetWindowTextW(search, L"");
    ShowWindow(hits, SW_HIDE);
    ShowWindow(tree, SW_SHOW);
    SetFocus(tree);
}

std::wstring excerpt_for(
    const Topic& topic, std::wstring_view query) {
    std::wstring text = topic.text;
    std::replace(text.begin(), text.end(), L'\n', L' ');
    const std::wstring folded = lower(text);
    const std::size_t match = folded.find(query);
    const std::size_t start =
        match == std::wstring::npos || match < 35 ? 0 : match - 35;
    std::wstring excerpt = text.substr(start, 105);
    if (start) excerpt.insert(0, L"...");
    if (start + 105 < text.size()) excerpt += L"...";
    return excerpt;
}

void HelpState::search_changed() {
    const std::wstring entered = window_text(search);
    const std::wstring query = lower(entered);
    ListView_DeleteAllItems(hits);
    if (query.empty()) {
        ShowWindow(hits, SW_HIDE);
        ShowWindow(tree, SW_SHOW);
        return;
    }
    ShowWindow(tree, SW_HIDE);
    ShowWindow(hits, SW_SHOW);
    int row = 0;
    for (std::size_t index = 0; index < topics.size(); ++index) {
        const Topic& topic = topics[index];
        const std::wstring haystack = lower(
            topic.title + L" " + topic.keywords + L" " + topic.text);
        if (haystack.find(query) == std::wstring::npos) continue;
        std::wstring label =
            topic.title + L" — " + excerpt_for(topic, query);
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = row++;
        item.pszText = label.data();
        item.lParam = static_cast<LPARAM>(index);
        ListView_InsertItem(hits, &item);
    }
    if (row == 0) {
        std::wstring message =
            L"No offline help topics match “" + entered +
            L"”. Try a tool name, command, formula, address family, "
            L"or error term.";
        SetWindowTextW(article, message.c_str());
        SetWindowTextW(status, L"No search results");
    }
    ListView_SetColumnWidth(hits, 0, LVSCW_AUTOSIZE_USEHEADER);
}

void HelpState::copy_article() {
    copy_text(window, window_text(article));
}

void HelpState::print_article() {
    PRINTDLGW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.Flags =
        PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION |
        PD_USEDEVMODECOPIESANDCOLLATE;
    if (!PrintDlgW(&dialog)) return;

    DOCINFOW document{sizeof(document)};
    document.lpszDocName =
        topics.empty() ? L"Velocity NetTools Help" :
                         topics[current].title.c_str();
    bool failed = StartDocW(dialog.hDC, &document) <= 0;
    LONG next = 0;
    const LONG length = GetWindowTextLengthW(article);
    const int dpi_x = GetDeviceCaps(dialog.hDC, LOGPIXELSX);
    const int dpi_y = GetDeviceCaps(dialog.hDC, LOGPIXELSY);
    FORMATRANGE format{};
    format.hdc = dialog.hDC;
    format.hdcTarget = dialog.hDC;
    format.rc.left = MulDiv(
        GetDeviceCaps(dialog.hDC, PHYSICALOFFSETX), 1440, dpi_x);
    format.rc.top = MulDiv(
        GetDeviceCaps(dialog.hDC, PHYSICALOFFSETY), 1440, dpi_y);
    format.rc.right = format.rc.left + MulDiv(
        GetDeviceCaps(dialog.hDC, HORZRES), 1440, dpi_x);
    format.rc.bottom = format.rc.top + MulDiv(
        GetDeviceCaps(dialog.hDC, VERTRES), 1440, dpi_y);
    format.rcPage.left = 0;
    format.rcPage.top = 0;
    format.rcPage.right = MulDiv(
        GetDeviceCaps(dialog.hDC, PHYSICALWIDTH), 1440, dpi_x);
    format.rcPage.bottom = MulDiv(
        GetDeviceCaps(dialog.hDC, PHYSICALHEIGHT), 1440, dpi_y);
    while (!failed && next < length) {
        if (StartPage(dialog.hDC) <= 0) {
            failed = true;
            break;
        }
        format.chrg.cpMin = next;
        format.chrg.cpMax = -1;
        const LONG rendered = static_cast<LONG>(SendMessageW(
            article, EM_FORMATRANGE, TRUE,
            reinterpret_cast<LPARAM>(&format)));
        if (rendered <= next || EndPage(dialog.hDC) <= 0) {
            failed = true;
            break;
        }
        next = rendered;
    }
    SendMessageW(article, EM_FORMATRANGE, FALSE, 0);
    if (failed) {
        AbortDoc(dialog.hDC);
        MessageBoxW(
            window, L"Windows could not print this help topic.",
            L"Velocity NetTools", MB_OK | MB_ICONWARNING);
    } else {
        EndDoc(dialog.hDC);
    }
    DeleteDC(dialog.hDC);
    if (dialog.hDevMode) GlobalFree(dialog.hDevMode);
    if (dialog.hDevNames) GlobalFree(dialog.hDevNames);
}

void HelpState::follow_link(const ENLINK& link) {
    if (link.msg != WM_LBUTTONUP && link.msg != WM_KEYUP) return;
    const LONG length = link.chrg.cpMax - link.chrg.cpMin;
    if (length <= 0 || length > 2048) return;
    std::wstring target(static_cast<std::size_t>(length) + 1, L'\0');
    TEXTRANGEW range{};
    range.chrg = link.chrg;
    range.lpstrText = target.data();
    const LRESULT copied = SendMessageW(
        article, EM_GETTEXTRANGE, 0,
        reinterpret_cast<LPARAM>(&range));
    if (copied > 0) {
        target.resize(static_cast<std::size_t>(copied));
        open_https(window, target);
    }
}

void HelpState::apply_context(std::wstring_view context) {
    if (topics.empty()) return;
    if (context.empty()) {
        navigate(0, true, true);
        return;
    }
    const std::wstring sought = lower(context);
    for (std::size_t index = 0; index < topics.size(); ++index) {
        if (lower(topics[index].id) == sought) {
            navigate(index, true, true);
            return;
        }
    }
    for (std::size_t index = 0; index < topics.size(); ++index) {
        const std::wstring searchable = lower(
            topics[index].id + L" " + topics[index].title +
            L" " + topics[index].keywords);
        if (searchable.find(sought) != std::wstring::npos) {
            navigate(index, true, true);
            return;
        }
    }
    navigate(0, true, true);
}

void HelpState::layout() {
    RECT client{};
    GetClientRect(window, &client);
    const int pad = scaled(8, dpi);
    const int gap = scaled(6, dpi);
    const int button_width = scaled(78, dpi);
    const int toolbar_height = scaled(30, dpi);
    const int footer_height = scaled(30, dpi);
    int x = pad;
    for (HWND button : {back, forward, contents}) {
        MoveWindow(button, x, pad, button_width, toolbar_height, TRUE);
        x += button_width + gap;
    }
    const int search_left = x + scaled(58, dpi);
    MoveWindow(
        search, search_left, pad,
        std::max(scaled(120, dpi), static_cast<int>(client.right) - pad - search_left),
        toolbar_height, TRUE);

    const int body_top = pad + toolbar_height + gap;
    const int footer_top = std::max(
        body_top + scaled(100, dpi),
        static_cast<int>(client.bottom) - pad - footer_height);
    const int left_width = std::clamp(
        scaled(300, dpi), scaled(180, dpi),
        std::max(scaled(180, dpi), static_cast<int>(client.right) / 3));
    MoveWindow(
        tree, pad, body_top, left_width,
        footer_top - body_top - gap, TRUE);
    MoveWindow(
        hits, pad, body_top, left_width,
        footer_top - body_top - gap, TRUE);
    ListView_SetColumnWidth(
        hits, 0, std::max(80, left_width - scaled(8, dpi)));
    MoveWindow(
        article, pad + left_width + gap, body_top,
        std::max(1, static_cast<int>(client.right) - (pad * 2 + left_width + gap)),
        std::max(1, footer_top - body_top - gap), TRUE);
    int right = client.right - pad;
    for (HWND button : {close, print, copy}) {
        right -= button_width;
        MoveWindow(
            button, right, footer_top,
            button_width, footer_height, TRUE);
        right -= gap;
    }
    MoveWindow(
        status, pad, footer_top, std::max(1, right - pad),
        footer_height, TRUE);
}

LRESULT CALLBACK help_window_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<HelpState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create =
            reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<HelpState*>(create->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(
            window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }
    if (!state) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_CREATE:
        state->dpi = window_dpi(window);
        state->create_children();
        if (!state->back || !state->forward || !state->contents ||
            !state->search || !state->tree || !state->hits ||
            !state->article || !state->status || !state->copy ||
            !state->print || !state->close) return -1;
        state->apply_context(state->pending_context);
        return 0;
    case WM_SIZE:
        state->layout();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        HFONT old = static_cast<HFONT>(
            SelectObject(dc, state->font));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        const int x = scaled(8 + 3 * (78 + 6) + 6, state->dpi);
        const int y = scaled(15, state->dpi);
        TextOutW(dc, x, y, L"Search:", 7);
        SelectObject(dc, old);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DPICHANGED: {
        state->dpi = HIWORD(wparam);
        const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(
            window, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOACTIVATE | SWP_NOZORDER);
        state->update_font();
        state->layout();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        limits->ptMinTrackSize = {
            scaled(700, state->dpi), scaled(430, state->dpi)};
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case kBack:
            if (state->history_position > 0) {
                --state->history_position;
                state->navigate(
                    state->history[state->history_position],
                    false, true);
            }
            return 0;
        case kForward:
            if (state->history_position + 1 <
                state->history.size()) {
                ++state->history_position;
                state->navigate(
                    state->history[state->history_position],
                    false, true);
            }
            return 0;
        case kContents:
            state->show_contents();
            return 0;
        case kSearch:
            if (HIWORD(wparam) == EN_CHANGE) state->search_changed();
            return 0;
        case kCopy:
            state->copy_article();
            return 0;
        case kPrint:
            state->print_article();
            return 0;
        case kClose:
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_NOTIFY: {
        const NMHDR* notification =
            reinterpret_cast<const NMHDR*>(lparam);
        if (notification->idFrom == kTree &&
            notification->code == TVN_SELCHANGEDW &&
            !state->suppress_tree) {
            const auto* tree =
                reinterpret_cast<const NMTREEVIEWW*>(lparam);
            if (tree->itemNew.lParam >= 0) {
                state->navigate(
                    static_cast<std::size_t>(
                        tree->itemNew.lParam),
                    true, true);
            }
            return 0;
        }
        if (notification->idFrom == kHits &&
            (notification->code == NM_DBLCLK ||
             notification->code == LVN_ITEMACTIVATE)) {
            const int row = ListView_GetNextItem(
                state->hits, -1, LVNI_SELECTED);
            if (row >= 0) {
                LVITEMW item{};
                item.mask = LVIF_PARAM;
                item.iItem = row;
                if (ListView_GetItem(state->hits, &item)) {
                    state->navigate(
                        static_cast<std::size_t>(item.lParam),
                        true, true);
                }
            }
            return 0;
        }
        if (notification->idFrom == kArticle &&
            notification->code == EN_LINK) {
            state->follow_link(
                *reinterpret_cast<const ENLINK*>(lparam));
            return 0;
        }
        break;
    }
    case WM_SYSCOLORCHANGE:
    case WM_SETTINGCHANGE:
        SendMessageW(
            state->article, EM_SETBKGNDCOLOR, 0,
            GetSysColor(COLOR_WINDOW));
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        if (g_help_window == window) {
            g_help_window = nullptr;
            delete state;
            release_richedit();
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool register_window_class(
    const wchar_t* name, WNDPROC procedure, HICON icon) {
    WNDCLASSEXW existing{sizeof(existing)};
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (GetClassInfoExW(instance, name, &existing)) return true;
    WNDCLASSEXW window_class{sizeof(window_class)};
    window_class.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = procedure;
    window_class.hInstance = instance;
    window_class.hIcon = icon;
    window_class.hIconSm = icon;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground =
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = name;
    return RegisterClassExW(&window_class) != 0;
}

LRESULT CALLBACK about_window_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam);

std::wstring licence_text() {
    const auto resource = resource_view(kLicenceResource);
    if (!resource) {
        return L"Embedded licence information is unavailable.";
    }
    std::wstring text;
    if (!utf8_to_wide(resource->data, resource->size, text)) {
        return L"Embedded licence information could not be decoded.";
    }
    text +=
        L"\r\n\r\nThird-party notices\r\nVelocity NetTools uses "
        L"Windows system components supplied with the operating system. "
        L"No separately distributed third-party runtime components "
        L"are included.";
    return text;
}

struct AboutState {
    HWND window{}, title{}, details{}, links{}, copy{}, licence{}, help{}, close{};
    HFONT font{}, title_font{};
    UINT dpi{96};
    VersionDetails version;

    ~AboutState() {
        if (font) DeleteObject(font);
        if (title_font) DeleteObject(title_font);
    }

    std::wstring copy_value() const {
        return L"Velocity NetTools " + version.version +
               L"\r\nBuild " + version.build +
               L"\r\nPortable native Windows x64";
    }

    void update_font() {
        if (font) DeleteObject(font);
        if (title_font) DeleteObject(title_font);
        font = CreateFontW(
            -scaled(15, dpi), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
        title_font = CreateFontW(
            -scaled(25, dpi), 0, 0, 0, FW_SEMIBOLD,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
        set_font(title, title_font);
        for (HWND child :
             {details, links, copy, licence, help, close}) {
            set_font(child, font);
        }
    }

    void layout() {
        RECT client{};
        GetClientRect(window, &client);
        const int pad = scaled(22, dpi);
        MoveWindow(
            title, pad, pad, client.right - pad * 2,
            scaled(42, dpi), TRUE);
        MoveWindow(
            details, pad, pad + scaled(52, dpi),
            client.right - pad * 2, scaled(105, dpi), TRUE);
        MoveWindow(
            links, pad, pad + scaled(160, dpi),
            client.right - pad * 2, scaled(44, dpi), TRUE);
        const int width = scaled(112, dpi);
        const int height = scaled(30, dpi);
        const int gap = scaled(7, dpi);
        int right = client.right - pad;
        for (HWND button : {close, help, licence, copy}) {
            right -= width;
            MoveWindow(
                button, right, client.bottom - pad - height,
                width, height, TRUE);
            right -= gap;
        }
    }
};

LRESULT CALLBACK about_control_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR reference) {
    auto* state = reinterpret_cast<AboutState*>(reference);
    if (message == WM_KEYDOWN) {
        if (wparam == VK_ESCAPE) {
            DestroyWindow(state->window);
            return 0;
        }
        if (wparam == VK_TAB) {
            const BOOL previous =
                (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (HWND next = GetNextDlgTabItem(
                    state->window, window, previous)) {
                SetFocus(next);
            }
            return 0;
        }
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT CALLBACK about_window_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<AboutState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<AboutState*>(
            reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(
            window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }
    if (!state) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_CREATE: {
        state->dpi = window_dpi(window);
        state->version = version_details();
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        auto child = [&](const wchar_t* klass, const wchar_t* text,
                         DWORD style, int id) {
            DWORD child_style = WS_CHILD | WS_VISIBLE | style;
            if (id != 0) child_style |= WS_TABSTOP;
            HWND control = CreateWindowExW(
                0, klass, text, child_style,
                0, 0, 1, 1, window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(id)),
                instance, nullptr);
            if (control) {
                SetWindowSubclass(
                    control, about_control_proc, 1,
                    reinterpret_cast<DWORD_PTR>(state));
            }
            return control;
        };
        state->title = child(
            WC_STATICW, L"Velocity NetTools", SS_LEFT, 0);
        std::wstring details =
            L"By VEU\r\nVersion: " + state->version.version +
            L"\r\nBuild: " + state->version.build +
            L"\r\nPortable native Windows x64";
        state->details = child(
            WC_STATICW, details.c_str(), SS_LEFT, 0);
        state->links = child(
            WC_LINK,
            L"<a href=\"https://www.velocity-eu.com/\">"
            L"Velocity EU</a>    "
            L"<a href=\"https://github.com/velocityeu/NetTools\">"
            L"Project</a>",
            0, kAboutLinks);
        state->copy = child(
            WC_BUTTONW, L"Copy version",
            BS_PUSHBUTTON, kAboutCopy);
        state->licence = child(
            WC_BUTTONW, L"Licence && notices",
            BS_PUSHBUTTON, kAboutLicence);
        state->help = child(
            WC_BUTTONW, L"Help", BS_PUSHBUTTON, kAboutHelp);
        state->close = child(
            WC_BUTTONW, L"OK", BS_DEFPUSHBUTTON, kAboutClose);
        if (!state->title || !state->details || !state->links ||
            !state->copy || !state->licence || !state->help ||
            !state->close) return -1;
        state->update_font();
        return 0;
    }
    case WM_SIZE:
        state->layout();
        return 0;
    case WM_DPICHANGED: {
        state->dpi = HIWORD(wparam);
        const RECT* suggested =
            reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(
            window, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOACTIVATE | SWP_NOZORDER);
        state->update_font();
        state->layout();
        return 0;
    }
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {
            scaled(560, state->dpi), scaled(300, state->dpi)};
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case kAboutCopy:
            copy_text(window, state->copy_value());
            return 0;
        case kAboutLicence: {
            const std::wstring text = licence_text();
            MessageBoxW(
                window, text.c_str(),
                L"Velocity NetTools — Licence & notices",
                MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        case kAboutHelp:
            show_help(
                window, L"help-portability-and-shortcuts");
            return 0;
        case kAboutClose:
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_NOTIFY: {
        const NMHDR* notification =
            reinterpret_cast<const NMHDR*>(lparam);
        if (notification->idFrom == kAboutLinks &&
            (notification->code == NM_CLICK ||
             notification->code == NM_RETURN)) {
            const auto* link =
                reinterpret_cast<const NMLINK*>(lparam);
            open_https(window, link->item.szUrl);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        if (g_about_window == window) {
            g_about_window = nullptr;
            delete state;
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

HICON application_icon() {
    HICON icon = static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_VEU_NETTOOLS),
        IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

void centre_on_owner(HWND window, HWND owner) {
    RECT bounds{}, parent{};
    GetWindowRect(window, &bounds);
    if (!owner || !GetWindowRect(owner, &parent)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &parent, 0);
    }
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    const int x = parent.left +
        std::max(0, (static_cast<int>(parent.right - parent.left) - width) / 2);
    const int y = parent.top +
        std::max(0, (static_cast<int>(parent.bottom - parent.top) - height) / 2);
    SetWindowPos(
        window, nullptr, x, y, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

}  // namespace

void show_help(HWND owner, std::wstring_view context) {
    if (g_help_window && IsWindow(g_help_window)) {
        auto* state = reinterpret_cast<HelpState*>(
            GetWindowLongPtrW(g_help_window, GWLP_USERDATA));
        if (state) state->apply_context(context);
        ShowWindow(g_help_window, SW_RESTORE);
        SetForegroundWindow(g_help_window);
        return;
    }
    if (!acquire_richedit()) {
        MessageBoxW(
            owner,
            L"Windows Rich Edit could not be loaded from the system "
            L"directory. Help is unavailable, but the toolkit can continue.",
            L"Velocity NetTools", MB_OK | MB_ICONWARNING);
        return;
    }
    auto state = std::make_unique<HelpState>();
    state->pending_context.assign(context);
    if (!load_topics(state->topics)) {
        release_richedit();
        MessageBoxW(
            owner,
            L"Embedded help resources are missing or invalid. "
            L"Regenerate resources and rebuild Velocity NetTools.",
            L"Velocity NetTools", MB_OK | MB_ICONWARNING);
        return;
    }
    HICON icon = application_icon();
    if (!register_window_class(
            kHelpClass, help_window_proc, icon)) {
        release_richedit();
        MessageBoxW(
            owner, L"Windows could not register the Help window.",
            L"Velocity NetTools", MB_OK | MB_ICONWARNING);
        return;
    }
    const UINT dpi = owner ? window_dpi(owner) : 96;
    HWND window = CreateWindowExW(
        WS_EX_CONTROLPARENT, kHelpClass,
        L"Velocity NetTools Help",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        scaled(1000, dpi), scaled(650, dpi),
        owner, nullptr, GetModuleHandleW(nullptr), state.get());
    if (!window) {
        release_richedit();
        MessageBoxW(
            owner, L"Windows could not create the Help window.",
            L"Velocity NetTools", MB_OK | MB_ICONWARNING);
        return;
    }
    state.release();
    g_help_window = window;
    centre_on_owner(window, owner);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
}

void show_about(HWND owner) {
    if (g_about_window && IsWindow(g_about_window)) {
        ShowWindow(g_about_window, SW_RESTORE);
        SetForegroundWindow(g_about_window);
        return;
    }
    HICON icon = application_icon();
    if (!register_window_class(
            kAboutClass, about_window_proc, icon)) {
        MessageBoxW(
            owner, L"Windows could not register the About window.",
            L"Velocity NetTools", MB_OK | MB_ICONWARNING);
        return;
    }
    auto state = std::make_unique<AboutState>();
    const UINT dpi = owner ? window_dpi(owner) : 96;
    HWND window = CreateWindowExW(
        WS_EX_CONTROLPARENT, kAboutClass,
        L"About Velocity NetTools",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
            WS_THICKFRAME | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        scaled(680, dpi), scaled(330, dpi),
        owner, nullptr, GetModuleHandleW(nullptr), state.get());
    if (!window) {
        MessageBoxW(
            owner, L"Windows could not create the About window.",
            L"Velocity NetTools", MB_OK | MB_ICONWARNING);
        return;
    }
    state.release();
    g_about_window = window;
    centre_on_owner(window, owner);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
}

}  // namespace veu
