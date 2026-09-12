#include "veu/subnet.hpp"
#include <algorithm>
#include <iterator>

namespace veu::subnet {
namespace {
struct SnapshotRow {
  std::string_view prefix, name, allocation_status, allocation_date,
      termination_date, source_valid, destination_valid, forwardable,
      globally_reachable, reserved_by_protocol, references, notes;
  std::size_t source_index;
  bool special_purpose;
};
#include "registry_snapshot.inc"

bool covers(const RegistryEntry &entry, const Integer &value) {
  const auto bounds = interval(entry.prefix);
  return value >= bounds.first.value && value < bounds.end;
}
std::string_view address_type(const Address &address) {
  if (address.family == Family::ipv4) {
    const auto value = address.value.words[0];
    if (value == 0)
      return "Unspecified";
    if (value == 0xffffffffu)
      return "Limited broadcast";
    if ((value >> 28) == 14)
      return "Multicast";
    if ((value >> 24) == 127)
      return "Loopback";
    return "Unicast or reserved";
  }
  if (address.value == Integer())
    return "Unspecified";
  if (address.value == Integer(1))
    return "Loopback";
  if ((address.value.words[3] >> 24) == 255)
    return "Multicast";
  if (address.value.words[3] == 0 && address.value.words[2] == 0 &&
      address.value.words[1] == 65535)
    return "IPv4-mapped IPv6";
  return "Unicast or reserved";
}
} // namespace

std::span<const RegistrySource> classification_sources() { return kSources; }
std::span<const RegistryEntry> classification_entries() {
  // Immutable static data; initialization is thread safe and performs no I/O.
  static const std::vector<RegistryEntry> entries = [] {
    std::vector<RegistryEntry> out;
    out.reserve(std::size(kRows));
    for (const auto &r : kRows) {
      out.push_back({parse_cidr(r.prefix, true), r.name, r.allocation_status,
                     r.allocation_date, r.termination_date, r.source_valid,
                     r.destination_valid, r.forwardable, r.globally_reachable,
                     r.reserved_by_protocol, r.references, r.notes,
                     r.source_index, r.special_purpose});
    }
    return out;
  }();
  return entries;
}
ClassificationResult classify(const Prefix &prefix, const Context &ctx) {
  ctx.check();
  return classify(interval(prefix), ctx);
}
ClassificationResult classify(const Interval &input, const Context &ctx) {
  ctx.check();
  if (ctx.max_rows < 1)
    throw Error("Classification requires one input row.");
  auto limit = Integer::power2(width(input.first.family));
  if (input.first.value >= limit || input.end > limit ||
      input.first.value >= input.end)
    throw Error("Invalid or reversed classification interval.");
  // At most two boundaries per registry entry plus the requested endpoints.
  // Account before any dynamic allocation, including immutable initialization.
  constexpr auto max_boundaries = std::size(kRows) * 2 + 2;
  constexpr auto budget =
      std::size(kRows) * sizeof(RegistryEntry) +
      max_boundaries * (sizeof(Integer) + sizeof(ClassificationSegment)) +
      std::size(kRows) * sizeof(const RegistryEntry *) + 4096;
  if (budget > ctx.max_work_bytes)
    throw Error("Classification exceeds the working-memory budget.");
  auto entries = classification_entries();
  std::vector<Integer> boundaries;
  boundaries.reserve(max_boundaries);
  boundaries.push_back(input.first.value);
  boundaries.push_back(input.end);
  std::vector<const RegistryEntry *> candidates;
  candidates.reserve(entries.size());
  for (const auto &entry : entries) {
    ctx.check();
    if (entry.prefix.network.family != input.first.family)
      continue;
    auto block = interval(entry.prefix);
    if (block.end <= input.first.value || block.first.value >= input.end)
      continue;
    candidates.push_back(&entry);
    if (block.first.value > input.first.value)
      boundaries.push_back(block.first.value);
    if (block.end < input.end)
      boundaries.push_back(block.end);
  }
  std::size_t comparisons = 0;
  std::sort(boundaries.begin(), boundaries.end(),
            [&](const Integer &a, const Integer &b) {
              if ((++comparisons % 1024) == 0)
                ctx.check();
              return a < b;
            });
  ctx.check();
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                   boundaries.end());
  ClassificationResult out;
  out.segments.reserve(boundaries.size() - 1);
  for (std::size_t i = 0; i + 1 < boundaries.size(); ++i) {
    ctx.check();
    ClassificationSegment segment;
    segment.range = {{input.first.family, boundaries[i]}, boundaries[i + 1]};
    segment.address_type = address_type(segment.range.first);
    for (const auto *entry : candidates) {
      ctx.check();
      if (!covers(*entry, boundaries[i]))
        continue;
      auto *&selected =
          entry->special_purpose ? segment.special : segment.address_space;
      if (!selected || entry->prefix.length > selected->prefix.length)
        selected = entry;
    }
    segment.name = segment.special         ? segment.special->name
                   : segment.address_space ? segment.address_space->name
                                           : "Unknown registry classification";
    if (!out.segments.empty()) {
      auto &last = out.segments.back();
      if (last.special == segment.special &&
          last.address_space == segment.address_space &&
          last.address_type == segment.address_type) {
        last.range.end = segment.range.end;
        continue;
      }
    }
    out.segments.push_back(segment);
  }
  out.mixed = out.segments.size() > 1;
  if (out.mixed)
    out.summary = "Mixed classifications (" +
                  std::to_string(out.segments.size()) +
                  " ranges); inspect the constituent ranges.";
  else {
    const auto &segment = out.segments.front();
    out.summary =
        std::string(segment.name) + "; " + std::string(segment.address_type);
    if (segment.special && !segment.special->globally_reachable.empty())
      out.summary += "; IANA globally reachable: " +
                     std::string(segment.special->globally_reachable);
    else
      out.summary +=
          "; global reachability unknown (no applicable registry flag)";
  }
  out.summary += ". Embedded IANA snapshot retrieved " +
                 std::string(kSources[0].retrieved) +
                 "; registry flags do not guarantee operational routability.";
  ctx.check();
  return out;
}
} // namespace veu::subnet
