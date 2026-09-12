#include "veu/network.hpp"
#include "../src/net/icmp_dispatch.hpp"
#include <chrono>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>
using namespace veu::net;
void check(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
bool approximately(double a, double b)
{
    return std::abs(a - b) < 1e-8;
}
void local_integration()
{
    // Occupy the real dispatch capacity so the probe must wait before submission.
    auto &slots = detail::icmp_slots();
    for (int i = 0; i < 4; ++i)
        slots.acquire();
    Result queued;
    std::atomic_bool entered = false;
    std::jthread worker([&] {
        std::atomic_bool stop = false;
        Request request;
        request.tool = Tool::ping;
        request.target = L"127.0.0.1";
        request.count = 2;
        request.interval_ms = 100;
        entered = true;
        entered.notify_one();
        queued = run(request, stop);
    });
    entered.wait(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    slots.release(4);
    worker.join();
    check(queued.probes.size() == 2 && queued.probes[0].outcome == Outcome::reply,
          "queued loopback probe completes after capacity is released");
    check(queued.probes[0].start_ms >= 150,
          "probe actual start excludes time waiting for an ICMP dispatch slot");
    check(queued.probes[1].start_ms - queued.probes[0].start_ms >= 95,
          "queued dispatch preserves the next requested start interval");
    // Expiry while queued is not a network timeout and must never send a probe.
    for (auto tool : {Tool::traceroute, Tool::mtu})
    {
        for (int i = 0; i < 4; ++i)
            slots.acquire();
        Result expired;
        entered = false;
        std::jthread deadline_worker([&] {
            std::atomic_bool stop = false;
            Request request;
            request.tool = tool;
            request.target = L"127.0.0.1";
            request.deadline_ms = 50;
            request.max_hops = 1;
            request.probes_per_hop = 1;
            entered = true;
            entered.notify_one();
            expired = run(request, stop);
        });
        entered.wait(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        slots.release(4);
        deadline_worker.join();
        check(expired.probes.size() == 1 && expired.probes[0].outcome == Outcome::not_sent &&
                  expired.probes[0].native_status == ERROR_TIMEOUT,
              "trace and MTU must not submit a probe when the deadline expires while queued");
        check(expired.statistics.not_sent == 1 && expired.statistics.sent == 0 && expired.statistics.lost == 0,
              "queued deadline expiry is excluded from sent and loss counters");
    }
    std::atomic_bool cancel = false;
    Request r;
    r.tool = Tool::adapters;
    auto adapters = run(r, cancel);
    check(adapters.status == Status::complete, "local adapter snapshot");
    for (auto &row : adapters.rows)
        check(row.size() == adapters.columns.size(), "snapshot row shape");
    r = Request{};
    r.tool = Tool::ping;
    r.target = L"127.0.0.1";
    r.count = 2;
    r.interval_ms = 0;
    r.timeout_ms = 500;
    auto ping = run(r, cancel);
    check(ping.status == Status::complete && ping.probes.size() == 2, "bounded loopback ping run");
    check(ping.statistics.received == 2, "IPv4 loopback replies");
    r.target = L"::1";
    r.family = Family::ipv6;
    auto ping6 = run(r, cancel);
    check(ping6.statistics.received == 2, "IPv6 loopback replies");
    WSADATA data{};
    check(WSAStartup(MAKEWORD(2, 2), &data) == 0, "fixture winsock");
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    check(listener != INVALID_SOCKET, "fixture socket");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK);
    check(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0, "fixture bind");
    check(listen(listener, 1) == 0, "fixture listen");
    int length = sizeof(address);
    check(getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length) == 0, "fixture port");
    r = Request{};
    r.tool = Tool::tcp;
    r.target = L"127.0.0.1";
    r.port = ntohs(address.sin_port);
    r.deadline_ms = 1000;
    auto tcp = run(r, cancel);
    check(tcp.status == Status::complete, "nonblocking TCP loopback handshake");
    SOCKET accepted = accept(listener, nullptr, nullptr);
    check(accepted != INVALID_SOCKET, "accept completed TCP fixture");
    closesocket(accepted);
    std::thread server([&] {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listener, &readable);
        timeval timeout{5, 0};
        if (select(0, &readable, nullptr, nullptr, &timeout) > 0)
        {
            SOCKET client = accept(listener, nullptr, nullptr);
            if (client != INVALID_SOCKET)
            {
                DWORD limit = 3000;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&limit), sizeof(limit));
                char request[4096];
                recv(client, request, sizeof(request), 0);
                const char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nSet-Cookie: "
                                        "secret=hidden\r\nConnection: close\r\n\r\nhello";
                send(client, response, sizeof(response) - 1, 0);
                closesocket(client);
            }
        }
    });
    r = Request{};
    r.tool = Tool::http;
    r.method = L"GET";
    r.direct = true;
    r.deadline_ms = 3000;
    r.target = L"http://127.0.0.1:" + std::to_wstring(ntohs(address.sin_port)) + L"/";
    auto http = run(r, cancel);
    server.join();
    closesocket(listener);
    WSACleanup();
    if (http.status != Status::complete)
        std::wcerr << http.summary << L'\n';
    check(http.status == Status::complete, "bounded asynchronous HTTP loopback GET");
    bool bytes = false, redacted = false;
    for (auto &row : http.rows)
    {
        if (row[0] == L"Body bytes received" && row[1] == L"5")
            bytes = true;
        if (row[0] == L"Set-Cookie" && row[1] == L"[redacted]")
            redacted = true;
    }
    check(bytes && redacted, "bounded body count and cookie redaction");
}
int main(int argc, char **)
{
    try
    {
        Request r;
        r.tool = Tool::ping;
        check(validate(r).has_value(), "empty ping target rejected");
        r.target = L"127.0.0.1";
        check(!validate(r), "valid loopback request");
        r.payload_bytes = 65536;
        check(validate(r).has_value(), "payload narrowing rejected");
        r.payload_bytes = 32;
        r.timeout_ms = 0;
        check(validate(r).has_value(), "infinite timeout rejected");
        r.timeout_ms = 1000;
        r.target = L"host name";
        check(validate(r).has_value(), "whitespace target rejected");
        auto packet = magic_packet(L"00:11:22:33:44:55");
        check(packet.has_value(), "valid MAC accepted");
        for (size_t i = 0; i < 6; ++i)
            check((*packet)[i] == 255, "magic prefix");
        for (size_t i = 6; i < 102; ++i)
            check((*packet)[i] == ((i - 6) % 6) * 17, "sixteen MAC repetitions");
        check(!magic_packet(L"00:11:22:33:44:55x"), "MAC suffix rejected");
        check(!magic_packet(L"00:11-22:33:44:55"), "mixed MAC separators rejected");
        std::vector<Probe> p;
        const Outcome outcomes[] = {Outcome::reply,      Outcome::reply,          Outcome::timeout, Outcome::reply,
                                    Outcome::reply,      Outcome::network_error,  Outcome::pending, Outcome::cancelled,
                                    Outcome::send_error, Outcome::operation_error};
        const double rtts[] = {10, 14, 0, 22, 18, 0, 0, 0, 0, 0};
        for (size_t i = 0; i < 10; ++i)
        {
            Probe q;
            q.sequence = i + 1;
            q.outcome = outcomes[i];
            if (q.outcome == Outcome::reply)
                q.rtt_ms = rtts[i];
            p.push_back(q);
        }
        auto s = statistics(p);
        check(s.attempted == 10 && s.sent == 9 && s.completed == 6 && s.received == 4 && s.lost == 2,
              "state partition");
        check(s.local_errors == 2 && s.pending == 1 && s.cancelled == 1, "excluded outcomes counted");
        check(approximately(*s.mean, 16) && approximately(*s.population_sd, std::sqrt(20.0)) && *s.p95 == 22,
              "independent RTT oracle");
        check(s.variation_pairs == 2 && *s.variation == 4 && approximately(*s.loss_percent, 100.0 / 3),
              "gaps break adjacency");
        std::swap(p[0], p[1]);
        check(*statistics(p).variation == 4, "sequence not completion order");
        check(!statistics({}).loss_percent, "empty loss unavailable");
        check(!statistics({}).mean, "empty RTT unavailable");
        p.clear();
        for (int i = 1; i <= 20; ++i)
        {
            Probe q;
            q.sequence = i;
            q.outcome = Outcome::reply;
            q.rtt_ms = i;
            p.push_back(q);
        }
        check(*statistics(p).p95 == 19, "nearest rank p95");
        check(parse_external_ip(" 8.8.8.8\r\n", Family::ipv4) == L"8.8.8.8", "external literal trim");
        check(!parse_external_ip("127.0.0.1", Family::ipv4), "non-global external address rejected");
        check(!parse_external_ip("8.8.8.8 1.1.1.1", Family::ipv4), "multiple tokens rejected");
        check(!parse_external_ip(std::string(1025, 'x'), Family::ipv4), "body limit enforced");
        check(!parse_external_ip("2001:db8::1", Family::ipv6), "documentation IPv6 rejected");
        check(!parse_external_ip("3fff::1", Family::ipv6), "additional documentation IPv6 rejected");
        check(parse_external_ip("192.0.10.1", Family::ipv4) == L"192.0.10.1", "ordinary global IPv4 not overfiltered");
        check(reverse_name(L"192.0.2.1") == L"1.2.0.192.in-addr.arpa", "PTR IPv4 name");
        check(reverse_name(L"::1") == L"1.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.ip6.arpa",
              "PTR IPv6 nibbles");
        check(classify_icmp_status(11010, true) == Outcome::timeout, "timeout classification");
        check(classify_icmp_status(11050, false) == Outcome::send_error, "generic failure not remote loss");
        std::atomic_bool cancel = true;
        r = Request{};
        r.tool = Tool::ping;
        r.target = L"127.0.0.1";
        check(run(r, cancel).status == Status::cancelled, "cancel before dispatch sends nothing");
        if (argc > 1)
            local_integration();
        ProbeHistory history(3);
        for (unsigned i = 1; i <= 5; ++i)
        {
            Probe q;
            q.sequence = i;
            q.start_ms = i * 1000;
            q.outcome = Outcome::reply;
            q.rtt_ms = i * 10;
            history.append(q);
        }
        check(history.retained().size() == 3 && history.evicted() == 2, "history retains bounded raw cohort");
        auto lifetime = history.session_statistics();
        check(lifetime.received == 5 && approximately(*lifetime.mean, 30) &&
                  approximately(*lifetime.population_sd, std::sqrt(200.0)),
              "eviction preserves session counters and stable moments");
        check(!lifetime.p95, "exact session percentile unavailable after raw eviction");
        auto window = probe_cohort(history.retained(), 5000, 2000);
        check(window.size() == 2 && window[0].sequence == 4 && window[1].sequence == 5,
              "rolling cohort excludes exact lower boundary and includes now");
        check(approximately(*statistics(window).mean, 45), "raw window statistics");
        check(probe_cohort(history.retained(), 2000, 1000).empty(), "future start times excluded");
        Probe q;
        q.sequence = 6;
        q.segment = 1;
        q.start_ms = 6000;
        q.outcome = Outcome::reply;
        q.rtt_ms = 70;
        history.append(q);
        check(history.session_statistics().variation_pairs == 4,
              "configuration or pause segment breaks variation adjacency");
        check(probe_cohort(history.retained(), 7000, 0).size() == 3, "zero window selects retained session");
        std::cout << "network tests passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
