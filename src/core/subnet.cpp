#include "veu/subnet.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <sstream>

namespace veu::subnet {
namespace {
constexpr std::size_t input_limit = 16u * 1024u * 1024u;
std::string_view bounded(std::string_view text) {
  if (text.size() > input_limit)
    throw Error("Input exceeds the 16 MiB limit.");
  return text;
}
bool space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
         c == '\v';
}
std::string_view trim(std::string_view s) {
  while (!s.empty() && space(s.front()))
    s.remove_prefix(1);
  while (!s.empty() && space(s.back()))
    s.remove_suffix(1);
  return s;
}
void token(std::string_view s) {
  if (s.empty())
    throw Error("Input is incomplete.");
  if (s.size() > input_limit)
    throw Error("Input exceeds the 16 MiB limit.");
  for (unsigned char c : s)
    if (c <= 32 || c >= 127)
      throw Error("Use an ASCII literal without embedded whitespace or NULs.");
}
Integer shift(const Integer &n, unsigned bits) {
  Integer out;
  for (unsigned i = 0; i < 160; ++i)
    if (n.bit(i)) {
      if (i + bits >= 160)
        throw Error("Exact integer overflow.");
      out.words[(i + bits) / 32] |= std::uint32_t(1) << ((i + bits) % 32);
    }
  return out;
}
Integer low_clear(Integer n, unsigned bits) {
  for (unsigned i = 0; i < bits; ++i)
    n.words[i / 32] &= ~(std::uint32_t(1) << (i % 32));
  return n;
}
Address addr(Family f, const Integer &n) {
  if (n >= Integer::power2(width(f)))
    throw Error("An exclusive endpoint is not an address.");
  return {f, n};
}
void validate(const Prefix &p) {
  auto w = width(p.network.family);
  if (p.length > w || p.network.value >= Integer::power2(w) ||
      low_clear(p.network.value, w - p.length) != p.network.value)
    throw Error("Prefix must be a canonical aligned network.");
}
void memory(const Context &ctx, std::size_t count, std::size_t bytes) {
  ctx.check();
  if (bytes && count > ctx.max_work_bytes / bytes)
    throw Error("Calculation exceeds the working-memory budget.");
}
void rows_limit(const Context &ctx, std::size_t n) {
  ctx.check();
  if (n > std::min<std::size_t>(ctx.max_rows, 10000))
    throw Error("Operation exceeds the 10,000 input-row limit.");
}
void same(Family a, Family b) {
  if (a != b)
    throw Error("Address-family mismatch; IPv4 and IPv6 must not be mixed.");
}
Integer min(const Integer &a, const Integer &b) { return a < b ? a : b; }
Integer max(const Integer &a, const Integer &b) { return a < b ? b : a; }
std::vector<std::string_view> fields(std::string_view s, char delimiter) {
  std::vector<std::string_view> out;
  size_t at = 0;
  for (;;) {
    auto pos = s.find(delimiter, at);
    out.push_back(s.substr(at, pos == s.npos ? s.size() - at : pos - at));
    if (pos == s.npos)
      break;
    at = pos + 1;
  }
  return out;
}
unsigned decimal_small(std::string_view s, unsigned bound) {
  if (s.empty() || s.size() > 39)
    throw Error("Decimal token must contain 1 to 39 digits.");
  unsigned n = 0;
  for (char c : s) {
    if (c < '0' || c > '9')
      throw Error("Use an unsigned decimal integer.");
    if (n > bound / 10 || (n == bound / 10 && unsigned(c - '0') > bound % 10))
      throw Error("Prefix or octet is out of range.");
    n = n * 10 + unsigned(c - '0');
  }
  return n;
}
std::vector<Interval> normalise(const std::vector<Interval> &input,
                                const Context &ctx, bool enforce_rows = true) {
  if (enforce_rows)
    rows_limit(ctx, input.size());
  memory(ctx, input.size(), sizeof(Interval) * 4);
  if (input.empty())
    return {};
  auto f = input.front().first.family;
  auto limit = Integer::power2(width(f));
  for (size_t i = 0; i < input.size(); ++i) {
    ctx.check();
    auto &r = input[i];
    if (r.first.family != f)
      throw Error("Row " + std::to_string(i + 1) +
                  ": address-family mismatch.");
    if (r.first.value >= limit || r.end > limit || r.first.value >= r.end)
      throw Error("Row " + std::to_string(i + 1) +
                  ": invalid or reversed interval.");
  }
  auto out = input;
  std::size_t comparisons = 0;
  std::sort(out.begin(), out.end(), [&](const Interval &a, const Interval &b) {
    if ((++comparisons % 1024) == 0)
      ctx.check();
    return a.first.value < b.first.value ||
           (a.first.value == b.first.value && a.end < b.end);
  });
  ctx.check();
  size_t n = 0;
  for (auto &r : out) {
    ctx.check();
    if (n && r.first.value <= out[n - 1].end)
      out[n - 1].end = max(out[n - 1].end, r.end);
    else
      out[n++] = r;
  }
  out.resize(n);
  return out;
}
std::vector<Interval> difference(const std::vector<Interval> &a,
                                 const std::vector<Interval> &b,
                                 const Context &ctx) {
  std::vector<Interval> out;
  memory(ctx, a.size() + b.size(), sizeof(Interval) * 4);
  out.reserve(a.size() + b.size());
  size_t j = 0;
  for (auto &x : a) {
    ctx.check();
    auto cursor = x.first.value;
    while (j < b.size() && b[j].end <= cursor) {
      ctx.check();
      ++j;
    }
    size_t k = j;
    while (k < b.size() && b[k].first.value < x.end) {
      ctx.check();
      if (b[k].first.value > cursor)
        out.push_back({{x.first.family, cursor}, min(b[k].first.value, x.end)});
      cursor = max(cursor, b[k].end);
      if (cursor >= x.end)
        break;
      ++k;
    }
    if (cursor < x.end)
      out.push_back({{x.first.family, cursor}, x.end});
  }
  return out;
}
SetResult result(std::vector<Interval> ranges, const Context &ctx) {
  SetResult out;
  out.ranges = std::move(ranges);
  auto base = out.ranges.capacity() * sizeof(Interval);
  if (base > ctx.max_work_bytes)
    throw Error("Calculation exceeds the working-memory budget.");
  for (auto &r : out.ranges) {
    auto cursor = r.first.value;
    auto w = width(r.first.family);
    out.count = out.count + (r.end - cursor);
    while (cursor < r.end) {
      ctx.check();
      unsigned bits = 0;
      while (bits < w && !cursor.bit(bits))
        ++bits;
      while (Integer::power2(bits) > r.end - cursor)
        --bits;
      // Allow the vector's doubling growth and retained interval storage before
      // allocating.
      if (out.cidrs.size() == out.cidrs.capacity()) {
        size_t cap = std::max<std::size_t>(16, out.cidrs.capacity() * 2);
        if (cap + out.cidrs.capacity() >
            (ctx.max_work_bytes - base) / sizeof(Prefix))
          throw Error("Calculation exceeds the working-memory budget.");
        out.cidrs.reserve(cap);
      }
      out.cidrs.push_back({{r.first.family, cursor}, w - bits});
      cursor = cursor + Integer::power2(bits);
    }
  }
  return out;
}
void combined_memory(const Context &ctx,
                     std::initializer_list<const SetResult *> sets) {
  size_t used = 0;
  for (auto s : sets) {
    size_t n = s->ranges.capacity() * sizeof(Interval) +
               s->cidrs.capacity() * sizeof(Prefix);
    if (n > ctx.max_work_bytes - used)
      throw Error("Calculation exceeds the working-memory budget.");
    used += n;
  }
  ctx.check();
}
// Reserve space for input, scratch vectors and retained earlier results before
// any subsequent result grows. Prefix-vector reallocation also accounts for
// simultaneous old and new buffers.
std::size_t retained(const SetResult &s) {
  return s.ranges.capacity() * sizeof(Interval) +
         s.cidrs.capacity() * sizeof(Prefix);
}
Context remaining(const Context &ctx, std::size_t used) {
  ctx.check();
  if (used > ctx.max_work_bytes)
    throw Error("Calculation exceeds the working-memory budget.");
  Context out = ctx;
  out.max_work_bytes -= used;
  return out;
}
unsigned required(const Request &r, const Prefix &p) {
  if (r.kind == RequestKind::ipv6_prefix) {
    if (p.network.family != Family::ipv6)
      throw Error("IPv6 prefix request requires an IPv6 parent.");
    if (r.child_prefix > 128 || r.child_prefix < p.length)
      throw Error("Requested prefix does not fit the parent.");
    return r.child_prefix;
  }
  if (p.network.family != Family::ipv4)
    throw Error("IPv4 capacity request requires an IPv4 parent.");
  unsigned q = 32;
  if (r.kind == RequestKind::ipv4_lan) {
    if (r.hosts < Integer(1) || r.hosts > Integer(4294967294ULL))
      throw Error("LAN host requirement must be 1 through 4294967294.");
    q = 30;
    while (Integer::power2(32 - q) - Integer(2) < r.hosts)
      --q;
  } else if (r.kind == RequestKind::ipv4_point_to_point) {
    if (r.hosts < Integer(1) || r.hosts > Integer(2))
      throw Error("Point-to-point requires one or two endpoints.");
    q = 31;
  } else if (r.kind == RequestKind::ipv4_host_route) {
    if (r.hosts != Integer(1))
      throw Error("A host route requests exactly one address.");
    q = 32;
  } else
    throw Error("Unknown request kind.");
  if (q < p.length)
    throw Error("Requested block is larger than the parent.");
  return q;
}
} // namespace
Integer::Integer(std::uint64_t value) {
  words[0] = std::uint32_t(value);
  words[1] = std::uint32_t(value >> 32);
}
bool Integer::bit(unsigned i) const {
  return i < 160 && ((words[i / 32] >> (i % 32)) & 1) != 0;
}
Integer Integer::power2(unsigned b) {
  if (b >= 160)
    throw Error("Exact integer power exceeds 160 bits.");
  Integer n;
  n.words[b / 32] = std::uint32_t(1) << (b % 32);
  return n;
}
bool operator<(const Integer &a, const Integer &b) {
  for (int i = 4; i >= 0; --i) {
    if (a.words[i] != b.words[i])
      return a.words[i] < b.words[i];
  }
  return false;
}
bool operator>(const Integer &a, const Integer &b) { return b < a; }
bool operator<=(const Integer &a, const Integer &b) { return !(b < a); }
bool operator>=(const Integer &a, const Integer &b) { return !(a < b); }
Integer operator+(const Integer &a, const Integer &b) {
  Integer n;
  std::uint64_t carry = 0;
  for (unsigned i = 0; i < 5; ++i) {
    carry += std::uint64_t(a.words[i]) + b.words[i];
    n.words[i] = std::uint32_t(carry);
    carry >>= 32;
  }
  if (carry)
    throw Error("Exact integer overflow.");
  return n;
}
Integer operator-(const Integer &a, const Integer &b) {
  if (a < b)
    throw Error("Exact integer underflow.");
  Integer n;
  std::uint64_t borrow = 0;
  for (unsigned i = 0; i < 5; ++i) {
    auto rhs = std::uint64_t(b.words[i]) + borrow;
    n.words[i] = std::uint32_t(std::uint64_t(a.words[i]) - rhs);
    borrow = std::uint64_t(a.words[i]) < rhs ? 1 : 0;
  }
  return n;
}
Integer Integer::decimal(std::string_view s) {
  s = trim(s);
  token(s);
  if (s.size() > 39)
    throw Error("Decimal token exceeds 39 digits.");
  Integer n;
  for (char c : s) {
    if (c < '0' || c > '9')
      throw Error("Use an unsigned decimal integer.");
    n = shift(n, 3) + shift(n, 1) + Integer(unsigned(c - '0'));
  }
  return n;
}
std::string Integer::str() const {
  if (*this == Integer())
    return "0";
  Integer n = *this;
  std::string s;
  while (n != Integer()) {
    std::uint64_t rem = 0;
    for (int i = 4; i >= 0; --i) {
      auto v = (rem << 32) | n.words[i];
      n.words[i] = std::uint32_t(v / 10);
      rem = v % 10;
    }
    s.push_back(char('0' + rem));
  }
  std::reverse(s.begin(), s.end());
  return s;
}
void Context::check() const {
  if (cancelled && cancelled())
    throw Error("Calculation cancelled.");
}
unsigned width(Family f) {
  if (f == Family::ipv4)
    return 32;
  if (f == Family::ipv6)
    return 128;
  throw Error("Unknown address family.");
}
Address parse_address(std::string_view text) {
  auto s = trim(bounded(text));
  token(s);
  if (s.size() > 64)
    throw Error("Address literal is too long.");
  if (s.find('%') != s.npos)
    throw Error("Subnet arithmetic requires an unscoped literal; zone "
                "identifiers are unsupported.");
  if (s.find_first_of("[]/") != s.npos || s.find("://") != s.npos)
    throw Error("Use an unscoped address literal without brackets, ports, URL "
                "or prefix.");
  if (s.find(':') == s.npos) {
    auto parts = fields(s, '.');
    if (parts.size() != 4)
      throw Error("IPv4 requires exactly four decimal octets.");
    Integer n;
    for (auto part : parts) {
      if (part.size() > 1 && part.front() == '0')
        throw Error("IPv4 octets must not have leading zeros.");
      if (part.size() > 3)
        throw Error("IPv4 octet is out of range.");
      n = shift(n, 8) + Integer(decimal_small(part, 255));
    }
    return {Family::ipv4, n};
  }
  if (s.find('.') != s.npos && s.find('.') < s.rfind(':'))
    throw Error("IPv4 tail must be final; ports are unsupported.");
  auto compressed = s.find("::");
  if (compressed != s.npos && s.find("::", compressed + 2) != s.npos)
    throw Error("IPv6 permits only one :: compression.");
  std::vector<unsigned> left, right;
  auto groups = [&](std::string_view side, std::vector<unsigned> &out,
                    bool tail_allowed) {
    if (side.empty())
      return;
    auto parts = fields(side, ':');
    for (size_t i = 0; i < parts.size(); ++i) {
      auto part = parts[i];
      if (part.empty())
        throw Error("Malformed IPv6 group.");
      if (part.find('.') != part.npos) {
        if (!tail_allowed || i + 1 != parts.size())
          throw Error("IPv4 tail must occupy the final 32 bits.");
        auto v = parse_address(part);
        out.push_back(v.value.words[0] >> 16);
        out.push_back(v.value.words[0] & 65535);
      } else {
        if (part.size() > 4)
          throw Error("IPv6 groups contain one to four hexadecimal digits.");
        unsigned n = 0;
        for (char c : part) {
          unsigned d;
          if (c >= '0' && c <= '9')
            d = c - '0';
          else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
          else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
          else
            throw Error(
                "Invalid IPv6 hexadecimal group; ports are unsupported.");
          n = n * 16 + d;
        }
        out.push_back(n);
      }
    }
  };
  if (compressed == s.npos) {
    groups(s, left, true);
    if (left.size() != 8)
      throw Error("IPv6 expanded length must be eight groups.");
  } else {
    groups(s.substr(0, compressed), left, false);
    groups(s.substr(compressed + 2), right, true);
    if (left.size() + right.size() >= 8)
      throw Error(":: must expand to at least one zero group.");
    left.resize(8 - right.size(), 0);
    left.insert(left.end(), right.begin(), right.end());
  }
  Integer n;
  for (auto g : left)
    n = shift(n, 16) + Integer(g);
  return {Family::ipv6, n};
}
unsigned parse_prefix(std::string_view text, Family f) {
  auto s = trim(bounded(text));
  token(s);
  if (s.front() == '/')
    s.remove_prefix(1);
  if (s.find('.') != s.npos) {
    if (f != Family::ipv4)
      throw Error("IPv6 accepts a prefix length only.");
    auto mask = parse_address(s);
    unsigned p = 0;
    bool zero = false;
    for (int i = 31; i >= 0; --i) {
      if (mask.value.bit(unsigned(i))) {
        if (zero)
          throw Error("IPv4 mask must be contiguous ones followed by zeros.");
        ++p;
      } else
        zero = true;
    }
    return p;
  }
  return decimal_small(s, width(f));
}
Prefix parse_cidr(std::string_view text, bool aligned) {
  auto s = trim(bounded(text));
  token(s);
  auto slash = s.find('/');
  if (slash == s.npos)
    throw Error("CIDR prefix is required.");
  if (s.find('/', slash + 1) != s.npos)
    throw Error("CIDR contains multiple prefix separators.");
  auto a = parse_address(s.substr(0, slash));
  auto suffix = s.substr(slash + 1);
  token(suffix);
  if (suffix.find('.') != suffix.npos)
    throw Error("CIDR suffix must be a decimal prefix, not a dotted mask.");
  unsigned p = decimal_small(suffix, width(a.family));
  auto n = low_clear(a.value, width(a.family) - p);
  if (aligned && n != a.value)
    throw Error("Pinned or reserved CIDR must be aligned; host bits are set.");
  return {{a.family, n}, p};
}
std::string format(const Address &a) {
  addr(a.family, a.value);
  if (a.family == Family::ipv4) {
    auto n = a.value.words[0];
    return std::to_string(n >> 24) + "." + std::to_string((n >> 16) & 255) +
           "." + std::to_string((n >> 8) & 255) + "." + std::to_string(n & 255);
  }
  if (a.value.words[3] == 0 && a.value.words[2] == 0 &&
      a.value.words[1] == 65535)
    return "::ffff:" + format(Address{Family::ipv4, Integer(a.value.words[0])});
  unsigned g[8];
  for (unsigned i = 0; i < 8; ++i)
    g[i] = (a.value.words[(7 - i) / 2] >> ((7 - i) % 2 * 16)) & 65535;
  unsigned best = 8, len = 0;
  for (unsigned i = 0; i < 8;) {
    if (g[i]) {
      ++i;
      continue;
    }
    auto j = i;
    while (j < 8 && !g[j])
      ++j;
    if (j - i >= 2 && j - i > len) {
      best = i;
      len = j - i;
    }
    i = j;
  }
  std::ostringstream out;
  out << std::hex;
  for (unsigned i = 0; i < 8;) {
    if (i == best) {
      out << "::";
      i += len;
      if (i == 8)
        break;
    } else {
      if (i && i != best + len)
        out << ':';
      out << g[i];
      ++i;
    }
  }
  return out.str();
}
std::string format(const Prefix &p) {
  validate(p);
  return format(p.network) + "/" + std::to_string(p.length);
}
Integer size(const Prefix &p) {
  validate(p);
  return Integer::power2(width(p.network.family) - p.length);
}
Interval interval(const Prefix &p) {
  return {p.network, p.network.value + size(p)};
}
Calculation calculate(std::string_view text, std::string_view prefix) {
  Calculation c;
  c.original_input = std::string(bounded(text));
  auto s = trim(bounded(text));
  auto slash = s.find('/');
  if (slash != s.npos) {
    c.prefix = parse_cidr(s);
    c.entered = parse_address(s.substr(0, slash));
  } else {
    c.entered = parse_address(s);
    unsigned p = parse_prefix(prefix, c.entered.family);
    c.prefix = {{c.entered.family,
                 low_clear(c.entered.value, width(c.entered.family) - p)},
                p};
  }
  auto f = c.entered.family;
  auto n = c.prefix.network.value;
  auto b = size(c.prefix);
  auto end = n + b;
  auto limit = Integer::power2(width(f));
  c.total = b;
  c.offset = c.entered.value - n;
  c.last = addr(f, end - Integer(1));
  if (n >= b)
    c.previous = addr(f, n - b + c.offset);
  if (end < limit)
    c.next = addr(f, end + c.offset);
  if (f == Family::ipv4) {
    c.mask = addr(f, limit - b);
    c.wildcard = addr(f, b - Integer(1));
    if (c.prefix.length <= 30) {
      c.capacity = b - Integer(2);
      c.broadcast = c.last;
      c.first_host = addr(f, n + Integer(1));
      c.last_host = addr(f, end - Integer(2));
      c.capacity_note = "Conventional IPv4 LAN host capacity; deployment "
                        "depends on address roles.";
    } else {
      c.capacity = b;
      c.first_host = c.prefix.network;
      c.last_host = c.last;
      c.capacity_note = c.prefix.length == 31
                            ? "Two point-to-point endpoints; directed "
                              "broadcast is not applicable."
                            : "One address / host route; directed broadcast is "
                              "not applicable.";
    }
  } else
    c.capacity_note =
        c.prefix.length == 127
            ? "Two IPv6 addresses. /127 is for router point-to-point links "
              "with Subnet-Router anycast disabled; no broadcast or generic "
              "usable-host count."
        : c.prefix.length == 128
            ? "One IPv6 address / host route; no broadcast or generic "
              "usable-host count."
            : "IPv6 total address count; broadcast and generic usable-host "
              "capacity are not applicable.";
  return c;
}
Integer split_count(const Prefix &p, unsigned q) {
  validate(p);
  if (q < p.length || q > width(p.network.family))
    throw Error(
        "Child prefix must be between parent prefix and address width.");
  return Integer::power2(q - p.length);
}
unsigned split_prefix(const Prefix &p, const Integer &parts) {
  validate(p);
  if (parts == Integer())
    throw Error("Part count must be positive.");
  for (unsigned i = 0; i <= width(p.network.family) - p.length; ++i)
    if (parts == Integer::power2(i))
      return p.length + i;
  throw Error("Part count must be a power of two that fits the parent.");
}
Prefix split_at(const Prefix &p, unsigned q, const Integer &i) {
  auto count = split_count(p, q);
  if (i >= count)
    throw Error("Split index is outside the child count.");
  return {{p.network.family,
           p.network.value + shift(i, width(p.network.family) - q)},
          q};
}
std::vector<Prefix> split_page(const Prefix &p, unsigned q,
                               const Integer &first, size_t count,
                               const Context &ctx) {
  ctx.check();
  if (count > 200)
    throw Error("Result pages are limited to 200 rows.");
  auto total = split_count(p, q);
  if (first >= total)
    throw Error("Split page index is outside the child count.");
  memory(ctx, count, sizeof(Prefix));
  std::vector<Prefix> out;
  out.reserve(count);
  auto i = first;
  while (out.size() < count && i < total) {
    ctx.check();
    out.push_back(split_at(p, q, i));
    i = i + Integer(1);
  }
  return out;
}
Interval parse_range(std::string_view first, std::string_view last) {
  auto a = parse_address(first), b = parse_address(last);
  same(a.family, b.family);
  if (b.value < a.value)
    throw Error("Range start must not exceed the last address.");
  return {a, b.value + Integer(1)};
}
std::vector<Interval> parse_set(std::string_view text, const Context &ctx) {
  ctx.check();
  if (text.size() > input_limit)
    throw Error("Input exceeds the 16 MiB limit.");
  std::vector<Interval> out;
  size_t pos = 0, row = 0;
  while (pos < text.size()) {
    ctx.check();
    auto end = text.find('\n', pos);
    if (end == text.npos)
      end = text.size();
    auto s = trim(text.substr(pos, end - pos));
    pos = end + 1;
    ++row;
    if (s.empty())
      continue;
    rows_limit(ctx, out.size() + 1);
    memory(ctx, out.size() + 1, sizeof(Interval) * 2);
    try {
      auto dash = s.find('-');
      out.push_back(dash == s.npos
                        ? interval(parse_cidr(s))
                        : parse_range(s.substr(0, dash), s.substr(dash + 1)));
      if (out.size() > 1)
        same(out.front().first.family, out.back().first.family);
    } catch (const Error &e) {
      throw Error("Row " + std::to_string(row) + ": " + e.what());
    }
  }
  return out;
}
SetResult exact_union(const std::vector<Interval> &input, const Context &ctx) {
  rows_limit(ctx, input.size());
  auto output_budget = remaining(ctx, input.size() * sizeof(Interval) * 4);
  return result(normalise(input, ctx), output_budget);
}
AggregateResult aggregate(const std::vector<Interval> &input,
                          const Context &ctx) {
  AggregateResult out;
  out.exact = exact_union(input, ctx);
  if (out.exact.ranges.empty())
    return out;
  auto a = out.exact.ranges.front().first;
  auto last = out.exact.ranges.back().end - Integer(1);
  auto w = width(a.family);
  unsigned p = 0;
  while (p < w && a.value.bit(w - p - 1) == last.bit(w - p - 1))
    ++p;
  out.covering = Prefix{{a.family, low_clear(a.value, w - p)}, p};
  auto output_budget =
      remaining(ctx, input.size() * sizeof(Interval) * 4 + retained(out.exact));
  out.extra =
      result(difference({interval(*out.covering)}, out.exact.ranges, ctx),
             output_budget);
  combined_memory(ctx, {&out.exact, &out.extra});
  return out;
}
CompareResult compare(const std::vector<Interval> &ai,
                      const std::vector<Interval> &bi, const Context &ctx) {
  if (ai.size() > 10000 || bi.size() > 10000)
    throw Error("Operation exceeds the 10,000 input-row limit.");
  rows_limit(ctx, ai.size() + bi.size());
  auto output_budget =
      remaining(ctx, (ai.size() + bi.size() + 1) * sizeof(Interval) * 8);
  auto a = normalise(ai, ctx), b = normalise(bi, ctx);
  if (!a.empty() && !b.empty())
    same(a[0].first.family, b[0].first.family);
  std::vector<Interval> both;
  memory(ctx, a.size() + b.size(), sizeof(Interval) * 4);
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    ctx.check();
    auto lo = max(a[i].first.value, b[j].first.value),
         hi = min(a[i].end, b[j].end);
    if (lo < hi)
      both.push_back({{a[i].first.family, lo}, hi});
    if (a[i].end < b[j].end)
      ++i;
    else
      ++j;
  }
  CompareResult out;
  out.intersection = result(std::move(both), output_budget);
  output_budget = remaining(output_budget, retained(out.intersection));
  out.a_only = result(difference(a, b, ctx), output_budget);
  output_budget = remaining(output_budget, retained(out.a_only));
  out.b_only = result(difference(b, a, ctx), output_budget);
  out.overlap = out.intersection.count != Integer();
  out.a_contains_b = out.b_only.count == Integer();
  out.b_contains_a = out.a_only.count == Integer();
  out.equal = out.a_contains_b && out.b_contains_a;
  combined_memory(ctx, {&out.intersection, &out.a_only, &out.b_only});
  return out;
}
VlsmResult allocate(const Prefix &parent, const std::vector<Request> &requests,
                    const std::vector<Reservation> &reservations,
                    bool reallocate, const Context &ctx) {
  validate(parent);
  if (requests.size() > 10000 || reservations.size() > 10000)
    throw Error("Operation exceeds the 10,000 input-row limit.");
  rows_limit(ctx, requests.size() + reservations.size());
  std::size_t text_bytes = 0;
  auto account_text = [&](const std::string &text) {
    ctx.check();
    if (text.size() > input_limit - text_bytes)
      throw Error("VLSM text exceeds the 16 MiB input limit.");
    text_bytes += text.size();
  };
  for (auto &r : requests) {
    account_text(r.id);
    account_text(r.name);
  }
  for (auto &r : reservations) {
    account_text(r.id);
    account_text(r.name);
  }
  auto output_budget = remaining(
      ctx, (requests.size() + reservations.size() + 1) * sizeof(Request) * 8 +
               text_bytes * 4);
  auto bounds = interval(parent);
  auto f = parent.network.family;
  VlsmResult out;
  std::vector<Interval> reserved, occupied;
  std::set<std::string> ids;
  std::set<std::uint64_t> sequences;
  auto in_parent = [&](const Prefix &p) {
    validate(p);
    same(f, p.network.family);
    auto r = interval(p);
    if (r.first.value < bounds.first.value || r.end > bounds.end)
      throw Error("Network is outside the parent.");
    return r;
  };
  Integer raw_reserved;
  for (auto &r : reservations) {
    ctx.check();
    try {
      if (r.id.empty() || !ids.insert(r.id).second)
        throw Error("Stable row IDs must be nonempty and unique.");
      auto block = in_parent(r.prefix);
      reserved.push_back(block);
      raw_reserved = raw_reserved + size(r.prefix);
    } catch (const Error &e) {
      throw Error("Reservation " + r.id + ": " + e.what());
    }
  }
  reserved = normalise(reserved, ctx);
  out.reserved = result(reserved, output_budget);
  output_budget = remaining(output_budget, retained(out.reserved));
  out.reservations_overlap = raw_reserved > out.reserved.count;
  struct Pending {
    size_t row;
    unsigned prefix;
  };
  std::vector<Pending> pending;
  // Validate every existing assignment, including movable ones, before
  // constructing the proposal.
  for (size_t i = 0; i < requests.size(); ++i) {
    ctx.check();
    auto &r = requests[i];
    try {
      if (r.id.empty() || !ids.insert(r.id).second)
        throw Error("Stable row IDs must be nonempty and unique.");
      if (!sequences.insert(r.sequence).second)
        throw Error("Insertion sequences must be unique and persistent.");
      auto q = required(r, parent);
      if (r.pinned && !r.assigned)
        throw Error("A pinned request requires an assigned network.");
      if (r.assigned) {
        auto block = in_parent(*r.assigned);
        if (r.assigned->length > q ||
            (r.kind != RequestKind::ipv4_lan && r.assigned->length != q))
          throw Error(
              "Assigned block does not satisfy the request type and prefix.");
        for (auto &v : reserved) {
          ctx.check();
          if (block.first.value < v.end && v.first.value < block.end)
            throw Error("Assigned network overlaps a reservation.");
        }
        for (auto &v : occupied) {
          ctx.check();
          if (block.first.value < v.end && v.first.value < block.end)
            throw Error("Assigned network overlaps another assignment.");
        }
        occupied.push_back(block);
      }
      if (r.assigned && (!reallocate || r.pinned))
        out.allocations.push_back({r.id, *r.assigned, true, r.pinned});
      else
        pending.push_back({i, q});
    } catch (const Error &e) {
      throw Error("Request " + r.id + ": " + e.what());
    }
  }
  occupied = reserved;
  for (auto &a : out.allocations)
    occupied.push_back(interval(a.prefix));
  auto unavailable = normalise(occupied, ctx, false);
  auto free = difference({bounds}, unavailable, ctx);
  size_t comparisons = 0;
  std::sort(pending.begin(), pending.end(),
            [&](const Pending &a, const Pending &b) {
              if ((++comparisons % 1024) == 0)
                ctx.check();
              return a.prefix < b.prefix ||
                     (a.prefix == b.prefix &&
                      requests[a.row].sequence < requests[b.row].sequence);
            });
  ctx.check();
  for (auto pending_row : pending) {
    ctx.check();
    auto &r = requests[pending_row.row];
    auto bits = width(f) - pending_row.prefix;
    auto block_size = Integer::power2(bits);
    std::optional<Prefix> chosen;
    Integer free_count;
    for (auto &slot : free) {
      ctx.check();
      free_count = free_count + (slot.end - slot.first.value);
      auto start = low_clear(slot.first.value, bits);
      if (start < slot.first.value)
        start = start + block_size;
      if (!chosen && start <= slot.end && block_size <= slot.end - start)
        chosen = Prefix{{f, start}, pending_row.prefix};
    }
    if (chosen) {
      out.allocations.push_back({r.id, *chosen, false, r.pinned});
      free = difference(free, {interval(*chosen)}, ctx);
    } else
      out.unallocated.push_back(
          {r.id, free_count < block_size
                     ? "Insufficient total free address space."
                     : "Insufficient contiguous aligned address space "
                       "(fragmentation)."});
  }
  out.free = result(std::move(free), output_budget);
  for (auto &p : out.free.cidrs)
    if (!out.largest_free || p.length < out.largest_free->length)
      out.largest_free = p;
  out.partial = !out.unallocated.empty();
  combined_memory(ctx, {&out.free, &out.reserved});
  return out;
}
} // namespace veu::subnet
