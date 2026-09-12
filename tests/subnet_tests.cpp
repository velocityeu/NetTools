#include "veu/subnet.hpp"
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
using namespace veu::subnet;
int checks = 0;
void check(bool ok, const char *name) {
  ++checks;
  if (!ok)
    throw std::runtime_error(name);
}
template <class F> void rejects(F fn, const char *name) {
  bool failed = false;
  try {
    fn();
  } catch (const Error &) {
    failed = true;
  }
  check(failed, name);
}
void eq(const std::string &a, const std::string &b) {
  ++checks;
  if (a != b)
    throw std::runtime_error("expected " + b + ", got " + a);
}
void regression_limits() {
  rejects([] { calculate(std::string(16u * 1024u * 1024u, ' ') + "::/0"); },
          "oversized calculator input rejected before retaining");
  rejects([] { parse_cidr("192.0.2.1 /24"); }, "embedded CIDR whitespace");
  Context c;
  c.max_rows = 3;
  rejects([&] { compare(parse_set("::/0\n::/0"), parse_set("::/0\n::/0"), c); },
          "duplicate rows still count toward combined input cap");
}
void parsing() {
  auto c = calculate("192.168.10.42", "/26");
  eq(format(c.prefix), "192.168.10.0/26");
  eq(format(c.last), "192.168.10.63");
  eq(c.total.str(), "64");
  eq(c.offset.str(), "42");
  eq(c.capacity->str(), "62");
  eq(format(*c.next), "192.168.10.106");
  eq(format(*c.first_host), "192.168.10.1");
  eq(format(*c.last_host), "192.168.10.62");
  c = calculate("172.16.37.19", "255.255.240.0");
  eq(format(c.prefix), "172.16.32.0/20");
  eq(format(c.last), "172.16.47.255");
  eq(format(*c.wildcard), "0.0.15.255");
  check(parse_prefix("0.0.0.0", Family::ipv4) == 0, "zero mask");
  check(parse_prefix("255.255.255.255", Family::ipv4) == 32, "full mask");
  for (auto bad : {"255.0.255.0", "255.255.255.1", "0.0.0.255"})
    rejects([&] { parse_prefix(bad, Family::ipv4); }, "bad mask");
  for (auto bad : {"192.168.001.1", "127.1", "0xc0000201", "256.0.0.1",
                   "+1.2.3.4", "1.2.3.4x", "1.2.3. 4", "fe80::1%12",
                   "[2001:db8::1]:443", "192.0.2.1:80", "2001::db8::1",
                   "1:2:3:4:5:6:7:8::", "::ffff:192.0.002.1", ":1:2:3:4:5:6:7",
                   "1:2:3:4:5:6:7:"})
    rejects([&] { parse_address(bad); }, "strict address");
  rejects([] { parse_address(std::string("1.2.3.4\0", 8)); }, "embedded NUL");
  eq(format(calculate("192.0.2.1", "/024").prefix), "192.0.2.0/24");
  rejects([] { calculate("192.0.2.1"); }, "incomplete prefix");
  for (auto bad :
       {"/33", "/-1", "/1.5", "/", "/9999999999999999999999999999999999999999"})
    rejects([&] { parse_prefix(bad, Family::ipv4); }, "bad prefix");
  eq(format(parse_address("  2001:0DB8:0:0:1:0:0:1\t")), "2001:db8::1:0:0:1");
  eq(format(parse_address("2001:db8::1:1:1:1:1")), "2001:db8:0:1:1:1:1:1");
  eq(format(parse_address("0:0:1:0:0:2:3:4")), "::1:0:0:2:3:4");
  eq(format(parse_address("::ffff:c000:201")), "::ffff:192.0.2.1");
  eq(format(parse_address("2001:db8::192.0.2.1")), "2001:db8::c000:201");
  rejects([] { parse_cidr("192.0.2.1/24", true); }, "misaligned pin");
  rejects([] { parse_cidr("192.0.2.1/255.255.255.0"); }, "CIDR mask forbidden");
}
void boundaries() {
  auto c = calculate("255.255.255.255/0");
  eq(c.total.str(), "4294967296");
  eq(c.offset.str(), "4294967295");
  check(!c.previous && !c.next, "v4 /0 navigation");
  c = calculate("192.0.2.11/31");
  eq(format(c.prefix), "192.0.2.10/31");
  eq(c.capacity->str(), "2");
  check(!c.broadcast, "p2p broadcast");
  c = calculate("255.255.255.255/32");
  check(!c.next && !c.broadcast, "v4 final host");
  eq(c.capacity->str(), "1");
  c = calculate("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/0");
  eq(c.total.str(), "340282366920938463463374607431768211456");
  eq(c.offset.str(), "340282366920938463463374607431768211455");
  check(!c.next && !c.previous && !c.capacity, "v6 /0 semantics");
  c = calculate("2001:db8:0:0:8000::1/65");
  eq(format(c.prefix), "2001:db8:0:0:8000::/65");
  eq(format(c.last), "2001:db8::ffff:ffff:ffff:ffff");
  eq(c.total.str(), "9223372036854775808");
  c = calculate("2001:db8::11/127");
  eq(format(c.prefix), "2001:db8::10/127");
  eq(c.total.str(), "2");
  check(!c.capacity && !c.broadcast, "v6 host semantics");
  c = calculate("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/128");
  check(!c.next, "v6 final host");
  eq(format(*c.previous), "ffff:ffff:ffff:ffff:ffff:ffff:ffff:fffe");
  c = calculate("::ffff:192.0.2.1/120");
  check(c.entered.family == Family::ipv6 && !c.capacity, "mapped stays v6");
  eq(format(c.prefix), "::ffff:192.0.2.0/120");
  rejects([] { Integer::decimal(std::string(40, '1')); }, "numeric length");
  rejects(
      [] {
        auto x = Integer(0) - Integer(1);
        (void)x;
      },
      "underflow");
}
void sets() {
  auto x = aggregate({parse_range("192.0.2.5", "192.0.2.14")});
  std::vector<std::string> expected = {"192.0.2.5/32", "192.0.2.6/31",
                                       "192.0.2.8/30", "192.0.2.12/31",
                                       "192.0.2.14/32"};
  check(x.exact.cidrs.size() == 5, "range rows");
  for (size_t i = 0; i < 5; ++i)
    eq(format(x.exact.cidrs[i]), expected[i]);
  eq(x.exact.count.str(), "10");
  eq(format(*x.covering), "192.0.2.0/28");
  eq(x.extra.count.str(), "6");
  x = aggregate(parse_set("192.0.2.0/25\n192.0.2.128/25\n192.0.2.1/32"));
  check(x.exact.cidrs.size() == 1, "siblings aggregate");
  eq(format(x.exact.cidrs[0]), "192.0.2.0/24");
  eq(x.extra.count.str(), "0");
  x = aggregate(parse_set("192.0.2.64/26\n192.0.2.128/26"));
  check(x.exact.cidrs.size() == 2, "adjacent nonsiblings");
  eq(x.extra.count.str(), "128");
  auto c = compare({parse_range("192.0.2.0", "192.0.2.10")},
                   {parse_range("192.0.2.5", "192.0.2.15")});
  eq(c.intersection.count.str(), "6");
  eq(c.a_only.count.str(), "5");
  eq(c.b_only.count.str(), "5");
  check(c.overlap && !c.equal && !c.a_contains_b, "partial compare");
  c = compare({}, {});
  check(c.equal && c.a_contains_b && c.b_contains_a && !c.overlap,
        "empty semantics");
  c = compare(parse_set("::/0"),
              parse_set("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/128"));
  eq(c.a_only.count.str(), "340282366920938463463374607431768211455");
  eq(c.intersection.count.str(), "1");
  rejects(
      [] {
        compare(parse_set("192.0.2.1/32"), parse_set("::ffff:192.0.2.1/128"));
      },
      "mixed compare");
  rejects([] { parse_range("192.0.2.10", "192.0.2.1"); }, "reversed range");
}
void splits() {
  auto p = parse_cidr("::/0");
  eq(split_count(p, 128).str(), "340282366920938463463374607431768211456");
  eq(format(split_at(
         p, 128, Integer::decimal("340282366920938463463374607431768211455"))),
     "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/128");
  auto rows = split_page(
      p, 128, Integer::decimal("340282366920938463463374607431768211455"), 200);
  check(rows.size() == 1, "last split page");
  check(split_prefix(p, Integer::power2(128)) == 128, "split full parts");
  rejects([&] { split_prefix(p, Integer(3)); }, "three parts");
  rejects([&] { split_at(p, 128, Integer::power2(128)); }, "split end index");
  rejects([&] { split_page(p, 128, 0, 201); }, "page limit");
}
Request lan(std::string id, uint64_t seq, uint64_t hosts) {
  Request r;
  r.id = id;
  r.name = id;
  r.sequence = seq;
  r.hosts = hosts;
  return r;
}
void vlsm() {
  {
    auto host = lan("host", 1, 1);
    host.kind = RequestKind::ipv4_host_route;
    host.assigned = parse_cidr("192.0.2.0/30");
    rejects([&] { allocate(parse_cidr("192.0.2.0/24"), {host}); },
            "host-route assignment must remain /32");
    auto point = host;
    point.kind = RequestKind::ipv4_point_to_point;
    rejects([&] { allocate(parse_cidr("192.0.2.0/24"), {point}); },
            "point-to-point assignment must remain /31");
  }

  {
    auto named = lan("named", 100, 1);
    named.name = std::string(200000, 'x');
    Context budget;
    budget.max_work_bytes = 100000;
    rejects(
        [&] {
          allocate(parse_cidr("192.0.2.0/24"), {named}, {}, false, budget);
        },
        "VLSM text respects working-memory budget");
  }

  auto p = parse_cidr("192.0.2.0/24");
  auto a = lan("a", 10, 50), b = lan("b", 20, 50), d = lan("d", 30, 2);
  auto r = allocate(p, {b, d, a});
  check(!r.partial, "vlsm complete");
  eq(format(r.allocations[0].prefix), "192.0.2.0/26");
  check(r.allocations[0].id == "a", "stable insertion");
  eq(format(r.allocations[1].prefix), "192.0.2.64/26");
  eq(format(r.allocations[2].prefix), "192.0.2.128/30");
  eq(r.free.count.str(), "124");
  a.assigned = parse_cidr("192.0.2.128/26");
  a.pinned = true;
  b.assigned = parse_cidr("192.0.2.64/26");
  r = allocate(p, {a, b, d});
  check(r.allocations[0].preserved && r.allocations[1].preserved,
        "preserve existing");
  eq(format(r.allocations[0].prefix), "192.0.2.128/26");
  eq(format(r.allocations[2].prefix), "192.0.2.0/30");
  Reservation res{"r", "reserved", parse_cidr("192.0.2.0/26")};
  rejects(
      [&] { allocate(p, {d}, {{"bad", "bad", parse_cidr("192.0.3.0/24")}}); },
      "outside reservation");
  d.assigned = parse_cidr("192.0.2.0/30");
  rejects([&] { allocate(p, {d}, {res}); }, "reservation collision");
  d.assigned.reset();
  auto big = lan("big", 1, 100), small = lan("small", 2, 2);
  r = allocate(p, {big, small},
               {{"r1", "first", parse_cidr("192.0.2.64/26")},
                {"r2", "second", parse_cidr("192.0.2.192/26")}});
  check(r.partial && r.unallocated.size() == 1 && r.allocations.size() == 1,
        "continue smaller");
  check(r.unallocated[0].reason.find("contiguous") != std::string::npos,
        "fragmentation reason");
  eq(format(r.allocations[0].prefix), "192.0.2.0/30");
  r = allocate(p, {}, {res, {"r2", "overlap", parse_cidr("192.0.2.0/27")}});
  check(r.reservations_overlap, "reservation union");
  eq(r.reserved.count.str(), "64");
  rejects(
      [&] {
        auto invalid = lan("x", 4, 4294967295ULL);
        allocate(parse_cidr("0.0.0.0/0"), {invalid});
      },
      "LAN host bound");
  Request v;
  v.id = "v";
  v.kind = RequestKind::ipv6_prefix;
  v.child_prefix = 64;
  r = allocate(parse_cidr("::/0"), {v});
  eq(format(r.allocations[0].prefix), "::/64");
}
void limits_and_properties() {
  Context ctx;
  ctx.cancelled = [] { return true; };
  rejects([&] { aggregate(parse_set("::/0"), ctx); }, "cancellation");
  ctx.cancelled = {};
  ctx.max_rows = 1;
  rejects([&] { compare(parse_set("::/1"), parse_set("8000::/1"), ctx); },
          "combined row limit");
  ctx.max_rows = 10000;
  ctx.max_work_bytes = 1;
  rejects([&] { aggregate(parse_set("::/0"), ctx); }, "memory budget");
  std::mt19937 rng(1979);
  for (int iteration = 0; iteration < 100; ++iteration) {
    std::vector<Interval> a, b;
    bool aa[256]{}, bb[256]{};
    for (int j = 0; j < 10; ++j) {
      unsigned lo = rng() % 256, hi = rng() % 256;
      if (lo > hi)
        std::swap(lo, hi);
      auto &out = j < 5 ? a : b;
      out.push_back(parse_range("192.0.2." + std::to_string(lo),
                                "192.0.2." + std::to_string(hi)));
      for (unsigned k = lo; k <= hi; ++k)
        (j < 5 ? aa : bb)[k] = true;
    }
    auto c = compare(a, b);
    unsigned both = 0, left = 0, right = 0;
    for (int k = 0; k < 256; ++k) {
      both += aa[k] && bb[k];
      left += aa[k] && !bb[k];
      right += bb[k] && !aa[k];
    }
    eq(c.intersection.count.str(), std::to_string(both));
    eq(c.a_only.count.str(), std::to_string(left));
    eq(c.b_only.count.str(), std::to_string(right));
  }
}
void classification_tests() {
  auto c = classify(parse_cidr("192.0.0.9/32"));
  check(!c.mixed && c.segments.size() == 1,
        "more-specific host classification");
  eq(std::string(c.segments[0].name), "Port Control Protocol Anycast");
  check(c.segments[0].special != nullptr, "special-purpose metadata available");
  eq(std::string(c.segments[0].special->globally_reachable), "True");
  c = classify(parse_cidr("192.0.0.0/24"));
  check(c.mixed && c.segments.size() > 1,
        "containing protocol block has mixed classification");
  bool found_pcp = false, found_general = false;
  Integer total;
  for (auto &s : c.segments) {
    total = total + (s.range.end - s.range.first.value);
    found_pcp |= s.name == "Port Control Protocol Anycast";
    found_general |= s.name == "IETF Protocol Assignments";
  }
  check(found_pcp && found_general,
        "constituents include exception and parent");
  eq(total.str(), "256");
  c = classify(parse_cidr("255.255.255.255/32"));
  eq(std::string(c.segments[0].name), "Limited Broadcast");
  c = classify(parse_cidr("224.0.0.1/32"));
  eq(std::string(c.segments[0].address_type), "Multicast");
  c = classify(parse_cidr("ff02::1/128"));
  eq(std::string(c.segments[0].address_type), "Multicast");
  c = classify(parse_cidr("2001::/32"));
  check(c.segments[0].special != nullptr, "Teredo metadata");
  check(c.segments[0].special->globally_reachable.starts_with("N/A"),
        "N/A preserved");
  check(c.segments[0].special->notes.find("rfc4380") != std::string_view::npos,
        "registry footnote expanded");
  c = classify(parse_cidr("2001:4860::1/128"));
  check(c.segments[0].special == nullptr,
        "ordinary space is not a special-purpose entry");
  check(c.summary.find("unknown") != std::string::npos,
        "missing special entry does not imply reachability");
  c = classify(parse_cidr("3fff::/20"));
  eq(std::string(c.segments[0].name), "Documentation");
  c = classify(parse_cidr("::ffff:192.0.2.1/128"));
  eq(std::string(c.segments[0].name), "IPv4-mapped Address");
  eq(std::string(c.segments[0].address_type), "IPv4-mapped IPv6");
  c = classify(parse_cidr("::/0"));
  check(c.mixed, "IPv6 /0 mixed classification");
  total = 0;
  Integer cursor;
  for (auto &s : c.segments) {
    check(s.range.first.value == cursor,
          "classification segments have no gaps");
    total = total + s.range.end - s.range.first.value;
    cursor = s.range.end;
  }
  eq(total.str(), "340282366920938463463374607431768211456");
  check(classification_sources().size() == 4, "all four registries available");
  check(classification_entries().size() == 327,
        "all snapshot prefixes retained");
  Context stop;
  stop.cancelled = [] { return true; };
  rejects([&] { classify(parse_cidr("::/0"), stop); },
          "classification cancellation");
}
void extensive_properties() {
  std::vector<Interval> many(10000, interval(parse_cidr("::/128")));
  eq(exact_union(many).count.str(), "1");
  many.push_back(many.front());
  rejects([&] { exact_union(many); }, "row cap above 10000");
  std::string payload(16u * 1024u * 1024u, ' ');
  check(parse_set(payload).empty(), "exact bulk byte limit accepted");
  payload.push_back(' ');
  rejects([&] { parse_set(payload); }, "bulk byte limit above bound rejected");
  auto page = split_page(parse_cidr("::/0"), 128, Integer::power2(64), 200);
  check(page.size() == 200, "full page at 2^64");
  eq(format(page.front()), "0:0:0:1::/128");
  eq(format(page.back()), "::1:0:0:0:c7/128");
  auto universe = compare(parse_set("::/0"), parse_set("::/0"));
  check(universe.equal, "full-space equality avoids intermediate overflow");
  eq(universe.intersection.count.str(),
     "340282366920938463463374607431768211456");
  auto absent_zero = compare(parse_set("::/0"), parse_set("::/128"));
  check(absent_zero.a_only.cidrs.size() == 128,
        "full-space minus zero prefix count");
  eq(format(absent_zero.a_only.cidrs.front()), "::1/128");
  eq(format(absent_zero.a_only.cidrs.back()), "8000::/1");
  Context cancellable;
  unsigned calls = 0;
  cancellable.cancelled = [&] { return ++calls > 30; };
  rejects(
      [&] {
        exact_union(
            {parse_range("::1", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:fffe")},
            cancellable);
      },
      "decomposition cancellation");
  std::mt19937 random(911);
  for (unsigned iteration = 0; iteration < 100; ++iteration) {
    std::vector<Request> requests;
    for (unsigned row = 0; row < 8; ++row)
      requests.push_back(
          lan("r" + std::to_string(row), row, 1 + random() % 60));
    auto proposed =
        allocate(parse_cidr("192.0.2.0/24"), requests,
                 {{"reserved", "reserved", parse_cidr("192.0.2.64/27")}});
    unsigned membership[256]{};
    auto mark = [&](const Prefix &prefix) {
      unsigned first = prefix.network.value.words[0] & 255;
      unsigned count = 1u << (32 - prefix.length);
      check(first + count <= 256, "allocation stays within parent");
      check(first % count == 0, "allocation is aligned");
      for (unsigned n = first; n < first + count; ++n)
        ++membership[n];
    };
    for (auto &a : proposed.allocations)
      mark(a.prefix);
    for (auto &p : proposed.reserved.cidrs)
      mark(p);
    for (auto &p : proposed.free.cidrs)
      mark(p);
    for (auto n : membership)
      check(n == 1, "free/reserved/allocated exactly partition parent");
    for (auto &r : requests)
      r.name = "renamed";
    std::reverse(requests.begin(), requests.end());
    auto again =
        allocate(parse_cidr("192.0.2.0/24"), requests,
                 {{"reserved", "renamed", parse_cidr("192.0.2.64/27")}});
    check(again.allocations.size() == proposed.allocations.size(),
          "rename and view order preserve allocation count");
    for (size_t n = 0; n < again.allocations.size(); ++n) {
      check(again.allocations[n].id == proposed.allocations[n].id &&
                again.allocations[n].prefix == proposed.allocations[n].prefix,
            "stable placement survives rename and reorder");
    }
  }
}
void reference(const char *path) {
  std::ifstream file(path);
  check(bool(file), "reference fixture opens");
  std::string line;
  while (std::getline(file, line)) {
    std::vector<std::string> v;
    std::istringstream in(line);
    std::string cell;
    while (std::getline(in, cell, '\t'))
      v.push_back(cell);
    if (v[0] == "C") {
      auto c = calculate(v[1]);
      eq(format(c.prefix), v[2]);
      eq(format(c.last), v[3]);
      eq(c.total.str(), v[4]);
      eq(c.offset.str(), v[5]);
      eq(c.previous ? format(*c.previous) : "none", v[6]);
      eq(c.next ? format(*c.next) : "none", v[7]);
    } else if (v[0] == "R") {
      auto r = exact_union({parse_range(v[1], v[2])});
      std::string actual;
      for (auto &p : r.cidrs) {
        if (!actual.empty())
          actual += "|";
        actual += format(p);
      }
      eq(actual, v[3]);
      eq(r.count.str(), v[4]);
    } else if (v[0] == "K") {
      auto c = classify(parse_cidr(v[1]));
      check(c.segments.size() == 1,
            "oracle host classification is one segment");
      eq(std::string(c.segments[0].name), v[2]);
      auto special = c.segments[0].special;
      eq(special && !special->globally_reachable.empty()
             ? std::string(special->globally_reachable)
             : "-",
         v[3]);
      check(c.segments[0].address_space != nullptr,
            "oracle base registry covers address");
      eq(std::string(c.segments[0].address_space->name), v[4]);
    } else if (v[0] == "S") {
      eq(format(split_at(parse_cidr(v[1]), unsigned(std::stoul(v[2])),
                         Integer::decimal(v[3]))),
         v[4]);
    } else
      throw std::runtime_error("unknown reference kind");
  }
}
int main(int argc, char **argv) {
  try {
    if (argc > 1)
      reference(argv[1]);
    regression_limits();
    parsing();
    boundaries();
    sets();
    splits();
    vlsm();
    limits_and_properties();
    extensive_properties();
    classification_tests();
    std::cout << "PASS: " << checks << " subnet checks\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
    return 1;
  }
}
