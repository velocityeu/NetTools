#include "veu/subnet_pane.hpp"
#include "veu/help.hpp"
#include "veu/storage.hpp"
#include "veu/subnet.hpp"
#include "veu/ui_common.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <commctrl.h>
#include <condition_variable>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <uxtheme.h>
namespace veu {
namespace {
using namespace ui;
using namespace subnet;
struct Spec {
  const wchar_t *name;
  const wchar_t *hint;
  const wchar_t *help;
  std::vector<std::pair<const wchar_t *, const wchar_t *>> fields;
};
const std::array<Spec, 5> specs = {
    {{L"Subnet calculator",
      L"Literal address and explicit prefix or contiguous mask. Pasted CIDR "
      L"overrides the prefix field. Automatic calculation is enabled.",
      L"understand-an-ipv4-subnet",
      {{L"IP address / CIDR", L"192.168.10.42"},
       {L"Prefix length / mask", L"26"}}},
     {L"Equal split",
      L"Direct indexed paging; no address enumeration. The index starts at "
      L"zero. Use VLSM for unequal requirements.",
      L"split-a-network-into-equal-parts",
      {{L"Parent CIDR", L"192.168.10.0/24"},
       {L"Child prefix", L"26"},
       {L"First subnet index", L"0"}}},
     {L"VLSM planner",
      L"Requirements: ID | name | hosts or IPv6 prefix | LAN/PTP/HOST/IPv6 | "
      L"existing CIDR (optional) | PIN (optional). Preview before Apply.",
      L"pinned-vlsm",
      {{L"Parent CIDR", L"192.168.10.0/24"},
       {L"Requirements (one per line)",
        L"office | Office | 50 | LAN\r\nvoice | Voice | 25 | LAN\r\nlink | "
        L"Router link | 2 | PTP"},
       {L"Reservations: ID | name | aligned CIDR", L""}}},
     {L"Aggregate / ranges",
      L"One CIDR or inclusive range (first - last) per line. Exact aggregation "
      L"adds no addresses. A covering supernet can add coverage. Use EMPTY for "
      L"an empty set.",
      L"aggregate-without-extra-coverage",
      {{L"Networks / ranges", L"192.0.2.0/25\r\n192.0.2.128/25"}}},
     {L"Directional comparison",
      L"Use EMPTY for an intentional empty set. Results show the intersection, "
      L"A minus B, and B "
      L"minus A.",
      L"directional-compare",
      {{L"Set A", L"192.0.2.0/24"}, {L"Set B", L"192.0.2.128/25"}}}}};
using Row = std::vector<std::wstring>;
struct SharedRows {
  std::shared_ptr<std::vector<Row>> value;
  static const std::vector<Row> &empty_rows() {
    static const std::vector<Row> empty;
    return empty;
  }
  void clear() { value.reset(); }
  void push_back(Row row) {
    if (!value)
      value = std::make_shared<std::vector<Row>>();
    value->push_back(std::move(row));
  }
  bool empty() const { return !value || value->empty(); }
  size_t size() const { return value ? value->size() : 0; }
  Row &operator[](size_t i) { return (*value)[i]; }
  const Row &operator[](size_t i) const { return (*value)[i]; }
  auto begin() const { return value ? value->cbegin() : empty_rows().cbegin(); }
  auto end() const { return value ? value->cend() : empty_rows().cend(); }
};

struct View {
  std::vector<std::wstring> fields, columns;
  SharedRows rows;
  std::wstring summary = L"Ready.", applied_text;
  size_t page = 0;
  uint64_t revision = 0;
  bool ready = false, busy = false;
  std::optional<Calculation> calculation;
  std::optional<VlsmResult> proposal;
  std::vector<Request> requests;
  std::map<std::string, uint64_t> sequences;
  Integer split_index, split_total;
};
struct Job {
  enum Kind { calculate, export_csv, clipboard } kind = calculate;
  unsigned mode = 0;
  uint64_t revision = 0;
  bool details = false, reallocate = false;
  std::vector<std::string> fields;
  std::map<std::string, uint64_t> sequences;
  std::shared_ptr<std::atomic_bool> cancelled =
      std::make_shared<std::atomic_bool>(false);
  SharedRows rows;
  std::vector<std::wstring> columns;
  std::wstring summary;
  std::filesystem::path path;
};
struct Completion {
  unsigned mode;
  uint64_t revision;
  Job::Kind kind;
  View view;
  std::wstring clipboard;
};
struct Worker {
  std::mutex mutex;
  std::condition_variable wake;
  bool closing = false, running = false;
  std::optional<Job> pending;
  std::optional<Completion> complete;
  std::shared_ptr<std::atomic_bool> active;
};
struct PlanSnapshot {
  std::vector<std::wstring> fields;
  std::map<std::string, uint64_t> sequences;
};
struct Pane {
  HWND window{}, tabs{}, title{}, hint{}, list{}, summary{};
  HFONT font{};
  std::vector<HWND> labels, edits;
  std::array<HWND, 15> buttons{};
  std::shared_ptr<Worker> worker = std::make_shared<Worker>();
  std::vector<PlanSnapshot> undo, redo;
  std::array<View, 5> views;
  unsigned mode = 0;
  bool loading = false, details = false;
  int dpi = 96, base_dpi = 96, scroll = 0, content_height = 0;
  LOGFONTW base_font{};
};
Pane *state(HWND h) {
  return reinterpret_cast<Pane *>(GetWindowLongPtrW(h, GWLP_USERDATA));
}
int scale(Pane &p, int n) { return MulDiv(n, p.dpi, 96); }
HWND make(Pane &p, const wchar_t *cls, const wchar_t *caption, DWORD style,
          int id) {
  auto h = CreateWindowExW(wcscmp(cls, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0,
                           cls, caption, WS_CHILD | WS_VISIBLE | style, 0, 0, 0,
                           0, p.window, reinterpret_cast<HMENU>(INT_PTR(id)),
                           GetModuleHandleW(nullptr), nullptr);
  SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(p.font), TRUE);
  return h;
}
void capture(Pane &p) {
  auto &v = p.views[p.mode];
  v.fields.clear();
  for (auto h : p.edits)
    v.fields.push_back(text(h));
}
std::string input(const Job &job, size_t i) { return job.fields.at(i); }
bool multi(unsigned mode, size_t i) {
  return mode >= 3 || (mode == 2 && i > 0);
}
void layout(Pane &p) {
  if (!p.title)
    return;
  RECT r{};
  GetClientRect(p.window, &r);
  const int pad = scale(p, 12), w = std::max(1, int(r.right) - 2 * pad);
  int y = pad;
  struct Position {
    HWND h;
    int x, y, w, hgt;
  };
  std::vector<Position> positions;
  auto place = [&](HWND h, int x, int yy, int ww, int hh) {
    positions.push_back({h, x, yy, std::max(1, ww), hh});
  };
  auto wrapped = [&](HWND h, int minimum) {
    HDC dc = GetDC(h);
    auto old = SelectObject(dc, p.font);
    RECT measure{0, 0, w, 0};
    auto label = text(h);
    DrawTextW(dc, label.c_str(), int(label.size()), &measure,
              DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(h, dc);
    return std::max(scale(p, minimum), int(measure.bottom) + scale(p, 3));
  };
  int h = wrapped(p.title, 24);
  place(p.title, pad, y, w, h);
  y += h + scale(p, 5);
  h = wrapped(p.hint, 42);
  place(p.hint, pad, y, w, h);
  y += h + scale(p, 7);
  MoveWindow(p.tabs, pad, y, w, scale(p, 28), FALSE);
  h = scale(p, 28) * std::max(1, TabCtrl_GetRowCount(p.tabs));
  place(p.tabs, pad, y, w, h);
  y += h + scale(p, 7);
  for (size_t i = 0; i < p.edits.size(); ++i) {
    h = wrapped(p.labels[i], 19);
    place(p.labels[i], pad, y, w, h);
    y += h + scale(p, 2);
    h = scale(p, multi(p.mode, i) ? (p.mode == 2 ? 72 : 80) : 25);
    place(p.edits[i], pad, y, w, h);
    y += h + scale(p, 7);
  }
  int x = pad;
  for (size_t i = 0; i < p.buttons.size(); ++i) {
    if (!(GetWindowLongPtrW(p.buttons[i], GWL_STYLE) & WS_VISIBLE))
      continue;
    int bw = std::min(w, scale(p, (i == 4 || i == 5 || i == 8) ? 124 : 92));
    if (x != pad && x + bw > pad + w) {
      x = pad;
      y += scale(p, 32);
    }
    place(p.buttons[i], x, y, bw, scale(p, 27));
    x += bw + scale(p, 5);
  }
  y += scale(p, 34);
  place(p.summary, pad, y, w, scale(p, 72));
  y += scale(p, 79);
  const int list_height = std::max(scale(p, 160), int(r.bottom) - y - pad);
  place(p.list, pad, y, w, list_height);
  p.content_height = y + list_height + pad;
  p.scroll =
      std::clamp(p.scroll, 0, std::max(0, p.content_height - int(r.bottom)));
  SCROLLINFO si{sizeof(si),
                SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
  si.nMax = p.content_height - 1;
  si.nPage = std::max(1, int(r.bottom));
  si.nPos = p.scroll;
  SetScrollInfo(p.window, SB_VERT, &si, TRUE);
  for (auto &q : positions)
    MoveWindow(q.h, q.x, q.y - p.scroll, q.w, q.hgt, TRUE);
}
void set_font(Pane &p) {
  auto lf = p.base_font;
  lf.lfHeight = MulDiv(lf.lfHeight, p.dpi, p.base_dpi);
  auto font = CreateFontIndirectW(&lf);
  if (!font)
    return;
  auto old = p.font;
  p.font = font;
  for (HWND child = GetWindow(p.window, GW_CHILD); child;
       child = GetWindow(child, GW_HWNDNEXT))
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  if (old)
    DeleteObject(old);
}
void scroll_to(Pane &p, int position) {
  p.scroll = position;
  layout(p);
}
void reveal(Pane &p, HWND child) {
  if (!child)
    return;
  RECT box{}, client{};
  GetWindowRect(child, &box);
  MapWindowPoints(nullptr, p.window, reinterpret_cast<POINT *>(&box), 2);
  GetClientRect(p.window, &client);
  if (box.top < 0)
    scroll_to(p, p.scroll + box.top - scale(p, 8));
  else if (box.bottom > client.bottom)
    scroll_to(p, p.scroll + box.bottom - client.bottom + scale(p, 8));
}

void render(Pane &p) {
  auto &v = p.views[p.mode];
  SendMessageW(p.list, WM_SETREDRAW, FALSE, 0);
  ListView_DeleteAllItems(p.list);
  while (ListView_DeleteColumn(p.list, 0)) {
  }
  for (size_t c = 0; c < v.columns.size(); c++) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = v.columns[c].data();
    col.cx = scale(p, c == 0 ? 190 : 300);
    ListView_InsertColumn(p.list, int(c), &col);
  }
  if (v.rows.empty())
    v.page = 0;
  else
    v.page = std::min(v.page, (v.rows.size() - 1) / 200);
  size_t end = std::min(v.rows.size(), v.page * 200 + 200);
  for (size_t i = v.page * 200; i < end; i++) {
    auto &row = v.rows[i];
    if (row.empty())
      continue;
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = int(i - v.page * 200);
    item.pszText = row[0].data();
    int idx = ListView_InsertItem(p.list, &item);
    for (size_t c = 1; c < row.size(); c++)
      ListView_SetItemText(p.list, idx, int(c), row[c].data());
  }
  SendMessageW(p.list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(p.list, nullptr, TRUE);
  SetWindowTextW(p.summary, v.summary.c_str());
  EnableWindow(p.buttons[1], v.ready && !v.busy);
  EnableWindow(p.buttons[2], v.ready && !v.busy);
  EnableWindow(p.buttons[4],
               p.mode == 0   ? (v.calculation && v.calculation->previous)
               : p.mode == 1 ? v.ready && !v.busy && v.split_index > Integer()
                             : v.page > 0);
  EnableWindow(p.buttons[5],
               p.mode == 0 ? (v.calculation && v.calculation->next)
               : p.mode == 1
                   ? v.ready && !v.busy &&
                         v.split_index + Integer(v.rows.size()) < v.split_total
                   : end < v.rows.size());
  EnableWindow(p.buttons[7], v.ready && !v.busy && v.proposal.has_value());
  EnableWindow(p.buttons[10], v.busy);
  EnableWindow(p.buttons[11], p.mode == 2 && !p.undo.empty());
  EnableWindow(p.buttons[12], p.mode == 2 && !p.redo.empty());
  EnableWindow(p.buttons[13], p.mode == 0 && v.page > 0);
  EnableWindow(p.buttons[14], p.mode == 0 && end < v.rows.size());
}
void build(Pane &p, unsigned mode, bool saveCurrent = true) {
  if (saveCurrent && !p.edits.empty())
    capture(p);
  p.loading = true;
  for (auto h : p.edits)
    DestroyWindow(h);
  for (auto h : p.labels)
    DestroyWindow(h);
  p.edits.clear();
  p.labels.clear();
  p.mode = mode;
  p.scroll = 0;
  auto &spec = specs[mode];
  auto &v = p.views[mode];
  SetWindowTextW(p.title, spec.name);
  SetWindowTextW(p.hint, spec.hint);
  TabCtrl_SetCurSel(p.tabs, mode < 2    ? int(mode)
                            : mode == 2 ? 1
                                        : int(mode) - 1);
  for (size_t i = 0; i < spec.fields.size(); i++) {
    p.labels.push_back(make(p, L"STATIC", spec.fields[i].first, SS_LEFT, 0));
    auto h = make(p, L"EDIT", L"",
                  WS_TABSTOP | (multi(mode, i) ? ES_MULTILINE | ES_WANTRETURN |
                                                     WS_VSCROLL | ES_AUTOVSCROLL
                                               : ES_AUTOHSCROLL),
                  1000 + int(i));
    SendMessageW(h, EM_SETLIMITTEXT, multi(mode, i) ? 16 * 1024 * 1024 : 4096,
                 0);
    SetWindowTextW(h, i < v.fields.size() ? v.fields[i].c_str()
                                          : spec.fields[i].second);
    p.edits.push_back(h);
  }
  SetWindowTextW(p.buttons[0], mode == 2 ? L"&Preview" : L"&Calculate");
  SetWindowTextW(p.buttons[4],
                 mode == 0 ? L"Previous subnet" : L"Previous page");
  SetWindowTextW(p.buttons[5], mode == 0 ? L"Next subnet" : L"Next page");
  ShowWindow(p.buttons[6], mode == 0 ? SW_SHOW : SW_HIDE);
  ShowWindow(p.buttons[7], mode == 2 ? SW_SHOW : SW_HIDE);
  ShowWindow(p.buttons[8], mode == 2 ? SW_SHOW : SW_HIDE);
  ShowWindow(p.buttons[9], mode == 1 || mode == 2 ? SW_SHOW : SW_HIDE);
  SetWindowTextW(p.buttons[9], mode == 1 ? L"&VLSM" : L"&Equal split");
  ShowWindow(p.buttons[11], mode == 2 ? SW_SHOW : SW_HIDE);
  ShowWindow(p.buttons[12], mode == 2 ? SW_SHOW : SW_HIDE);
  ShowWindow(p.buttons[13], mode == 0 ? SW_SHOW : SW_HIDE);
  ShowWindow(p.buttons[14], mode == 0 ? SW_SHOW : SW_HIDE);
  HWND previous = HWND_TOP;
  auto order = [&](HWND control) {
    SetWindowPos(control, previous, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    previous = control;
  };
  order(p.title);
  order(p.hint);
  order(p.tabs);
  for (size_t i = 0; i < p.edits.size(); ++i) {
    order(p.labels[i]);
    order(p.edits[i]);
  }
  for (auto button : p.buttons)
    order(button);
  order(p.summary);
  order(p.list);
  SetWindowTextW(p.buttons[1], mode == 1 ? L"Copy page" : L"&Copy");
  SetWindowTextW(p.buttons[2], mode == 1 ? L"Export page" : L"&Export CSV");
  p.loading = false;
  layout(p);
  render(p);
}
std::string trim(std::string s) {
  auto a = s.find_first_not_of(" \r\t\n\f\v"),
       b = s.find_last_not_of(" \r\t\n\f\v");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
std::vector<std::vector<std::string>> parse_rows(const std::string &s,
                                                 const Context &ctx) {
  if (s.size() > 16u * 1024u * 1024u)
    throw Error("Input exceeds 16 MiB.");
  std::vector<std::vector<std::string>> out;
  std::istringstream all(s);
  std::string line;
  while (std::getline(all, line)) {
    ctx.check();
    if (trim(line).empty())
      continue;
    std::istringstream one(line);
    std::string item;
    std::vector<std::string> row;
    while (std::getline(one, item, '|'))
      row.push_back(trim(item));
    out.push_back(row);
    if (out.size() > 10000)
      throw Error("Maximum 10,000 rows.");
  }
  return out;
}
std::vector<Interval> operand(const std::string &text, const Context &ctx) {
  auto value = trim(text);
  if (value.empty())
    throw Error("Input is incomplete; use EMPTY for an intentional empty set.");
  return value == "EMPTY" ? std::vector<Interval>{} : parse_set(value, ctx);
}
View compute(const Job &job) {
  View v;
  v.revision = job.revision;
  v.sequences = job.sequences;
  Context ctx;
  ctx.max_work_bytes = 64u * 1024u * 1024u;
  ctx.cancelled = [&] { return job.cancelled->load(); };
  size_t row_bytes = 0;
  auto add_row = [&](Row row) {
    ctx.check();
    size_t bytes = sizeof(Row) + row.capacity() * sizeof(std::wstring);
    for (auto &cell : row)
      bytes += (cell.capacity() + 1) * sizeof(wchar_t);
    if (v.rows.size() >= 1000000 || bytes > 32u * 1024u * 1024u - row_bytes)
      throw Error("Result display exceeds the 32 MiB working limit; narrow the "
                  "operation.");
    row_bytes += bytes;
    v.rows.push_back(std::move(row));
  };
  v.rows.clear();
  v.columns = {L"Property", L"Value"};
  v.calculation.reset();
  v.proposal.reset();
  v.page = 0;
  try {
    if (job.mode == 0) {
      auto c = subnet::calculate(input(job, 0), input(job, 1));
      v.calculation = c;
      auto row = [&](const wchar_t *k, std::string val) {
        add_row({k, wide(val)});
      };
      row(L"Entered address", format(c.entered));
      row(L"Network / prefix", format(c.prefix));
      row(L"Last address", format(c.last));
      row(L"Total addresses", c.total.str());
      row(L"Host offset", c.offset.str());
      if (c.mask)
        row(L"Subnet mask", format(*c.mask));
      if (c.wildcard)
        row(L"Wildcard", format(*c.wildcard));
      if (c.broadcast)
        row(L"Broadcast", format(*c.broadcast));
      if (c.first_host)
        row(L"First usable / endpoint", format(*c.first_host));
      if (c.last_host)
        row(L"Last usable / endpoint", format(*c.last_host));
      if (c.capacity)
        row(L"Capacity", c.capacity->str());
      row(L"Interpretation", c.capacity_note);
      auto cl = classify(c.prefix, ctx);
      row(L"Address classification", cl.summary);
      if (job.details) {
        std::string bits, hex;
        auto widthBits = width(c.entered.family);
        for (unsigned i = widthBits; i > 0; i--) {
          bits += c.entered.value.bit(i - 1) ? '1' : '0';
          if ((i - 1) % 8 == 0 && i > 1)
            bits += ' ';
        }
        for (unsigned i = widthBits; i > 0; i -= 4) {
          unsigned n = 0;
          for (unsigned j = 0; j < 4; j++)
            n = (n << 1) | unsigned(c.entered.value.bit(i - j - 1));
          hex += "0123456789abcdef"[n];
        }
        row(L"Address bits", bits);
        row(L"Address hex", hex);
        for (auto &seg : cl.segments)
          row(L"Classification range",
              format(seg.range.first) + " – " +
                  format(Address{seg.range.first.family,
                                 seg.range.end - Integer(1)}) +
                  ": " + std::string(seg.name));
      }
      v.summary = L"Exact calculation. " + wide(c.capacity_note);
    } else if (job.mode == 1) {
      auto parent = parse_cidr(input(job, 0));
      auto child = parse_prefix(input(job, 1), parent.network.family);
      auto index = Integer::decimal(input(job, 2));
      auto total = split_count(parent, child);
      v.split_index = index;
      v.split_total = total;
      auto page = split_page(parent, child, index, 200, ctx);
      v.columns = {L"Index (zero-based)", L"Subnet", L"Addresses"};
      for (size_t i = 0; i < page.size(); i++)
        add_row({wide((index + Integer(i)).str()), wide(format(page[i])),
                 wide(size(page[i]).str())});
      v.summary = L"Normalized parent: " + wide(format(parent)) +
                  L". Total subnets: " + wide(total.str()) +
                  L". Showing and exporting this page of up to 200. Change the "
                  L"first index to jump directly.";
    } else if (job.mode == 2) {
      auto parent = parse_cidr(input(job, 0));
      std::vector<Request> requests;
      std::vector<Reservation> reservations;
      auto parsed_requests = parse_rows(input(job, 1), ctx);
      std::set<std::string_view> current_ids;
      for (auto &r : parsed_requests)
        if (!r.empty())
          current_ids.insert(r[0]);
      for (auto it = v.sequences.begin(); it != v.sequences.end();) {
        ctx.check();
        if (!current_ids.contains(it->first))
          it = v.sequences.erase(it);
        else
          ++it;
      }
      uint64_t next_sequence = 0;
      for (auto &entry : v.sequences) {
        if (entry.second == UINT64_MAX)
          throw Error("Insertion sequence exhausted.");
        next_sequence = std::max(next_sequence, entry.second + 1);
      }
      for (auto &r : parsed_requests) {
        try {
          if (r.size() < 4 || r.size() > 6)
            throw Error("Requirements need ID | name | size | kind, with "
                        "optional existing CIDR | PIN.");
          Request q;
          q.id = r[0];
          q.name = r[1];
          auto found = v.sequences.find(q.id);
          if (found == v.sequences.end()) {
            if (next_sequence == UINT64_MAX)
              throw Error("Insertion sequence exhausted.");
            found = v.sequences.emplace(q.id, next_sequence++).first;
          }
          q.sequence = found->second;
          if (r[3] == "LAN")
            q.kind = RequestKind::ipv4_lan;
          else if (r[3] == "PTP")
            q.kind = RequestKind::ipv4_point_to_point;
          else if (r[3] == "HOST")
            q.kind = RequestKind::ipv4_host_route;
          else if (r[3] == "IPv6") {
            q.kind = RequestKind::ipv6_prefix;
            q.child_prefix = parse_prefix(r[2], Family::ipv6);
          } else
            throw Error("Kind must be LAN, PTP, HOST or IPv6.");
          if (q.kind != RequestKind::ipv6_prefix)
            q.hosts = Integer::decimal(r[2]);
          if (r.size() > 4 && !r[4].empty())
            q.assigned = parse_cidr(r[4], true);
          if (r.size() > 5 && !r[5].empty()) {
            if (r[5] != "PIN")
              throw Error("Pin marker must be PIN.");
            q.pinned = true;
          }
          requests.push_back(q);
        } catch (const Error &e) {
          throw Error("Request " +
                      (r.empty() || r[0].empty()
                           ? std::to_string(requests.size() + 1)
                           : r[0]) +
                      ": " + e.what());
        }
      }
      for (auto &r : parse_rows(input(job, 2), ctx)) {
        try {
          if (r.size() != 3)
            throw Error("Reservations need ID | name | aligned CIDR.");
          reservations.push_back({r[0], r[1], parse_cidr(r[2], true)});
        } catch (const Error &e) {
          throw Error("Reservation " +
                      (r.empty() || r[0].empty()
                           ? std::to_string(reservations.size() + 1)
                           : r[0]) +
                      ": " + e.what());
        }
      }
      auto result =
          allocate(parent, requests, reservations, job.reallocate, ctx);

      v.columns = {L"ID / state", L"Subnet / reason", L"Addresses"};
      for (auto &a : result.allocations)
        add_row({wide(a.id) + (a.pinned      ? L" [pinned]"
                               : a.preserved ? L" [preserved]"
                                             : L""),
                 wide(format(a.prefix)), wide(size(a.prefix).str())});
      for (auto &a : result.unallocated)
        add_row({wide(a.id) + L" [unallocated]", wide(a.reason), L""});
      for (auto &a : result.free.cidrs)
        add_row({L"Free", wide(format(a)), wide(size(a).str())});
      v.summary =
          std::wstring(
              result.partial
                  ? L"PARTIAL proposal: review unallocated rows before Apply. "
                  : L"Proposal ready. Apply to retain assignments in the "
                    L"plan. ") +
          L"Normalized parent: " + wide(format(parent)) + L". Free: " +
          wide(result.free.count.str()) +
          (result.reservations_overlap
               ? L". Overlapping reservations were unioned."
               : L".");
      if (result.largest_free)
        v.summary += L" Largest free aligned block: " +
                     wide(format(*result.largest_free)) + L".";
      std::map<std::string, Prefix> assigned;
      for (auto &a : result.allocations)
        assigned.emplace(a.id, a.prefix);
      std::string applied;
      for (const auto &q : requests) {
        ctx.check();
        auto found = assigned.find(q.id);
        auto kind = q.kind == RequestKind::ipv4_lan              ? "LAN"
                    : q.kind == RequestKind::ipv4_point_to_point ? "PTP"
                    : q.kind == RequestKind::ipv4_host_route     ? "HOST"
                                                                 : "IPv6";
        auto line =
            q.id + " | " + q.name + " | " +
            (q.kind == RequestKind::ipv6_prefix ? std::to_string(q.child_prefix)
                                                : q.hosts.str()) +
            " | " + kind + " | " +
            (found == assigned.end() ? "" : format(found->second)) + " | " +
            (q.pinned ? "PIN" : "") + "\r\n";
        if (line.size() > 16u * 1024u * 1024u - applied.size())
          throw Error("Applied plan would exceed the 16 MiB input limit.");
        applied += line;
      }
      v.applied_text = wide(applied);
      v.proposal = std::move(result);
    } else if (job.mode == 3) {
      auto r = aggregate(operand(input(job, 0), ctx), ctx);
      v.columns = {L"Kind", L"CIDR", L"Addresses"};
      for (auto &a : r.exact.cidrs)
        add_row({L"Exact", wide(format(a)), wide(size(a).str())});
      if (r.covering)
        add_row({L"Covering supernet", wide(format(*r.covering)),
                 wide(size(*r.covering).str())});
      for (auto &a : r.extra.cidrs)
        add_row({L"ADDITIONAL coverage", wide(format(a)), wide(size(a).str())});
      v.summary = L"Exact union: " + wide(r.exact.count.str()) +
                  L" addresses. Covering adds: " + wide(r.extra.count.str()) +
                  L".";
    } else {
      auto r = compare(operand(input(job, 0), ctx), operand(input(job, 1), ctx),
                       ctx);
      v.columns = {L"Set", L"CIDR", L"Addresses"};
      for (auto [name, set] : {std::pair{L"A intersection B", &r.intersection},
                               std::pair{L"A minus B", &r.a_only},
                               std::pair{L"B minus A", &r.b_only}})
        for (auto &a : set->cidrs)
          add_row({name, wide(format(a)), wide(size(a).str())});
      v.summary = std::wstring(r.equal ? L"Equal. " : L"Not equal. ") +
                  L"Intersection: " + wide(r.intersection.count.str()) +
                  L"; A only: " + wide(r.a_only.count.str()) + L"; B only: " +
                  wide(r.b_only.count.str()) + L".";
    }
    ctx.check();
    v.ready = true;
  } catch (const std::exception &e) {
    v.rows.clear();
    v.calculation.reset();
    v.proposal.reset();
    v.summary = L"Input error: " + wide(e.what());
  }
  return v;
}

void worker_loop(std::shared_ptr<Worker> worker) {
  for (;;) {
    Job job;
    {
      std::unique_lock lock(worker->mutex);
      worker->wake.wait(
          lock, [&] { return worker->closing || worker->pending.has_value(); });
      if (worker->closing)
        return;
      job = std::move(*worker->pending);
      worker->pending.reset();
      worker->running = true;
      worker->active = job.cancelled;
    }
    Completion done{job.mode, job.revision, job.kind};
    try {
      if (job.kind == Job::calculate)
        done.view = compute(job);
      else {
        auto check = [&] {
          if (job.cancelled->load())
            throw Error("Operation cancelled.");
        };
        if (job.rows.size() > 1000000)
          throw Error("Export exceeds 1,000,000 rows.");
        if (job.kind == Job::clipboard) {
          done.clipboard = std::wstring(specs[job.mode].name) + L"\r\n" +
                           job.summary + L"\r\n";
          for (auto &row : job.rows) {
            check();
            for (size_t i = 0; i < row.size(); ++i) {
              if (i)
                done.clipboard += L"\t";
              if (row[i].size() > 16u * 1024u * 1024u - done.clipboard.size())
                throw Error("Clipboard report exceeds 32 MiB; export a smaller "
                            "result.");
              done.clipboard += row[i];
            }
            done.clipboard += L"\r\n";
          }
          done.view.summary = L"Result copied to clipboard.";
        } else {
          std::string bytes = "\xef\xbb\xbf";
          auto add = [&](const Row &row) {
            check();
            for (size_t i = 0; i < row.size(); ++i) {
              auto field = storage::csv_cell(utf8(row[i]));
              if (field.size() + 3 > 256u * 1024u * 1024u - bytes.size())
                throw Error("Export exceeds 256 MiB.");
              if (i)
                bytes += ',';
              bytes += field;
            }
            bytes += "\r\n";
          };
          add({L"Velocity NetTools", job.summary});
          add(job.columns);
          for (auto &row : job.rows)
            add(row);
          check();
          storage::safe_save(job.path, bytes);
          done.view.summary =
              L"CSV export completed for the selected result snapshot.";
        }
      }
    } catch (const std::exception &e) {
      done.view.summary = L"Operation error: " + wide(e.what());
    }
    {
      std::lock_guard lock(worker->mutex);
      worker->running = false;
      worker->active.reset();
      if (!worker->closing && !job.cancelled->load())
        worker->complete = std::move(done);
    }
  }
}
void invalidate(Pane &p, unsigned mode, const wchar_t *reason) {
  for (unsigned i = 0; i < p.views.size(); ++i)
    if (i != mode && p.views[i].busy) {
      auto &other = p.views[i];
      ++other.revision;
      other.busy = false;
      other.ready = false;
      other.rows.clear();
      other.calculation.reset();
      other.proposal.reset();
      other.summary = L"Superseded by newer subnet work. Calculate again.";
    }
  auto &v = p.views[mode];
  ++v.revision;
  v.ready = false;
  v.busy = false;
  v.rows.clear();
  v.calculation.reset();
  v.proposal.reset();
  v.page = 0;
  v.summary = reason;
  std::lock_guard lock(p.worker->mutex);
  if (p.worker->active)
    p.worker->active->store(true);
  if (p.worker->pending)
    p.worker->pending->cancelled->store(true);
  p.worker->pending.reset();
  p.worker->complete.reset();
}
void submit(Pane &p, Job job) {
  auto &v = p.views[job.mode];
  v.busy = true;
  {
    std::lock_guard lock(p.worker->mutex);
    if (p.worker->active)
      p.worker->active->store(true);
    if (p.worker->pending)
      p.worker->pending->cancelled->store(true);
    p.worker->pending = std::move(job);
    p.worker->complete.reset();
  }
  p.worker->wake.notify_one();
  render(p);
}
void calculate(Pane &p, bool reallocate = false) {
  KillTimer(p.window, 2);
  capture(p);
  invalidate(p, p.mode, L"Calculating… use Stop to cancel.");
  try {
    Job job;
    job.mode = p.mode;
    job.revision = p.views[p.mode].revision;
    job.details = p.details;
    job.reallocate = reallocate;
    job.sequences = p.views[p.mode].sequences;
    size_t bytes = 0;
    for (auto &field : p.views[p.mode].fields) {
      auto value = utf8(field);
      if (value.size() > 16u * 1024u * 1024u - bytes)
        throw Error("Combined input exceeds 16 MiB.");
      bytes += value.size();
      job.fields.push_back(std::move(value));
    }
    submit(p, std::move(job));
  } catch (const std::exception &e) {
    p.views[p.mode].summary = L"Input error: " + wide(e.what());
    render(p);
  }
}
void poll(Pane &p) {
  std::optional<Completion> done;
  {
    std::lock_guard lock(p.worker->mutex);
    if (p.worker->complete) {
      done = std::move(p.worker->complete);
      p.worker->complete.reset();
    }
  }
  if (!done)
    return;
  auto &view = p.views[done->mode];
  if (done->revision != view.revision)
    return;
  if (done->kind == Job::calculate) {
    auto fields = std::move(view.fields);
    if (!done->view.ready)
      done->view.sequences = std::move(view.sequences);
    view = std::move(done->view);
    view.fields = std::move(fields);
    view.busy = false;
    if (done->mode == 0 && view.calculation && view.fields.size() > 1 &&
        view.fields[0].find(L'/') != std::wstring::npos) {
      view.fields[0] = wide(format(view.calculation->entered));
      view.fields[1] = std::to_wstring(view.calculation->prefix.length);
      if (p.mode == 0) {
        p.loading = true;
        SetWindowTextW(p.edits[0], view.fields[0].c_str());
        SetWindowTextW(p.edits[1], view.fields[1].c_str());
        p.loading = false;
      }
    }

  } else {
    view.busy = false;
    if (done->kind == Job::clipboard && !done->clipboard.empty()) {
      try {
        copy(p.window, done->clipboard);
      } catch (const std::exception &e) {
        done->view.summary = L"Clipboard error: " + wide(e.what());
      }
    }
    view.summary = std::move(done->view.summary);
  }
  if (done->mode == p.mode)
    render(p);
}

size_t snapshot_bytes(const PlanSnapshot &value) {
  size_t bytes = 0;
  for (auto &field : value.fields)
    bytes += (field.size() + 1) * sizeof(wchar_t);
  for (auto &entry : value.sequences)
    bytes += entry.first.size() + 64;
  return bytes;
}
void push_history(std::vector<PlanSnapshot> &stack, PlanSnapshot value) {
  constexpr size_t limit = 32u * 1024u * 1024u;
  auto bytes = snapshot_bytes(value);
  if (bytes > limit)
    throw Error("Apply history exceeds 32 MiB.");
  size_t total = bytes;
  for (auto &old : stack)
    total += snapshot_bytes(old);
  while (!stack.empty() && (stack.size() >= 20 || total > limit)) {
    total -= snapshot_bytes(stack.front());
    stack.erase(stack.begin());
  }
  stack.push_back(std::move(value));
}
void restore_history(Pane &p, bool redo) {
  if (p.mode != 2)
    return;
  auto &from = redo ? p.redo : p.undo;
  auto &to = redo ? p.undo : p.redo;
  if (from.empty())
    return;
  capture(p);
  push_history(to, {p.views[2].fields, p.views[2].sequences});
  auto prior = std::move(from.back());
  from.pop_back();
  invalidate(p, 2,
             L"Plan restored. Preview again to calculate derived results.");
  p.views[2].fields = std::move(prior.fields);
  p.views[2].sequences = std::move(prior.sequences);
  build(p, 2, false);
  PostMessageW(GetParent(p.window), WM_APP + 10, 0, 0);
}
void apply(Pane &p) {
  auto &v = p.views[2];
  if (p.mode != 2 || !v.ready || v.busy || !v.proposal)
    return;
  auto revision = v.revision;
  if (v.proposal->partial &&
      MessageBoxW(p.window,
                  L"This proposal leaves requirements unallocated. Apply "
                  L"successful assignments and keep the remaining requests?",
                  L"Apply partial proposal",
                  MB_YESNO | MB_ICONQUESTION) != IDYES)
    return;
  if (v.revision != revision || !v.proposal)
    return;
  capture(p);
  push_history(p.undo, {v.fields, v.sequences});
  p.redo.clear();
  auto applied = std::move(v.applied_text);
  p.loading = true;
  SetWindowTextW(p.edits[1], applied.c_str());
  p.loading = false;
  capture(p);
  invalidate(p, 2,
             L"Proposal applied. Undo/Redo retains up to 20 Apply transactions "
             L"within 32 MiB. Save to retain assignments.");
  PostMessageW(GetParent(p.window), WM_APP + 10, 0, 0);
  render(p);
}

void run_command(Pane &p, int id) {
  auto &v = p.views[p.mode];
  try {
    switch (id) {
    case 100:
      calculate(p);
      break;
    case 101:
    case 102: {
      if (!v.ready || v.busy)
        break;
      Job job;
      job.kind = id == 101 ? Job::clipboard : Job::export_csv;
      job.mode = p.mode;
      job.revision = v.revision;
      job.rows = v.rows;
      job.columns = v.columns;
      job.summary = v.summary;
      if (id == 102) {
        job.path = file_dialog(p.window, true, true);
        if (job.path.empty())
          break;
      }
      submit(p, std::move(job));
      break;
    }
    case 103:
      show_help(p.window, specs[p.mode].help);
      break;
    case 104:
    case 105:
      if (p.mode == 0 && v.calculation) {
        auto a = id == 104 ? v.calculation->previous : v.calculation->next;
        if (a) {
          auto addr = wide(format(*a));
          auto prefix = std::to_wstring(v.calculation->prefix.length);
          p.loading = true;
          SetWindowTextW(p.edits[0], addr.c_str());
          SetWindowTextW(p.edits[1], prefix.c_str());
          p.loading = false;
          PostMessageW(GetParent(p.window), WM_APP + 10, 0, 0);
          calculate(p);
        }
      } else if (p.mode == 1 && v.ready && !v.busy) {
        auto index = v.split_index;
        if (id == 104)
          index = index > Integer(200) ? index - Integer(200) : Integer();
        else {
          index = index + Integer(v.rows.size());
          if (index >= v.split_total)
            break;
        }
        p.loading = true;
        SetWindowTextW(p.edits[2], wide(index.str()).c_str());
        p.loading = false;
        PostMessageW(GetParent(p.window), WM_APP + 10, 0, 0);
        calculate(p);
      } else {
        if (id == 104 && v.page)
          v.page--;
        else if (id == 105 && (v.page + 1) * 200 < v.rows.size())
          v.page++;
        render(p);
      }
      break;
    case 106:
      p.details = !p.details;
      calculate(p);
      break;
    case 107:
      apply(p);
      break;
    case 108:
      calculate(p, true);
      break;
    case 113:
      if (v.page)
        --v.page;
      render(p);
      break;
    case 114:
      if ((v.page + 1) * 200 < v.rows.size())
        ++v.page;
      render(p);
      break;
    case 110:
      stop_subnet(p.window);
      break;
    case 111:
      restore_history(p, false);
      break;
    case 112:
      restore_history(p, true);
      break;
    case 109:
      build(p, p.mode == 1 ? 2 : 1);
      PostMessageW(GetParent(p.window), WM_APP + 11, p.mode, 0);
      break;
    }
  } catch (const std::exception &e) {
    error(p.window, e);
  }
}
LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  auto p = state(h);
  if (msg == WM_NCCREATE) {
    p = new Pane;
    p->window = h;
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
  }
  if (!p)
    return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
  case WM_CREATE: {
    NONCLIENTMETRICSW m{sizeof(m)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(m), &m, 0);
    p->base_font = m.lfMessageFont;
    HDC dc = GetDC(h);
    p->base_dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(h, dc);
    p->dpi = p->base_dpi;
    set_font(*p);
    p->title = make(*p, L"STATIC", L"", SS_LEFT, 0);
    p->hint = make(*p, L"STATIC", L"", SS_LEFT, 0);
    p->tabs = make(*p, WC_TABCONTROLW, L"", WS_TABSTOP | TCS_MULTILINE, 10);
    for (auto s : {L"Calculator", L"Split / VLSM", L"Aggregate", L"Compare"}) {
      TCITEMW t{};
      t.mask = TCIF_TEXT;
      t.pszText = const_cast<wchar_t *>(s);
      TabCtrl_InsertItem(p->tabs, TabCtrl_GetItemCount(p->tabs), &t);
    }
    const wchar_t *names[] = {L"&Calculate",  L"&Copy",        L"&Export CSV",
                              L"&Help",       L"Previous",     L"Next",
                              L"Bits / hex",  L"&Apply",       L"Reallocate",
                              L"&VLSM",       L"&Stop",        L"&Undo apply",
                              L"&Redo apply", L"Prev results", L"Next results"};
    for (int i = 0; i < 15; i++)
      p->buttons[i] = make(*p, L"BUTTON", names[i],
                           WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY, 100 + i);
    p->summary = make(*p, L"EDIT", L"",
                      ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL |
                          WS_TABSTOP,
                      11);
    p->list = make(*p, WC_LISTVIEWW, L"",
                   LVS_REPORT | LVS_SHOWSELALWAYS | WS_TABSTOP, 12);
    ListView_SetExtendedListViewStyle(
        p->list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    SetWindowTheme(p->list, L"Explorer", nullptr);
    try {
      std::thread(worker_loop, p->worker).detach();
    } catch (...) {
      return -1;
    }
    SetTimer(h, 1, 40, nullptr);
    build(*p, 0);
    calculate(*p);
    return 0;
  }
  case WM_TIMER:
    if (wp == 1)
      poll(*p);
    else if (wp == 2) {
      KillTimer(h, 2);
      if (p->mode == 0)
        calculate(*p);
    }
    return 0;
  case WM_SIZE:
    layout(*p);
    return 0;
  case WM_DPICHANGED:
    p->dpi = HIWORD(wp);
    set_font(*p);
    layout(*p);
    return 0;
  case WM_VSCROLL: {
    SCROLLINFO si{sizeof(si), SIF_ALL};
    GetScrollInfo(h, SB_VERT, &si);
    int next = p->scroll;
    switch (LOWORD(wp)) {
    case SB_TOP:
      next = 0;
      break;
    case SB_BOTTOM:
      next = si.nMax;
      break;
    case SB_LINEUP:
      next -= scale(*p, 24);
      break;
    case SB_LINEDOWN:
      next += scale(*p, 24);
      break;
    case SB_PAGEUP:
      next -= si.nPage;
      break;
    case SB_PAGEDOWN:
      next += si.nPage;
      break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
      next = si.nTrackPos;
      break;
    }
    scroll_to(*p, next);
    return 0;
  }
  case WM_MOUSEWHEEL:
    scroll_to(*p, p->scroll - MulDiv(GET_WHEEL_DELTA_WPARAM(wp), scale(*p, 72),
                                     WHEEL_DELTA));
    return 0;
  case WM_HELP:
    show_help(h, specs[p->mode].help);
    return TRUE;
  case WM_NOTIFY:
    if (reinterpret_cast<NMHDR *>(lp)->code == NM_SETFOCUS)
      reveal(*p, reinterpret_cast<NMHDR *>(lp)->hwndFrom);
    if (reinterpret_cast<NMHDR *>(lp)->hwndFrom == p->tabs &&
        reinterpret_cast<NMHDR *>(lp)->code == TCN_SELCHANGE) {
      int i = TabCtrl_GetCurSel(p->tabs);
      build(*p, i < 2 ? unsigned(i) : unsigned(i + 1));
      PostMessageW(GetParent(h), WM_APP + 11, p->mode, 0);
    }
    return 0;
  case WM_COMMAND:
    if ((LOWORD(wp) >= 1000 && HIWORD(wp) == EN_SETFOCUS) ||
        (LOWORD(wp) >= 100 && LOWORD(wp) < 115 && HIWORD(wp) == BN_SETFOCUS)) {
      reveal(*p, reinterpret_cast<HWND>(lp));
      return 0;
    }
    if (LOWORD(wp) >= 1000 && HIWORD(wp) == EN_CHANGE && !p->loading) {
      capture(*p);
      invalidate(*p, p->mode, L"Inputs changed. Calculate or preview again.");
      PostMessageW(GetParent(h), WM_APP + 10, 0, 0);
      KillTimer(h, 2);
      if (p->mode == 0)
        SetTimer(h, 2, 150, nullptr);
      render(*p);
    } else if (LOWORD(wp) < 1000 && HIWORD(wp) == BN_CLICKED)
      run_command(*p, LOWORD(wp));
    return 0;
  case WM_NCDESTROY:
    KillTimer(h, 1);
    KillTimer(h, 2);
    {
      std::lock_guard lock(p->worker->mutex);
      p->worker->closing = true;
      if (p->worker->active)
        p->worker->active->store(true);
      p->worker->pending.reset();
      p->worker->complete.reset();
    }
    p->worker->wake.notify_one();
    DeleteObject(p->font);
    SetWindowLongPtrW(h, GWLP_USERDATA, 0);
    delete p;
    break;
  }
  return DefWindowProcW(h, msg, wp, lp);
}
} // namespace
HWND create_subnet_pane(HWND parent) {
  WNDCLASSW wc{};
  wc.lpfnWndProc = proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"VEU.SubnetPane";
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  RegisterClassW(&wc);
  return CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName,
                         L"Subnet workbench",
                         WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL, 0, 0, 0, 0,
                         parent, nullptr, wc.hInstance, nullptr);
}
void stop_subnet(HWND h) {
  if (auto p = state(h)) {
    KillTimer(h, 2);
    for (unsigned i = 0; i < p->views.size(); ++i)
      if (p->views[i].busy)
        invalidate(*p, i, L"Cancelled. Calculate or preview again.");
    {
      std::lock_guard lock(p->worker->mutex);
      if (p->worker->active)
        p->worker->active->store(true);
      p->worker->pending.reset();
      p->worker->complete.reset();
    }
    render(*p);
  }
}
bool subnet_busy(HWND h) {
  auto p = state(h);
  if (!p)
    return false;
  std::lock_guard lock(p->worker->mutex);
  return p->worker->running || p->worker->pending.has_value() ||
         p->worker->complete.has_value();
}
void select_subnet_tool(HWND h, unsigned mode) {
  if (mode > 4)
    return;
  build(*state(h), mode);
}
std::map<std::string, std::string> subnet_plan_fields(HWND h) {
  auto &p = *state(h);
  capture(p);
  std::map<std::string, std::string> result;
  for (size_t t = 0; t < p.views.size(); t++)
    for (size_t i = 0; i < p.views[t].fields.size(); i++)
      result["subnet." + std::to_string(t) + "." + std::to_string(i)] =
          utf8(p.views[t].fields[i]);
  std::vector<std::pair<uint64_t, std::string>> order;
  for (auto &entry : p.views[2].sequences)
    order.emplace_back(entry.second, entry.first);
  std::sort(order.begin(), order.end());
  std::string ids;
  for (auto &entry : order)
    ids += entry.second + "\n";
  result["subnet.vlsm.order"] = std::move(ids);
  return result;
}
void load_subnet_plan_fields(HWND h,
                             const std::map<std::string, std::string> &fields) {
  auto &p = *state(h);
  auto staged = p.views;
  for (size_t t = 0; t < staged.size(); t++) {
    staged[t] = View{};
    for (size_t i = 0; i < specs[t].fields.size(); i++) {
      auto it =
          fields.find("subnet." + std::to_string(t) + "." + std::to_string(i));
      staged[t].fields.push_back(it == fields.end() ? specs[t].fields[i].second
                                                    : wide(it->second));
    }
  }
  if (auto found = fields.find("subnet.vlsm.order"); found != fields.end()) {
    if (found->second.size() > 16u * 1024u * 1024u)
      throw Error("Saved VLSM order exceeds the input limit.");
    std::istringstream input(found->second);
    std::string id;
    uint64_t sequence = 0;
    while (std::getline(input, id)) {
      if (id.empty() || id.find('|') != std::string::npos ||
          sequence >= 10000 ||
          !staged[2].sequences.emplace(id, sequence++).second)
        throw Error("Invalid saved VLSM insertion order.");
    }
  }
  stop_subnet(h);
  for (size_t i = 0; i < staged.size(); ++i)
    staged[i].revision = p.views[i].revision + 1;
  p.undo.clear();
  p.redo.clear();
  p.views = std::move(staged);
  build(p, p.mode, false);
}
} // namespace veu
