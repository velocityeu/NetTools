#pragma once
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace veu::grid
{
// Reorder the headings and cells together so copying/exporting matches the UI.
inline void prioritize(std::vector<std::wstring> &columns,
                       std::vector<std::vector<std::wstring>> &rows,
                       std::initializer_list<std::wstring_view> preferred)
{
    std::vector<size_t> order;
    for (auto name : preferred)
        for (size_t i = 0; i < columns.size(); ++i)
            if (columns[i] == name && std::find(order.begin(), order.end(), i) == order.end())
                order.push_back(i);
    for (size_t i = 0; i < columns.size(); ++i)
        if (std::find(order.begin(), order.end(), i) == order.end())
            order.push_back(i);
    bool changed = false;
    for (size_t i = 0; i < order.size(); ++i)
        changed |= order[i] != i;
    if (!changed)
        return;
    auto reorder = [&](std::vector<std::wstring> &values) {
        std::vector<std::wstring> sorted;
        sorted.reserve(order.size());
        for (auto i : order)
            sorted.push_back(i < values.size() ? std::move(values[i]) : std::wstring{});
        values = std::move(sorted);
    };
    reorder(columns);
    for (auto &row : rows)
        reorder(row);
}

inline void fit(HWND table, unsigned dpi, const std::vector<std::wstring> &columns)
{
    if (!table || columns.empty())
        return;
    auto px = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    RECT bounds{};
    GetClientRect(table, &bounds);
    int available = std::max(1, static_cast<int>(bounds.right) - px(4));
    int used = 0;
    for (size_t i = 0; i < columns.size(); ++i)
    {
        const auto &name = columns[i];
        int low = 90, high = 180;
        if (name == L"Prefix" || name == L"Family" || name == L"Port" ||
            name == L"MTU" || name == L"Round" || name == L"Type" ||
            name == L"TTL/Hop" || name == L"TTL (s)" || name == L"Sequence")
        {
            low = 54;
            high = 112;
        }
        else if (name == L"Adapter" || name == L"ID / state")
        {
            low = 150;
            high = 230;
        }
        else if (name == L"Address" || name == L"Responder" ||
                 name == L"Attempted address" || name == L"Observed address / error" ||
                 name == L"Destination/prefix" || name == L"Next hop" ||
                 name == L"Local endpoint" || name == L"Subnet" ||
                 name == L"Subnet / reason" || name == L"CIDR" ||
                 name == L"Data" || name == L"Value" || name == L"Destination" || name == L"Source")
        {
            low = 210;
            high = 380;
        }
        else if (name == L"Addresses")
        {
            low = 110;
            high = 310;
        }
        int wanted = ListView_GetStringWidth(table, name.c_str()) + px(22);
        wchar_t value[512]{};
        for (int row = 0; row < std::min(200, ListView_GetItemCount(table)); ++row)
        {
            ListView_GetItemText(table, row, static_cast<int>(i), value, 512);
            wanted = std::max(wanted, ListView_GetStringWidth(table, value) + px(20));
        }
        int width = std::clamp(wanted, px(low), px(high));
        if (columns.size() == 2)
            width = i == 0 ? std::min(width, std::max(1, available * 2 / 5))
                           : std::max(1, available - used);
        else if (i == 0)
            width = std::min(width, std::max(1, available - px(80)));
        ListView_SetColumnWidth(table, static_cast<int>(i), width);
        used += width;
    }
}
} // namespace veu::grid
