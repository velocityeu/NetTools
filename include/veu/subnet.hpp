#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace veu::subnet {
// 160 checked bits: holds 2^128 endpoints and the sum of 10,000 full spaces.
struct Integer {
  std::array<std::uint32_t, 5>
      words{}; // little-endian limbs, independent of machine byte order
  Integer(std::uint64_t value = 0);
  static Integer decimal(std::string_view text);
  static Integer power2(unsigned bit);
  std::string str() const;
  bool bit(unsigned index) const;
  friend bool operator==(const Integer &, const Integer &) = default;
  friend bool operator<(const Integer &, const Integer &);
  friend Integer operator+(const Integer &, const Integer &);
  friend Integer operator-(const Integer &, const Integer &);
};
bool operator>(const Integer &, const Integer &);
bool operator<=(const Integer &, const Integer &);
bool operator>=(const Integer &, const Integer &);
enum class Family { ipv4, ipv6 };
struct Address {
  Family family = Family::ipv4;
  Integer value;
  friend bool operator==(const Address &, const Address &) = default;
};
struct Prefix {
  Address network;
  unsigned length = 0;
  friend bool operator==(const Prefix &, const Prefix &) = default;
};
struct Interval {
  Address first;
  Integer end;
}; // exclusive wide endpoint
struct Context {
  std::function<bool()> cancelled;
  std::size_t max_rows = 10000;
  std::size_t max_work_bytes = 128u * 1024u * 1024u;
  void check() const;
};
class Error : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};
Address parse_address(std::string_view);
unsigned parse_prefix(std::string_view, Family);
Prefix parse_cidr(std::string_view, bool require_aligned = false);
std::string format(const Address &);
std::string format(const Prefix &);
unsigned width(Family);
Integer size(const Prefix &);
Interval interval(const Prefix &);
struct Calculation {
  std::string original_input;
  Address entered;
  Prefix prefix;
  Address last;
  Integer total, offset;
  std::optional<Address> mask, wildcard, broadcast, first_host, last_host;
  std::optional<Integer> capacity;
  std::optional<Address> previous, next; // preserve entered host offset
  std::string capacity_note;
};
// A CIDR pasted in address overrides the separate field; absence of both is
// incomplete/error.
Calculation calculate(std::string_view address,
                      std::string_view prefix_or_mask = {});
Integer split_count(const Prefix &, unsigned child_prefix);
unsigned split_prefix(const Prefix &, const Integer &parts);
Prefix split_at(const Prefix &, unsigned child_prefix,
                const Integer &zero_based_index);
std::vector<Prefix> split_page(const Prefix &, unsigned child_prefix,
                               const Integer &first_index,
                               std::size_t count = 200, const Context & = {});
Interval parse_range(std::string_view first, std::string_view last);
// Each row is a CIDR or inclusive literal range written as "first - last".
std::vector<Interval> parse_set(std::string_view rows, const Context & = {});
struct SetResult {
  std::vector<Interval> ranges;
  std::vector<Prefix> cidrs;
  Integer count;
};
SetResult exact_union(const std::vector<Interval> &, const Context & = {});
struct AggregateResult {
  SetResult exact;
  std::optional<Prefix> covering;
  SetResult extra;
};
AggregateResult aggregate(const std::vector<Interval> &, const Context & = {});
struct CompareResult {
  bool equal = false, a_contains_b = false, b_contains_a = false,
       overlap = false;
  SetResult intersection, a_only, b_only;
};
CompareResult compare(const std::vector<Interval> &a,
                      const std::vector<Interval> &b, const Context & = {});
enum class RequestKind {
  ipv4_lan,
  ipv4_point_to_point,
  ipv4_host_route,
  ipv6_prefix
};
struct Request {
  std::string id, name;
  std::uint64_t sequence = 0;
  RequestKind kind = RequestKind::ipv4_lan;
  Integer hosts = 1;
  unsigned child_prefix = 64;
  std::optional<Prefix> assigned;
  bool pinned = false;
};
struct Reservation {
  std::string id, name;
  Prefix prefix;
};
struct Allocation {
  std::string id;
  Prefix prefix;
  bool preserved = false;
  bool pinned = false;
};
struct Unallocated {
  std::string id;
  std::string reason;
};
struct VlsmResult {
  std::vector<Allocation> allocations;
  std::vector<Unallocated> unallocated;
  SetResult free, reserved;
  std::optional<Prefix> largest_free;
  bool reservations_overlap = false;
  bool partial = false; // proposal only; caller must explicitly apply it as one
                        // transaction
};
VlsmResult allocate(const Prefix &parent, const std::vector<Request> &,
                    const std::vector<Reservation> & = {},
                    bool reallocate = false, const Context & = {});
// Embedded IANA registry metadata. String views and entry pointers have process
// lifetime. Flags retain the source text (True, False, N/A, blank/unknown and
// footnote references); they must not be coerced into a public/private Boolean.
struct RegistrySource {
  std::string_view title, url, updated, retrieved, sha256, notes;
};
struct RegistryEntry {
  Prefix prefix;
  std::string_view name, allocation_status, allocation_date, termination_date;
  std::string_view source_valid, destination_valid, forwardable;
  std::string_view globally_reachable, reserved_by_protocol;
  std::string_view references, notes;
  std::size_t source_index = 0;
  bool special_purpose = false;
};
struct ClassificationSegment {
  Interval range;
  std::string_view address_type, name;
  const RegistryEntry *special = nullptr;
  const RegistryEntry *address_space = nullptr;
};
struct ClassificationResult {
  bool mixed = false;
  std::string summary;
  std::vector<ClassificationSegment> segments;
};
std::span<const RegistrySource> classification_sources();
std::span<const RegistryEntry> classification_entries();
ClassificationResult classify(const Interval &, const Context & = {});
ClassificationResult classify(const Prefix &, const Context & = {});

} // namespace veu::subnet
