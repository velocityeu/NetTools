#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace veu::net
{
enum class Tool
{
    adapters,
    routes,
    neighbours,
    external_ip,
    ping,
    traceroute,
    dns,
    tcp,
    http,
    wake_on_lan,
    mtu
};
enum class Family
{
    any,
    ipv4,
    ipv6
};
enum class Status
{
    complete,
    failed,
    cancelled,
    unavailable
};
enum class Outcome
{
    reply,
    timeout,
    network_error,
    send_error,
    operation_error,
    pending,
    cancelled,
    not_sent
};
struct Request
{
    Tool tool = Tool::adapters;
    Family family = Family::any;
    std::wstring target, source, resolver, dns_type = L"A", method = L"HEAD", mac;
    std::uint32_t timeout_ms = 1000, interval_ms = 1000, count = 20, payload_bytes = 32;
    std::uint32_t max_hops = 30, probes_per_hop = 3, rounds = 1, deadline_ms = 15000;
    std::uint32_t interface_index = 0, mtu_ceiling = 1500, wol_burst = 1;
    std::uint16_t port = 0; // TCP requires an explicit nonzero port; WoL defaults to 9.
    bool bypass_cache = false, direct = false, follow_redirects = false;
    bool continuous = false;
};
struct Probe
{
    std::uint64_t sequence = 0, segment = 0;
    std::uint32_t round = 0, hop = 0, payload_bytes = 0, native_status = 0;
    double start_ms = 0;
    Outcome outcome = Outcome::not_sent;
    std::optional<double> rtt_ms;
    std::wstring responder;
};
struct Statistics
{
    std::uint64_t attempted = 0, sent = 0, completed = 0, received = 0, lost = 0;
    std::uint64_t timeouts = 0, network_errors = 0, local_errors = 0, pending = 0, cancelled = 0, not_sent = 0,
                  variation_pairs = 0;
    std::optional<double> loss_percent, minimum, mean, maximum, population_sd, p95, variation;
};
// Retains a bounded raw cohort while separately accumulating lifetime counters and moments.
class ProbeHistory
{
  public:
    explicit ProbeHistory(std::size_t capacity = 10000);
    void append(Probe probe);
    const std::vector<Probe> &retained() const
    {
        return retained_;
    }
    std::uint64_t evicted() const
    {
        return evicted_;
    }
    Statistics session_statistics() const;

  private:
    std::size_t capacity_;
    std::vector<Probe> retained_;
    std::uint64_t evicted_ = 0, timed_ = 0;
    Statistics lifetime_;
    double m2_ = 0, changes_ = 0;
    std::optional<Probe> previous_;
};
std::vector<Probe> probe_cohort(const std::vector<Probe> &probes, double now_ms, std::uint32_t window_ms);
struct Result
{
    Status status = Status::complete;
    std::wstring summary, observed_at, context;
    std::uint32_t native_status = 0;
    bool truncated = false;
    std::size_t retained_text_bytes = 0;
    std::vector<std::wstring> columns;
    std::vector<std::vector<std::wstring>> rows;
    std::vector<Probe> probes;
    Statistics statistics, session_statistics;
    double elapsed_ms = 0, sampled_clock_ms = 0;
    std::uint64_t evicted_probes = 0;
};
using Progress = std::function<void(const Result &)>; // worker callback; no HWND or synchronous UI calls
std::optional<std::wstring> validate(const Request &request);
Statistics statistics(const std::vector<Probe> &probes);
std::optional<std::array<std::uint8_t, 102>> magic_packet(std::wstring_view mac);
std::optional<std::wstring> parse_external_ip(std::string_view body, Family family);
std::wstring reverse_name(std::wstring_view literal);
Outcome classify_icmp_status(std::uint32_t status, bool submitted, bool traceroute = false);
Result run(const Request &request, std::atomic_bool &cancel, Progress progress = {},
           const std::atomic_bool *paused = nullptr);
} // namespace veu::net
