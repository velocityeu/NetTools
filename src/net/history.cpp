#include "veu/network.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace veu::net
{
ProbeHistory::ProbeHistory(std::size_t capacity) : capacity_(capacity)
{
    if (capacity < 1 || capacity > 10000)
        throw std::invalid_argument("Probe history capacity must be 1–10000");
    retained_.reserve(capacity);
}
void ProbeHistory::append(Probe probe)
{
    auto one = statistics({probe});
    auto add = [](std::uint64_t &sum, std::uint64_t value) {
        if (value > std::numeric_limits<std::uint64_t>::max() - sum)
            throw std::overflow_error("Probe counter overflow");
        sum += value;
    };
    add(lifetime_.attempted, one.attempted);
    add(lifetime_.sent, one.sent);
    add(lifetime_.completed, one.completed);
    add(lifetime_.received, one.received);
    add(lifetime_.lost, one.lost);
    add(lifetime_.timeouts, one.timeouts);
    add(lifetime_.network_errors, one.network_errors);
    add(lifetime_.local_errors, one.local_errors);
    add(lifetime_.pending, one.pending);
    add(lifetime_.cancelled, one.cancelled);
    add(lifetime_.not_sent, one.not_sent);
    if (one.mean)
    {
        ++timed_;
        double before = lifetime_.mean.value_or(0);
        lifetime_.mean = before + (*one.mean - before) / static_cast<double>(timed_);
        m2_ += (*one.mean - before) * (*one.mean - *lifetime_.mean);
        lifetime_.minimum = std::min(lifetime_.minimum.value_or(*one.mean), *one.mean);
        lifetime_.maximum = std::max(lifetime_.maximum.value_or(*one.mean), *one.mean);
        lifetime_.population_sd = std::sqrt(std::max(0.0, m2_ / static_cast<double>(timed_)));
    }
    if (previous_ && previous_->outcome == Outcome::reply && probe.outcome == Outcome::reply && previous_->rtt_ms &&
        probe.rtt_ms && std::isfinite(*previous_->rtt_ms) && std::isfinite(*probe.rtt_ms) &&
        previous_->segment == probe.segment && previous_->sequence != UINT64_MAX &&
        probe.sequence == previous_->sequence + 1)
    {
        changes_ += std::abs(*previous_->rtt_ms - *probe.rtt_ms);
        ++lifetime_.variation_pairs;
        lifetime_.variation = changes_ / static_cast<double>(lifetime_.variation_pairs);
    }
    if (lifetime_.completed)
        lifetime_.loss_percent = 100.0 * static_cast<double>(lifetime_.lost) / static_cast<double>(lifetime_.completed);
    previous_ = probe;
    if (retained_.size() == capacity_)
    {
        retained_.erase(retained_.begin());
        ++evicted_;
    }
    retained_.push_back(std::move(probe));
}
Statistics ProbeHistory::session_statistics() const
{
    if (!evicted_)
        return statistics(retained_);
    auto out = lifetime_;
    out.p95.reset();
    return out;
}
std::vector<Probe> probe_cohort(const std::vector<Probe> &probes, double now_ms, std::uint32_t window_ms)
{
    std::vector<Probe> result;
    if (!std::isfinite(now_ms))
        return result;
    for (auto &p : probes)
        if (std::isfinite(p.start_ms) && p.start_ms <= now_ms &&
            (!window_ms || p.start_ms > now_ms - static_cast<double>(window_ms)))
            result.push_back(p);
    return result;
}
} // namespace veu::net
