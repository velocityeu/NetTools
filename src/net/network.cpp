#include "veu/network.hpp"
#include "icmp_dispatch.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <windns.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <semaphore>
#include <sstream>
#include <thread>

namespace veu::net
{
using Clock = std::chrono::steady_clock;
namespace
{
constexpr size_t row_cap = 10000;
std::wstring number(double n)
{
    std::wostringstream s;
    s << std::fixed << std::setprecision(2) << n;
    return s.str();
}
std::wstring error_text(DWORD e)
{
    wchar_t *p = nullptr;
    DWORD n =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, e, 0, reinterpret_cast<wchar_t *>(&p), 0, nullptr);
    std::wstring s = n ? std::wstring(p, n) : L"Native error";
    if (p)
        LocalFree(p);
    while (!s.empty() && iswspace(s.back()))
        s.pop_back();
    return s + L" (" + std::to_wstring(e) + L")";
}
struct Wsa
{
    bool ok;
    Wsa()
    {
        WSADATA d{};
        ok = WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }
    ~Wsa()
    {
        if (ok)
            WSACleanup();
    }
};
struct Socket
{
    SOCKET value = INVALID_SOCKET;
    ~Socket()
    {
        if (value != INVALID_SOCKET)
            closesocket(value);
    }
};
struct Address
{
    sockaddr_storage storage{};
    int length = 0;
    int family() const
    {
        return storage.ss_family;
    }
    sockaddr *ptr()
    {
        return reinterpret_cast<sockaddr *>(&storage);
    }
    const sockaddr *ptr() const
    {
        return reinterpret_cast<const sockaddr *>(&storage);
    }
};
std::optional<Address> literal(std::wstring_view text)
{
    if (text.empty() || text.size() > 256)
        return {};
    std::wstring s(text);
    Address a;
    auto *v4 = reinterpret_cast<sockaddr_in *>(&a.storage);
    if (InetPtonW(AF_INET, s.c_str(), &v4->sin_addr) == 1)
    {
        v4->sin_family = AF_INET;
        a.length = sizeof(sockaddr_in);
        return a;
    }
    auto percent = s.find(L'%');
    ULONG scope = 0;
    if (percent != s.npos)
    {
        auto zone = s.substr(percent + 1);
        if (zone.empty() || zone.size() > 10)
            return {};
        std::uint64_t value = 0;
        for (wchar_t c : zone)
        {
            if (c < L'0' || c > L'9')
                return {};
            value = value * 10 + (c - L'0');
            if (value > std::numeric_limits<ULONG>::max())
                return {};
        }
        scope = static_cast<ULONG>(value);
        s.resize(percent);
    }
    auto *v6 = reinterpret_cast<sockaddr_in6 *>(&a.storage);
    if (InetPtonW(AF_INET6, s.c_str(), &v6->sin6_addr) == 1)
    {
        v6->sin6_family = AF_INET6;
        v6->sin6_scope_id = scope;
        a.length = sizeof(sockaddr_in6);
        return a;
    }
    return {};
}
std::wstring address_text(const sockaddr *p)
{
    if (!p)
        return L"unavailable";
    wchar_t b[INET6_ADDRSTRLEN]{};
    if (p->sa_family == AF_INET)
    {
        auto *s = reinterpret_cast<const sockaddr_in *>(p);
        if (InetNtopW(AF_INET, const_cast<IN_ADDR *>(&s->sin_addr), b, _countof(b)))
            return b;
    }
    if (p->sa_family == AF_INET6)
    {
        auto *s = reinterpret_cast<const sockaddr_in6 *>(p);
        if (InetNtopW(AF_INET6, const_cast<IN6_ADDR *>(&s->sin6_addr), b, _countof(b)))
            return std::wstring(b) + (s->sin6_scope_id ? L"%" + std::to_wstring(s->sin6_scope_id) : L"");
    }
    return L"unavailable";
}
std::wstring text(const wchar_t *s)
{
    if (!s)
        return L"";
    size_t n = 0;
    while (n < 4096 && s[n])
        ++n;
    return std::wstring(s, n) + (n == 4096 ? L" [truncated]" : L"");
}
void fail(Result &r, DWORD e, std::wstring_view what)
{
    r.status = Status::failed;
    r.native_status = e;
    r.summary = std::wstring(what) + L": " + error_text(e);
}
void row(Result &r, std::vector<std::wstring> v)
{
    size_t bytes = 0;
    for (auto &cell : v)
    {
        if (cell.size() > 8192)
        {
            cell.resize(8192);
            cell += L" [truncated]";
            r.truncated = true;
        }
        bytes += cell.size() * sizeof(wchar_t);
    }
    if (r.rows.size() >= row_cap || bytes > 16 * 1024 * 1024 - r.retained_text_bytes)
    {
        r.truncated = true;
        return;
    }
    r.retained_text_bytes += bytes;
    r.rows.push_back(std::move(v));
}
std::wstring family_text(int f)
{
    return f == AF_INET ? L"IPv4" : f == AF_INET6 ? L"IPv6" : L"unspecified";
}
int requested_family(Family f)
{
    return f == Family::ipv4 ? AF_INET : f == Family::ipv6 ? AF_INET6 : AF_UNSPEC;
}
std::wstring outcome_text(Outcome o)
{
    switch (o)
    {
    case Outcome::reply:
        return L"Reply";
    case Outcome::timeout:
        return L"Timeout (no reply)";
    case Outcome::network_error:
        return L"ICMP/network status";
    case Outcome::send_error:
        return L"Local submission error";
    case Outcome::operation_error:
        return L"Local operation error";
    case Outcome::cancelled:
        return L"Cancelled";
    case Outcome::pending:
        return L"Pending";
    default:
        return L"Not sent";
    }
}
void pause(std::atomic_bool &cancel, Clock::time_point until)
{
    while (!cancel && Clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
}
std::atomic_uint dns_contexts{0};
struct DnsContext
{
    bool within_limit = dns_contexts.fetch_add(1) < 8;
    DNS_QUERY_REQUEST request{};
    DNS_QUERY_RESULT result{};
    DNS_QUERY_CANCEL cancellation{};
    DNS_ADDR_ARRAY servers{};
    std::wstring name;
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~DnsContext()
    {
        dns_contexts.fetch_sub(1);
        if (result.pQueryRecords)
            DnsRecordListFree(result.pQueryRecords, DnsFreeRecordList);
        if (done)
            CloseHandle(done);
    }
};
void WINAPI dns_complete(void *opaque, DNS_QUERY_RESULT *)
{
    std::unique_ptr<std::shared_ptr<DnsContext>> hold(static_cast<std::shared_ptr<DnsContext> *>(opaque));
    SetEvent((*hold)->done);
}
std::shared_ptr<DnsContext> dns_query(const Request &r, const std::wstring &name, WORD type, std::atomic_bool &cancel,
                                      DWORD &error)
{
    auto c = std::make_shared<DnsContext>();
    if (!c->within_limit)
    {
        error = ERROR_BUSY;
        return {};
    }
    if (!c->done)
    {
        error = GetLastError();
        return {};
    }
    c->name = name;
    c->request.Version = DNS_QUERY_REQUEST_VERSION1;
    c->request.QueryName = c->name.c_str();
    c->request.QueryType = type;
    c->request.QueryOptions = DNS_QUERY_STANDARD | (r.bypass_cache ? DNS_QUERY_BYPASS_CACHE : 0);
    c->request.InterfaceIndex = r.interface_index;
    c->result.Version = DNS_QUERY_RESULTS_VERSION1;
    if (!r.resolver.empty())
    {
        auto a = literal(r.resolver);
        if (!a)
        {
            error = ERROR_INVALID_PARAMETER;
            return {};
        }
        if (a->family() == AF_INET)
            reinterpret_cast<sockaddr_in *>(a->ptr())->sin_port = htons(53);
        else
            reinterpret_cast<sockaddr_in6 *>(a->ptr())->sin6_port = htons(53);
        c->servers.MaxCount = 1;
        c->servers.AddrCount = 1;
        c->servers.Family = static_cast<WORD>(a->family());
        memcpy(c->servers.AddrArray[0].MaxSa, a->ptr(), a->length);
        c->request.pDnsServerList = &c->servers;
        c->request.QueryOptions |= DNS_QUERY_BYPASS_CACHE | DNS_QUERY_NO_HOSTS_FILE;
    }
    auto *holder = new std::shared_ptr<DnsContext>(c);
    c->request.pQueryContext = holder;
    c->request.pQueryCompletionCallback = dns_complete;
    error = DnsQueryEx(&c->request, &c->result, &c->cancellation);
    if (error != DNS_REQUEST_PENDING)
    {
        delete holder;
        error = c->result.QueryStatus ? c->result.QueryStatus : error;
        return c;
    }
    const auto deadline = Clock::now() + std::chrono::milliseconds(std::min(r.deadline_ms, 5000u));
    while (WaitForSingleObject(c->done, 25) != WAIT_OBJECT_0)
    {
        if (cancel || Clock::now() >= deadline)
        {
            DnsCancelQuery(&c->cancellation);
            error = cancel ? ERROR_CANCELLED : ERROR_TIMEOUT;
            return {};
        }
    }
    error = c->result.QueryStatus;
    return c;
}
std::vector<Address> resolve(const Request &r, std::atomic_bool &cancel, DWORD &error)
{
    if (auto a = literal(r.target))
    {
        if (r.family != Family::any && a->family() != requested_family(r.family))
        {
            error = WSAEAFNOSUPPORT;
            return {};
        }
        error = 0;
        return {*a};
    }
    const auto resolution_deadline = Clock::now() + std::chrono::milliseconds(std::min(r.deadline_ms, 5000u));
    std::vector<Address> addresses;
    for (WORD type : {WORD(DNS_TYPE_A), WORD(DNS_TYPE_AAAA)})
    {
        if (cancel)
            break;
        if ((type == DNS_TYPE_A && r.family == Family::ipv6) || (type == DNS_TYPE_AAAA && r.family == Family::ipv4))
            continue;
        auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(resolution_deadline - Clock::now()).count();
        if (remaining <= 0)
        {
            error = ERROR_TIMEOUT;
            break;
        }
        Request query = r;
        query.deadline_ms = static_cast<std::uint32_t>(remaining);
        auto c = dns_query(query, r.target, type, cancel, error);
        if (c && error == 0)
        {
            for (auto *p = c->result.pQueryRecords; p && addresses.size() < 8; p = p->pNext)
            {
                Address a;
                if (p->wType == DNS_TYPE_A)
                {
                    auto *s = reinterpret_cast<sockaddr_in *>(&a.storage);
                    s->sin_family = AF_INET;
                    s->sin_addr.S_un.S_addr = p->Data.A.IpAddress;
                    a.length = sizeof(*s);
                    addresses.push_back(a);
                }
                else if (p->wType == DNS_TYPE_AAAA)
                {
                    auto *s = reinterpret_cast<sockaddr_in6 *>(&a.storage);
                    s->sin6_family = AF_INET6;
                    memcpy(&s->sin6_addr, &p->Data.AAAA.Ip6Address, 16);
                    a.length = sizeof(*s);
                    addresses.push_back(a);
                }
            }
        }
    }
    if (!addresses.empty())
        error = 0;
    else if (!error)
        error = DNS_INFO_NO_RECORDS;
    return addresses;
}
WORD record_type(std::wstring_view t)
{
    if (t == L"A")
        return DNS_TYPE_A;
    if (t == L"AAAA")
        return DNS_TYPE_AAAA;
    if (t == L"CNAME")
        return DNS_TYPE_CNAME;
    if (t == L"MX")
        return DNS_TYPE_MX;
    if (t == L"NS")
        return DNS_TYPE_NS;
    if (t == L"SOA")
        return DNS_TYPE_SOA;
    if (t == L"PTR")
        return DNS_TYPE_PTR;
    if (t == L"TXT")
        return DNS_TYPE_TEXT;
    if (t == L"SRV")
        return DNS_TYPE_SRV;
    return 0;
}
std::wstring type_text(WORD t)
{
    switch (t)
    {
    case DNS_TYPE_A:
        return L"A";
    case DNS_TYPE_AAAA:
        return L"AAAA";
    case DNS_TYPE_CNAME:
        return L"CNAME";
    case DNS_TYPE_MX:
        return L"MX";
    case DNS_TYPE_NS:
        return L"NS";
    case DNS_TYPE_SOA:
        return L"SOA";
    case DNS_TYPE_PTR:
        return L"PTR";
    case DNS_TYPE_TEXT:
        return L"TXT";
    case DNS_TYPE_SRV:
        return L"SRV";
    default:
        return std::to_wstring(t);
    }
}
std::wstring record_value(const DNS_RECORDW &p)
{
    wchar_t b[INET6_ADDRSTRLEN]{};
    switch (p.wType)
    {
    case DNS_TYPE_A:
        InetNtopW(AF_INET, const_cast<DWORD *>(&p.Data.A.IpAddress), b, _countof(b));
        return b;
    case DNS_TYPE_AAAA:
        InetNtopW(AF_INET6, const_cast<IP6_ADDRESS *>(&p.Data.AAAA.Ip6Address), b, _countof(b));
        return b;
    case DNS_TYPE_CNAME:
    case DNS_TYPE_NS:
    case DNS_TYPE_PTR:
        return text(p.Data.PTR.pNameHost);
    case DNS_TYPE_MX:
        return std::to_wstring(p.Data.MX.wPreference) + L" " + text(p.Data.MX.pNameExchange);
    case DNS_TYPE_SRV:
        return std::to_wstring(p.Data.SRV.wPriority) + L" " + std::to_wstring(p.Data.SRV.wWeight) + L" " +
               std::to_wstring(p.Data.SRV.wPort) + L" " + text(p.Data.SRV.pNameTarget);
    case DNS_TYPE_SOA:
        return text(p.Data.SOA.pNamePrimaryServer) + L" " + text(p.Data.SOA.pNameAdministrator) + L" serial=" +
               std::to_wstring(p.Data.SOA.dwSerialNo) + L" refresh=" + std::to_wstring(p.Data.SOA.dwRefresh) +
               L" retry=" + std::to_wstring(p.Data.SOA.dwRetry) + L" expire=" + std::to_wstring(p.Data.SOA.dwExpire) +
               L" minimum=" + std::to_wstring(p.Data.SOA.dwDefaultTtl);
    case DNS_TYPE_TEXT: {
        std::wstring s;
        for (DWORD i = 0; i < p.Data.TXT.dwStringCount && i < 128 && s.size() < 16384; ++i)
        {
            if (i)
                s += L" | ";
            s += L"\"" + text(p.Data.TXT.pStringArray[i]) + L"\"";
        }
        if (p.Data.TXT.dwStringCount > 128 || s.size() >= 16384)
            s += L" [truncated]";
        return s;
    }
    default:
        return L"Record data unavailable for this type";
    }
}
void dns(Result &out, const Request &r, std::atomic_bool &cancel)
{
    std::wstring name = r.target;
    if (r.dns_type == L"PTR")
    {
        auto rev = reverse_name(name);
        if (!rev.empty())
            name = rev;
    }
    out.columns = {L"Owner", L"Type", L"TTL (s)", L"Section", L"Data"};
    DWORD e = 0;
    auto c = dns_query(r, name, record_type(r.dns_type), cancel, e);
    out.context +=
        L"; resolver=" + (r.resolver.empty() ? L"Windows system resolver" : r.resolver) + L"; cache=" +
        (!r.resolver.empty() || r.bypass_cache ? L"bypass (custom excludes hosts file)" : L"system cache allowed");
    if (e)
    {
        fail(out, e, L"DNS query");
        return;
    }
    for (auto *p = c->result.pQueryRecords; p && out.rows.size() < row_cap; p = p->pNext)
        row(out, {text(p->pName), type_text(p->wType), std::to_wstring(p->dwTtl), std::to_wstring(p->Flags.S.Section),
                  record_value(*p)});
    out.summary = out.rows.empty()
                      ? L"DNS completed: no requested records"
                      : L"DNS response: " + std::to_wstring(out.rows.size()) + L" records (display cap 10,000)";
}
std::wstring opstate(IF_OPER_STATUS s)
{
    switch (s)
    {
    case IfOperStatusUp:
        return L"Up";
    case IfOperStatusDown:
        return L"Down";
    case IfOperStatusTesting:
        return L"Testing";
    case IfOperStatusDormant:
        return L"Dormant";
    case IfOperStatusNotPresent:
        return L"Not present";
    case IfOperStatusLowerLayerDown:
        return L"Lower layer down";
    default:
        return L"Unknown";
    }
}
std::wstring dad(IP_DAD_STATE s)
{
    switch (s)
    {
    case IpDadStatePreferred:
        return L"Preferred";
    case IpDadStateTentative:
        return L"Tentative";
    case IpDadStateDuplicate:
        return L"Duplicate";
    case IpDadStateDeprecated:
        return L"Deprecated";
    default:
        return L"Invalid";
    }
}
std::wstring mac_text(const BYTE *bytes, ULONG count)
{
    std::wostringstream s;
    for (ULONG i = 0; i < count; ++i)
    {
        if (i)
            s << L":";
        s << std::hex << std::setw(2) << std::setfill(L'0') << unsigned(bytes[i]);
    }
    return s.str();
}
void adapters(Result &out, std::atomic_bool &cancel)
{
    out.columns = {L"Adapter", L"Index v4/v6", L"State", L"Kind", L"Family",
                   L"Address", L"Prefix",      L"DAD",   L"MTU",  L"Valid/preferred lifetime (s)"};
    ULONG size = 15 * 1024;
    std::vector<BYTE> buffer;
    DWORD e = ERROR_BUFFER_OVERFLOW;
    for (int retry = 0; retry < 4 && e == ERROR_BUFFER_OVERFLOW && !cancel; ++retry)
    {
        if (size > 4 * 1024 * 1024)
        {
            fail(out, ERROR_NOT_ENOUGH_MEMORY, L"Adapter snapshot size cap");
            return;
        }
        buffer.resize(size);
        e = GetAdaptersAddresses(AF_UNSPEC,
                                 GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_INCLUDE_ALL_INTERFACES,
                                 nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()), &size);
    }
    if (cancel)
        return;
    if (e == ERROR_NO_DATA)
    {
        out.summary = L"No adapters returned";
        return;
    }
    if (e)
    {
        fail(out, e, L"Adapter snapshot");
        return;
    }
    for (auto *a = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()); a; a = a->Next)
    {
        auto add = [&](std::wstring kind, const sockaddr *addr, std::wstring prefix = L"", std::wstring state = L"",
                       std::wstring life = L"") {
            row(out, {text(a->FriendlyName), std::to_wstring(a->IfIndex) + L" / " + std::to_wstring(a->Ipv6IfIndex),
                      opstate(a->OperStatus), kind, addr ? family_text(addr->sa_family) : L"",
                      addr ? address_text(addr) : L"", prefix, state, std::to_wstring(a->Mtu), life});
        };
        if (!a->FirstUnicastAddress)
            add(L"No unicast address", nullptr);
        for (auto *u = a->FirstUnicastAddress; u; u = u->Next)
        {
            auto *p = u->Address.lpSockaddr;
            unsigned width = p && p->sa_family == AF_INET ? 32 : 128;
            add(L"Unicast", p, u->OnLinkPrefixLength <= width ? std::to_wstring(u->OnLinkPrefixLength) : L"unavailable",
                dad(u->DadState), std::to_wstring(u->ValidLifetime) + L" / " + std::to_wstring(u->PreferredLifetime));
        }
        for (auto *p = a->FirstDnsServerAddress; p; p = p->Next)
            add(L"DNS", p->Address.lpSockaddr);
        for (auto *p = a->FirstGatewayAddress; p; p = p->Next)
            add(L"Gateway", p->Address.lpSockaddr);
    }
    out.summary = L"Local adapter snapshot; IPv6 zones retained. No connectivity probes. Display cap 10,000 rows.";
}
void routes(Result &out, const Request &r)
{
    out.columns = {L"Family",       L"Destination/prefix", L"Next hop", L"Interface",
                   L"Route metric", L"Interface metric",   L"Protocol"};
    PMIB_IPFORWARD_TABLE2 table = nullptr;
    DWORD e = GetIpForwardTable2(static_cast<ADDRESS_FAMILY>(requested_family(r.family)), &table);
    if (e)
    {
        fail(out, e, L"Route snapshot");
        return;
    }
    std::unique_ptr<MIB_IPFORWARD_TABLE2, decltype(&FreeMibTable)> hold(table, FreeMibTable);
    for (ULONG i = 0; i < table->NumEntries && out.rows.size() < row_cap; ++i)
    {
        auto &a = table->Table[i];
        MIB_IPINTERFACE_ROW iface{};
        InitializeIpInterfaceEntry(&iface);
        iface.Family = a.DestinationPrefix.Prefix.si_family;
        iface.InterfaceLuid = a.InterfaceLuid;
        DWORD ie = GetIpInterfaceEntry(&iface);
        row(out, {family_text(a.DestinationPrefix.Prefix.si_family),
                  address_text(reinterpret_cast<sockaddr *>(&a.DestinationPrefix.Prefix)) + L"/" +
                      std::to_wstring(a.DestinationPrefix.PrefixLength),
                  address_text(reinterpret_cast<sockaddr *>(&a.NextHop)), std::to_wstring(a.InterfaceIndex),
                  std::to_wstring(a.Metric), ie ? L"unavailable" : std::to_wstring(iface.Metric),
                  std::to_wstring(a.Protocol)});
    }
    out.summary = L"Local route snapshot; effective metric includes route and interface metrics. Display cap 10,000.";
}
void neighbours(Result &out, const Request &r)
{
    out.columns = {L"Family", L"Address", L"Physical address", L"Interface", L"Reachability state"};
    PMIB_IPNET_TABLE2 table = nullptr;
    DWORD e = GetIpNetTable2(static_cast<ADDRESS_FAMILY>(requested_family(r.family)), &table);
    if (e)
    {
        fail(out, e, L"Neighbour snapshot");
        return;
    }
    std::unique_ptr<MIB_IPNET_TABLE2, decltype(&FreeMibTable)> hold(table, FreeMibTable);
    const wchar_t *names[] = {L"Unreachable", L"Incomplete", L"Probe", L"Delay", L"Stale", L"Reachable", L"Permanent"};
    for (ULONG i = 0; i < table->NumEntries && out.rows.size() < row_cap; ++i)
    {
        auto &a = table->Table[i];
        row(out, {family_text(a.Address.si_family), address_text(reinterpret_cast<sockaddr *>(&a.Address)),
                  mac_text(a.PhysicalAddress, std::min(a.PhysicalAddressLength, ULONG(IF_MAX_PHYS_ADDRESS_LENGTH))),
                  std::to_wstring(a.InterfaceIndex), unsigned(a.State) < 7 ? names[a.State] : L"Unknown"});
    }
    out.summary = L"Local neighbour cache; incomplete/stale entries are not host inventory. Display cap 10,000.";
}
} // namespace
std::optional<std::wstring> validate(const Request &r)
{
    if (r.continuous && r.tool != Tool::ping)
        return L"Continuous mode is available for Ping.";
    if (r.continuous && r.interval_ms < 100)
        return L"Continuous Ping requires an interval of at least 100 ms.";
    if (r.timeout_ms < 1 || r.timeout_ms > 10000 || r.deadline_ms < 1 || r.deadline_ms > 120000)
        return L"Timeout must be 1–10,000 ms and total deadline 1–120,000 ms.";
    if (r.count < 1 || r.count > 10000 || r.interval_ms > 60000 || r.payload_bytes > 65000 || r.max_hops < 1 ||
        r.max_hops > 64 || r.probes_per_hop < 1 || r.probes_per_hop > 3 || r.rounds < 1 || r.rounds > 10)
        return L"Probe options exceed bounded limits (payload at most 65,000 bytes).";
    if (r.mtu_ceiling < 68 || r.mtu_ceiling > 65028 || r.wol_burst < 1 || r.wol_burst > 3)
        return L"MTU ceiling or Wake-on-LAN burst outside bounds.";
    if (r.source.size() > 256 || r.resolver.size() > 256 || r.target.size() > 4096)
        return L"Input exceeds the supported length.";
    if (!r.source.empty() && !literal(r.source))
        return L"Source must be a numeric IPv4/IPv6 address; IPv6 scope must be numeric.";
    if (!r.resolver.empty() && !literal(r.resolver))
        return L"Resolver must be a numeric IPv4/IPv6 address.";
    if (r.tool == Tool::adapters || r.tool == Tool::routes || r.tool == Tool::neighbours || r.tool == Tool::external_ip)
        return {};
    if (r.target.empty())
        return L"Enter an explicit target before starting.";
    if (std::any_of(r.target.begin(), r.target.end(), [](wchar_t c) { return c <= 32 || c == 127; }))
        return L"Target contains whitespace or control characters.";
    if (r.tool == Tool::http)
    {
        if (r.target.rfind(L"https://", 0) != 0 && r.target.rfind(L"http://", 0) != 0)
            return L"HTTP inspection requires an http:// or https:// URL.";
        if (r.method != L"HEAD" && r.method != L"GET")
            return L"Only HEAD and bounded GET are supported.";
        return {};
    }
    if (r.target.size() > 253 || r.target.find_first_of(L"/\\[]@?#") != std::wstring::npos)
        return L"Enter one hostname or numeric address without a port, path or prefix.";
    if (r.target.find(L':') != r.target.npos && !literal(r.target))
        return L"Invalid IPv6 literal or unsupported port suffix.";
    if (r.tool == Tool::dns && !record_type(r.dns_type))
        return L"Supported DNS types: A, AAAA, CNAME, MX, NS, SOA, PTR, TXT, SRV.";
    if (r.tool == Tool::tcp && !r.port)
        return L"TCP requires a port from 1 to 65535.";
    if (r.tool == Tool::wake_on_lan)
    {
        auto a = literal(r.target);
        auto source = literal(r.source);
        if (!a || a->family() != AF_INET || !source || source->family() != AF_INET)
            return L"Wake-on-LAN requires explicit numeric IPv4 destination and local source address.";
        if (!magic_packet(r.mac))
            return L"MAC must contain six hexadecimal octets with matching : or - separators.";
    }
    return {};
}
std::optional<std::array<std::uint8_t, 102>> magic_packet(std::wstring_view s)
{
    if (s.size() != 17 || (s[2] != L':' && s[2] != L'-'))
        return {};
    std::array<uint8_t, 6> mac{};
    auto hex = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9')
            return c - L'0';
        if (c >= L'a' && c <= L'f')
            return c - L'a' + 10;
        if (c >= L'A' && c <= L'F')
            return c - L'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < 6; ++i)
    {
        int a = hex(s[i * 3]), b = hex(s[i * 3 + 1]);
        if (a < 0 || b < 0 || (i < 5 && s[i * 3 + 2] != s[2]))
            return {};
        mac[i] = static_cast<uint8_t>(a * 16 + b);
    }
    std::array<uint8_t, 102> p{};
    std::fill_n(p.begin(), 6, uint8_t{255});
    for (size_t i = 6; i < 102; ++i)
        p[i] = mac[(i - 6) % 6];
    return p;
}
Statistics statistics(const std::vector<Probe> &probes)
{
    Statistics s;
    std::vector<const Probe *> ordered;
    std::vector<double> values;
    double m2 = 0;
    for (auto &p : probes)
    {
        if (p.outcome == Outcome::not_sent)
        {
            ++s.not_sent;
            continue;
        }
        ++s.attempted;
        if (p.outcome != Outcome::send_error)
            ++s.sent;
        switch (p.outcome)
        {
        case Outcome::reply:
            ++s.received;
            ++s.completed;
            break;
        case Outcome::timeout:
            ++s.timeouts;
            ++s.lost;
            ++s.completed;
            break;
        case Outcome::network_error:
            ++s.network_errors;
            ++s.lost;
            ++s.completed;
            break;
        case Outcome::send_error:
        case Outcome::operation_error:
            ++s.local_errors;
            break;
        case Outcome::pending:
            ++s.pending;
            break;
        case Outcome::cancelled:
            ++s.cancelled;
            break;
        default:
            break;
        }
        ordered.push_back(&p);
        if (p.outcome == Outcome::reply && p.rtt_ms && std::isfinite(*p.rtt_ms) && *p.rtt_ms >= 0)
        {
            values.push_back(*p.rtt_ms);
            double old = s.mean.value_or(0);
            s.mean = old + (*p.rtt_ms - old) / values.size();
            m2 += (*p.rtt_ms - old) * (*p.rtt_ms - *s.mean);
        }
    }
    if (s.completed)
        s.loss_percent = 100.0 * static_cast<double>(s.lost) / s.completed;
    if (!values.empty())
    {
        std::sort(values.begin(), values.end());
        s.minimum = values.front();
        s.maximum = values.back();
        s.population_sd = std::sqrt(std::max(0.0, m2 / values.size()));
        s.p95 = values[(95 * values.size() + 99) / 100 - 1];
    }
    std::sort(ordered.begin(), ordered.end(), [](auto *a, auto *b) {
        return a->segment == b->segment ? a->sequence < b->sequence : a->segment < b->segment;
    });
    double changes = 0;
    for (size_t i = 1; i < ordered.size(); ++i)
    {
        auto &a = *ordered[i - 1];
        auto &b = *ordered[i];
        if (a.segment == b.segment && a.sequence != UINT64_MAX && b.sequence == a.sequence + 1 &&
            a.outcome == Outcome::reply && b.outcome == Outcome::reply && a.rtt_ms && b.rtt_ms)
        {
            changes += std::abs(*b.rtt_ms - *a.rtt_ms);
            ++s.variation_pairs;
        }
    }
    if (s.variation_pairs)
        s.variation = changes / s.variation_pairs;
    return s;
}
std::optional<std::wstring> parse_external_ip(std::string_view body, Family f)
{
    if (body.size() > 1024 || f == Family::any)
        return {};
    auto white = [](char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; };
    while (!body.empty() && white(body.front()))
        body.remove_prefix(1);
    while (!body.empty() && white(body.back()))
        body.remove_suffix(1);
    std::wstring s;
    for (unsigned char c : body)
    {
        if (c > 127 || c == 0)
            return {};
        s.push_back(c);
    }
    auto a = literal(s);
    if (!a || a->family() != requested_family(f) || s.find(L'%') != s.npos)
        return {};
    if (f == Family::ipv4)
    {
        auto *bytes = reinterpret_cast<const BYTE *>(&reinterpret_cast<const sockaddr_in *>(a->ptr())->sin_addr);
        unsigned x = bytes[0], y = bytes[1], z = bytes[2];
        if (x == 0 || x == 10 || x == 127 || x >= 224 || (x == 100 && (y >= 64 && y <= 127)) ||
            (x == 169 && y == 254) || (x == 172 && y >= 16 && y <= 31) ||
            (x == 192 && (y == 168 || (y == 0 && (z == 2 || (z == 0 && bytes[3] != 9 && bytes[3] != 10))) ||
                          (y == 88 && z == 99))) ||
            (x == 198 && (y == 18 || y == 19 || (y == 51 && z == 100))) || (x == 203 && y == 0 && z == 113))
            return {};
    }
    else
    {
        auto &bytes = reinterpret_cast<const sockaddr_in6 *>(a->ptr())->sin6_addr.u.Byte;
        // Conservative global-unicast acceptance, excluding IANA special-use blocks.
        // Registry: https://www.iana.org/assignments/iana-ipv6-special-registry/
        if ((bytes[0] & 0xe0) != 0x20 || (bytes[0] == 0x3f && bytes[1] == 0xfe) ||
            (bytes[0] == 0x3f && bytes[1] == 0xff && bytes[2] < 0x10) ||
            (bytes[0] == 0x20 && bytes[1] == 0x01 && bytes[2] == 0x0d && bytes[3] == 0xb8))
            return {};
        if (bytes[0] == 0x20 && bytes[1] == 0x01 && bytes[2] < 2)
        {
            bool anycast = bytes[2] == 0 && bytes[3] == 1 && bytes[15] >= 1 && bytes[15] <= 3;
            for (unsigned i = 4; i < 15; ++i)
                anycast = anycast && bytes[i] == 0;
            bool assigned = bytes[2] == 0 && (bytes[3] == 3 || (bytes[3] == 4 && bytes[4] == 1 && bytes[5] == 0x12) ||
                                              (bytes[3] & 0xf0) == 0x20 || (bytes[3] & 0xf0) == 0x30);
            if (!anycast && !assigned)
                return {};
        }
    }
    return address_text(a->ptr());
}
std::wstring reverse_name(std::wstring_view s)
{
    auto a = literal(s);
    if (!a)
        return {};
    std::wstring out;
    if (a->family() == AF_INET)
    {
        auto *b = reinterpret_cast<const BYTE *>(&reinterpret_cast<const sockaddr_in *>(a->ptr())->sin_addr);
        for (int i = 3; i >= 0; --i)
            out += std::to_wstring(b[i]) + L".";
        return out + L"in-addr.arpa";
    }
    auto *b = reinterpret_cast<const sockaddr_in6 *>(a->ptr())->sin6_addr.u.Byte;
    constexpr wchar_t hex[] = L"0123456789abcdef";
    for (int i = 15; i >= 0; --i)
    {
        out += hex[b[i] & 15];
        out += L'.';
        out += hex[b[i] >> 4];
        out += L'.';
    }
    return out + L"ip6.arpa";
}
Outcome classify_icmp_status(std::uint32_t s, bool submitted, bool trace)
{
    if (s == IP_SUCCESS)
        return Outcome::reply;
    if (s == IP_REQ_TIMED_OUT)
        return Outcome::timeout;
    if (trace && (s == IP_TTL_EXPIRED_TRANSIT || s == IP_TTL_EXPIRED_REASSEM))
        return Outcome::reply;
    switch (s)
    {
    case IP_DEST_NET_UNREACHABLE:
    case IP_DEST_HOST_UNREACHABLE:
    case IP_DEST_PROT_UNREACHABLE:
    case IP_DEST_PORT_UNREACHABLE:
    case IP_PACKET_TOO_BIG:
    case IP_TTL_EXPIRED_TRANSIT:
    case IP_TTL_EXPIRED_REASSEM:
    case IP_PARAM_PROBLEM:
    case IP_BAD_ROUTE:
    case IP_DEST_UNREACHABLE:
    case IP_TIME_EXCEEDED:
    case IP_BAD_HEADER:
    case IP_UNRECOGNIZED_NEXT_HEADER:
    case IP_ICMP_ERROR:
        return Outcome::network_error;
    default:
        return submitted ? Outcome::operation_error : Outcome::send_error;
    }
}
namespace
{
struct EchoHandle
{
    HANDLE h = INVALID_HANDLE_VALUE;
    ~EchoHandle()
    {
        if (h != INVALID_HANDLE_VALUE)
            IcmpCloseHandle(h);
    }
};
std::optional<Address> select_source(const Request &r, const Address &dest, Result &out)
{
    SOCKADDR_INET d{}, source{};
    memcpy(&d, dest.ptr(), dest.length);
    MIB_IPFORWARD_ROW2 best{};
    std::optional<Address> explicit_source;
    if (!r.source.empty())
    {
        explicit_source = literal(r.source);
        if (!explicit_source || explicit_source->family() != dest.family())
        {
            fail(out, WSAEAFNOSUPPORT, L"Source family");
            return {};
        }
        memcpy(&source, explicit_source->ptr(), explicit_source->length);
    }
    DWORD e = GetBestRoute2(nullptr, r.interface_index, explicit_source ? &source : nullptr, &d, 0, &best, &source);
    if (e)
    {
        fail(out, e, L"Route/source selection");
        return {};
    }
    Address result;
    result.length = dest.length;
    memcpy(&result.storage, &source, dest.length);
    if (explicit_source)
        result = *explicit_source;
    out.context += L"; source=" + address_text(result.ptr()) + L"; interface=" + std::to_wstring(best.InterfaceIndex) +
                   L"; next-hop=" + address_text(reinterpret_cast<sockaddr *>(&best.NextHop));
    return result;
}
Probe echo(EchoHandle &handle, const Address &dest, const Address &source, const Request &r, unsigned payload,
           unsigned hop, bool trace, bool df, std::atomic_bool &cancel, Clock::time_point run_start,
           Clock::time_point deadline = Clock::time_point::max())
{
    auto &slots = detail::icmp_slots();
    slots.acquire();
    struct Release
    {
        std::counting_semaphore<4> &slots;
        ~Release()
        {
            slots.release();
        }
    } release{slots};
    Probe p;
    p.start_ms = std::chrono::duration<double, std::milli>(Clock::now() - run_start).count();
    p.payload_bytes = payload;
    p.hop = hop;
    std::vector<BYTE> request(payload, 0x56);
    std::vector<BYTE> reply(sizeof(ICMP_ECHO_REPLY) + sizeof(ICMPV6_ECHO_REPLY) + payload + 64);
    IP_OPTION_INFORMATION options{};
    options.Ttl = static_cast<UCHAR>(hop);
    options.Flags = df ? IP_FLAG_DF : 0;
    // A permit can become available after a trace/MTU deadline. Never submit stale queued work.
    if (cancel || Clock::now() >= deadline)
    {
        p.outcome = Outcome::not_sent;
        p.native_status = cancel ? ERROR_CANCELLED : ERROR_TIMEOUT;
        return p;
    }
    DWORD n = 0;
    if (dest.family() == AF_INET)
    {
        n = IcmpSendEcho2Ex(handle.h, nullptr, nullptr, nullptr,
                            reinterpret_cast<const sockaddr_in *>(source.ptr())->sin_addr.S_un.S_addr,
                            reinterpret_cast<const sockaddr_in *>(dest.ptr())->sin_addr.S_un.S_addr, request.data(),
                            static_cast<WORD>(payload), &options, reply.data(), static_cast<DWORD>(reply.size()),
                            r.timeout_ms);
        if (n)
        {
            auto *response = reinterpret_cast<ICMP_ECHO_REPLY *>(reply.data());
            p.native_status = response->Status;
            sockaddr_in responder{};
            responder.sin_family = AF_INET;
            responder.sin_addr.S_un.S_addr = response->Address;
            p.responder = address_text(reinterpret_cast<sockaddr *>(&responder));
            if (response->Status == IP_SUCCESS)
                p.rtt_ms = response->RoundTripTime;
        }
    }
    else
    {
        auto src = *reinterpret_cast<const sockaddr_in6 *>(source.ptr());
        auto dst = *reinterpret_cast<const sockaddr_in6 *>(dest.ptr());
        n = Icmp6SendEcho2(handle.h, nullptr, nullptr, nullptr, &src, &dst, request.data(), static_cast<WORD>(payload),
                           &options, reply.data(), static_cast<DWORD>(reply.size()), r.timeout_ms);
        if (n)
        {
            auto *response = reinterpret_cast<ICMPV6_ECHO_REPLY *>(reply.data());
            p.native_status = response->Status;
            sockaddr_in6 responder{};
            responder.sin6_family = AF_INET6;
            memcpy(&responder.sin6_addr, response->Address.sin6_addr, 16);
            responder.sin6_scope_id = response->Address.sin6_scope_id;
            p.responder = address_text(reinterpret_cast<sockaddr *>(&responder));
            if (response->Status == IP_SUCCESS)
                p.rtt_ms = response->RoundTripTime;
        }
    }
    if (!n)
        p.native_status = GetLastError();
    p.outcome = classify_icmp_status(p.native_status, n != 0, trace);
    return p;
}
void probes(Result &out, const Request &r, std::atomic_bool &cancel, const Progress &progress,
            const std::atomic_bool *paused)
{
    DWORD e = 0;
    const auto began = Clock::now();
    auto addresses = resolve(r, cancel, e);
    if (addresses.empty())
    {
        fail(out, e, L"Target resolution");
        return;
    }
    const auto dest = addresses.front();
    out.context += L"; selected=" + address_text(dest.ptr()) + L"; family=" + family_text(dest.family()) +
                   L"; resolution_ms=" +
                   number(std::chrono::duration<double, std::milli>(Clock::now() - began).count());
    auto source = select_source(r, dest, out);
    if (!source)
        return;
    EchoHandle h;
    h.h = dest.family() == AF_INET ? IcmpCreateFile() : Icmp6CreateFile();
    if (h.h == INVALID_HANDLE_VALUE)
    {
        fail(out, GetLastError(), L"ICMP handle");
        return;
    }
    out.columns = {L"Sequence",         L"Round",      L"TTL/Hop",      L"Start (ms)", L"Responder", L"Outcome",
                   L"RTT (integer ms)", L"Data bytes", L"Native status"};
    const auto start = Clock::now();
    auto last_update = start;
    uint64_t sequence = 0, segment = 0;
    ProbeHistory history;
    auto emit = [&](Probe p) {
        p.sequence = ++sequence;
        p.segment = segment;
        history.append(p);
        if (out.rows.size() == 10000)
        {
            for (auto &cell : out.rows.front())
                out.retained_text_bytes -= cell.size() * sizeof(wchar_t);
            out.rows.erase(out.rows.begin());
        }
        out.probes = history.retained();
        out.evicted_probes = history.evicted();
        out.session_statistics = history.session_statistics();
        out.sampled_clock_ms = std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
        out.elapsed_ms =
            out.sampled_clock_ms - std::chrono::duration<double, std::milli>(start.time_since_epoch()).count();
        row(out, {std::to_wstring(p.sequence), std::to_wstring(p.round), std::to_wstring(p.hop), number(p.start_ms),
                  p.responder, outcome_text(p.outcome), p.rtt_ms ? number(*p.rtt_ms) : L"unavailable",
                  std::to_wstring(p.payload_bytes), std::to_wstring(p.native_status)});
        if (progress && Clock::now() - last_update >= std::chrono::milliseconds(100))
        {
            out.statistics = statistics(out.probes);
            progress(out);
            last_update = Clock::now();
        }
    };
    if (r.tool == Tool::mtu)
    {
        unsigned lower = 0;
        std::optional<unsigned> upper;
        unsigned ceiling = r.mtu_ceiling;
        SOCKADDR_INET route_dest{}, route_source{};
        memcpy(&route_dest, dest.ptr(), dest.length);
        memcpy(&route_source, source->ptr(), source->length);
        MIB_IPFORWARD_ROW2 route{};
        if (GetBestRoute2(nullptr, r.interface_index, &route_source, &route_dest, 0, &route, &route_source) == NO_ERROR)
        {
            MIB_IPINTERFACE_ROW iface{};
            InitializeIpInterfaceEntry(&iface);
            iface.Family = route_dest.si_family;
            iface.InterfaceLuid = route.InterfaceLuid;
            if (GetIpInterfaceEntry(&iface) == NO_ERROR)
            {
                ceiling = std::min(ceiling, static_cast<unsigned>(iface.NlMtu));
                out.context += L"; local interface MTU=" + std::to_wstring(iface.NlMtu);
            }
        }
        MIB_IPPATH_ROW path{};
        path.Source.si_family = static_cast<ADDRESS_FAMILY>(source->family());
        memcpy(&path.Source, source->ptr(), source->length);
        memcpy(&path.Destination, dest.ptr(), dest.length);
        if (GetIpPathEntry(&path) == NO_ERROR)
            out.context += L"; OS cached path MTU=" + std::to_wstring(path.PathMtu);
        unsigned overhead = dest.family() == AF_INET ? 28 : 48;
        unsigned candidate = ceiling;
        const auto deadline = start + std::chrono::milliseconds(std::min(r.deadline_ms, 60000u));
        for (unsigned attempt = 0; attempt < 40 && !cancel && Clock::now() < deadline; ++attempt)
        {
            if (candidate < overhead)
                break;
            auto p = echo(h, dest, *source, r, candidate - overhead, 128, false, dest.family() == AF_INET, cancel,
                          start, deadline);
            emit(p);
            if (p.outcome == Outcome::reply)
            {
                lower = std::max(lower, candidate);
                if (!upper || *upper <= lower)
                    break;
                candidate = lower + (*upper - lower + 1) / 2;
            }
            else if (p.native_status == IP_PACKET_TOO_BIG)
            {
                upper = candidate - 1;
                if (lower && *upper < lower)
                {
                    out.summary = L"Contradictory size observations; MTU inconclusive";
                    return;
                }
                candidate = lower ? lower + (*upper - lower + 1) / 2 : std::min(576u, *upper);
                if (candidate == 0)
                    break;
            }
            else
            {
                if (attempt % 2 == 0)
                    continue;
                break;
            }
            if (upper && lower && *upper == lower)
                break;
        }
        if (dest.family() == AF_INET6)
            out.summary = L"IPv6 echo-size observations only; source fragmentation is unverified, so MTU measurement "
                          L"is unavailable.";
        else
            out.summary = L"IPv4 DF observations: " +
                          (lower ? L"at least " + std::to_wstring(lower) + L" IP bytes succeeded"
                                 : L"no successful lower bound") +
                          L"; " +
                          (upper ? L"size-status constraint at most " + std::to_wstring(*upper) +
                                       L" bytes (local/remote provenance unavailable)"
                                 : L"upper bound unknown") +
                          L". Timeout is inconclusive; no advertised next-hop MTU exposed by this API.";
    }
    else if (r.tool == Tool::traceroute)
    {
        for (unsigned round_id = 1; round_id <= r.rounds && !cancel; ++round_id)
        {
            const auto deadline = Clock::now() + std::chrono::milliseconds(r.deadline_ms);
            bool reached = false;
            for (unsigned hop = 1; hop <= r.max_hops && !reached && !cancel && Clock::now() < deadline; ++hop)
            {
                for (unsigned n = 0; n < r.probes_per_hop && !cancel && Clock::now() < deadline; ++n)
                {
                    auto p = echo(h, dest, *source, r, r.payload_bytes, hop, true, false, cancel, start, deadline);
                    p.round = round_id;
                    emit(p);
                    if (p.native_status == IP_SUCCESS)
                    {
                        reached = true;
                        break;
                    }
                }
            }
            out.summary = reached ? L"Destination Echo Reply observed. Intermediate RTT unavailable where helper "
                                    L"timing is not guaranteed."
                                  : L"Trace stopped at hop/deadline cap without destination Echo Reply. Missing hop "
                                    L"replies do not identify broken links.";
            if (round_id < r.rounds && !cancel)
                pause(cancel, Clock::now() + std::chrono::seconds(10));
        }
    }
    else
    {
        for (std::uint64_t i = 0; (r.continuous || i < r.count) && !cancel; ++i)
        {
            if (paused && paused->load())
            {
                Probe gap;
                gap.start_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                gap.outcome = Outcome::not_sent;
                emit(gap);
                while (paused->load() && !cancel)
                {
                    out.sampled_clock_ms =
                        std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
                    out.elapsed_ms = out.sampled_clock_ms -
                                     std::chrono::duration<double, std::milli>(start.time_since_epoch()).count();
                    if (progress && Clock::now() - last_update >= std::chrono::milliseconds(200))
                    {
                        out.summary = L"Paused; no probes scheduled. Resume starts a new adjacency segment.";
                        progress(out);
                        last_update = Clock::now();
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
                }
                ++segment;
                if (cancel)
                    break;
            }

            auto p = echo(h, dest, *source, r, r.payload_bytes, 128, false, false, cancel, start);
            out.summary = r.continuous ? L"Continuous ping; latest 10,000 observations retained. Stop ends the run."
                                       : L"Ping in progress.";
            emit(p);
            if (r.continuous || i + 1 < r.count)
            {
                auto due = start + std::chrono::duration_cast<Clock::duration>(
                                       std::chrono::duration<double, std::milli>(p.start_ms)) +
                           std::chrono::milliseconds(r.interval_ms);
                while (!cancel && Clock::now() < due && !(paused && paused->load()))
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if (i == std::numeric_limits<std::uint64_t>::max() - 1)
            {
                out.summary = L"Sequence limit reached.";
                break;
            }
        }
        out.statistics = statistics(out.probes);
        auto &s = out.statistics;
        out.summary = L"Echo reply loss: " + std::to_wstring(s.lost) + L" / " + std::to_wstring(s.completed) +
                      L" completed" + (s.loss_percent ? L" (" + number(*s.loss_percent) + L"%)" : L" (unavailable)") +
                      L"; local errors=" + std::to_wstring(s.local_errors) + L"; mean=" +
                      (s.mean ? number(*s.mean) : L"unavailable") + L" ms; population SD=" +
                      (s.population_sd ? number(*s.population_sd) : L"unavailable") + L"; p95 nearest-rank=" +
                      (s.p95 ? number(*s.p95) : L"unavailable") + L"; adjacent variation=" +
                      (s.variation ? number(*s.variation) : L"unavailable") + L" (" +
                      std::to_wstring(s.variation_pairs) + L" pairs).";
    }
    out.statistics = statistics(out.probes);
    out.sampled_clock_ms = std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
    out.elapsed_ms = out.sampled_clock_ms - std::chrono::duration<double, std::milli>(start.time_since_epoch()).count();
    if (out.evicted_probes)
        out.summary += L" Retained latest 10,000 observations; earlier raw history evicted. Session p95 unavailable.";
}
void tcp(Result &out, const Request &r, std::atomic_bool &cancel)
{
    DWORD e = 0;
    auto addresses = resolve(r, cancel, e);
    if (addresses.empty())
    {
        fail(out, e, L"Target resolution");
        return;
    }
    out.columns = {L"Attempted address", L"Port", L"Outcome", L"Elapsed ms", L"Local endpoint"};
    auto deadline = Clock::now() + std::chrono::milliseconds(r.deadline_ms);
    bool connected = false;
    for (auto dest : addresses)
    {
        if (cancel || Clock::now() >= deadline)
            break;
        Socket socket;
        socket.value = ::socket(dest.family(), SOCK_STREAM, IPPROTO_TCP);
        if (socket.value == INVALID_SOCKET)
        {
            fail(out, WSAGetLastError(), L"Socket");
            return;
        }
        u_long mode = 1;
        if (ioctlsocket(socket.value, FIONBIO, &mode))
        {
            fail(out, WSAGetLastError(), L"Nonblocking socket");
            return;
        }
        if (r.interface_index)
        {
            DWORD index = dest.family() == AF_INET ? htonl(r.interface_index) : r.interface_index;
            if (setsockopt(socket.value, dest.family() == AF_INET ? IPPROTO_IP : IPPROTO_IPV6,
                           dest.family() == AF_INET ? IP_UNICAST_IF : IPV6_UNICAST_IF, reinterpret_cast<char *>(&index),
                           sizeof(index)))
            {
                fail(out, WSAGetLastError(), L"Source interface");
                return;
            }
        }
        if (!r.source.empty())
        {
            auto source = literal(r.source);
            if (!source || source->family() != dest.family())
            {
                row(out,
                    {address_text(dest.ptr()), std::to_wstring(r.port), L"Source family mismatch", L"0", r.source});
                continue;
            }
            if (bind(socket.value, source->ptr(), source->length))
            {
                row(out,
                    {address_text(dest.ptr()), std::to_wstring(r.port), error_text(WSAGetLastError()), L"0", r.source});
                continue;
            }
        }
        if (dest.family() == AF_INET)
            reinterpret_cast<sockaddr_in *>(dest.ptr())->sin_port = htons(r.port);
        else
            reinterpret_cast<sockaddr_in6 *>(dest.ptr())->sin6_port = htons(r.port);
        auto start = Clock::now();
        int result = connect(socket.value, dest.ptr(), dest.length);
        int error = result == 0 ? 0 : WSAGetLastError();
        if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS)
        {
            while (!cancel && Clock::now() < deadline)
            {
                fd_set writable, errors;
                FD_ZERO(&writable);
                FD_ZERO(&errors);
                FD_SET(socket.value, &writable);
                FD_SET(socket.value, &errors);
                timeval slice{0, 25000};
                int selected = select(0, nullptr, &writable, &errors, &slice);
                if (selected == SOCKET_ERROR)
                {
                    error = WSAGetLastError();
                    break;
                }
                if (selected)
                {
                    int len = sizeof(error);
                    if (getsockopt(socket.value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &len))
                        error = WSAGetLastError();
                    break;
                }
            }
            if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS)
                error = cancel ? ERROR_CANCELLED : WSAETIMEDOUT;
        }
        Address local;
        local.length = sizeof(local.storage);
        getsockname(socket.value, local.ptr(), &local.length);
        row(out, {address_text(dest.ptr()), std::to_wstring(r.port),
                  error ? error_text(error) : L"Connected (TCP handshake only)",
                  number(std::chrono::duration<double, std::milli>(Clock::now() - start).count()),
                  address_text(local.ptr())});
        connected |= error == 0;
        if (connected)
            break;
    }
    out.status = connected ? Status::complete : Status::failed;
    out.summary = connected ? L"TCP handshake succeeded; no application data sent."
                            : L"No TCP handshake succeeded in the bounded attempts.";
}
void wol(Result &out, const Request &r, std::atomic_bool &cancel)
{
    auto packet = magic_packet(r.mac);
    auto dest = *literal(r.target);
    auto source = *literal(r.source);
    Socket socket;
    socket.value = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket.value == INVALID_SOCKET)
    {
        fail(out, WSAGetLastError(), L"UDP socket");
        return;
    }
    BOOL enable = TRUE;
    if (setsockopt(socket.value, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char *>(&enable), sizeof(enable)) ||
        bind(socket.value, source.ptr(), source.length))
    {
        fail(out, WSAGetLastError(), L"Broadcast/source setup");
        return;
    }
    DWORD timeout = r.timeout_ms;
    setsockopt(socket.value, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeout));
    reinterpret_cast<sockaddr_in *>(dest.ptr())->sin_port = htons(r.port ? r.port : 9);
    out.columns = {L"Destination", L"Source", L"Port", L"Bytes", L"Outcome"};
    for (unsigned n = 0; n < r.wol_burst && !cancel; ++n)
    {
        int sent = sendto(socket.value, reinterpret_cast<const char *>(packet->data()),
                          static_cast<int>(packet->size()), 0, dest.ptr(), dest.length);
        if (sent != packet->size())
        {
            fail(out, WSAGetLastError(), L"Magic packet send");
            return;
        }
        row(out, {r.target, r.source, std::to_wstring(r.port ? r.port : 9), L"102",
                  L"UDP accepted locally; wake not verified"});
        if (n + 1 < r.wol_burst)
            pause(cancel, Clock::now() + std::chrono::milliseconds(100));
    }
    out.summary = L"Magic packet submitted. Delivery and device wake are not confirmed.";
}
} // namespace
Result run_http(const Request &, std::atomic_bool &);
Result run(const Request &r, std::atomic_bool &cancel, Progress progress, const std::atomic_bool *paused)
{
    Result out;
    static std::atomic_uint sessions{0};
    unsigned prior = sessions.fetch_add(1);
    struct Release
    {
        std::atomic_uint &n;
        ~Release()
        {
            n.fetch_sub(1);
        }
    } release{sessions};
    if (prior >= 8)
    {
        out.status = Status::failed;
        out.summary = L"At most eight network sessions can run simultaneously.";
        return out;
    }
    SYSTEMTIME now{};
    GetSystemTime(&now);
    wchar_t stamp[64]{};
    swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u UTC", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
               now.wSecond);
    out.observed_at = stamp;
    out.context = L"Target=" + r.target + L"; family=" + family_text(requested_family(r.family)) +
                  L"; requested source=" + (r.source.empty() ? L"system selected" : r.source) + L"; timeout_ms=" +
                  std::to_wstring(r.timeout_ms) + L"; deadline_ms=" + std::to_wstring(r.deadline_ms);
    if (cancel)
    {
        out.status = Status::cancelled;
        out.summary = L"Cancelled before dispatch; no probes sent";
        return out;
    }
    if (auto problem = validate(r))
    {
        out.status = Status::failed;
        out.summary = *problem;
        return out;
    }
    Wsa startup;
    if (!startup.ok)
    {
        fail(out, WSASYSNOTREADY, L"Winsock startup");
        return out;
    }
    try
    {
        switch (r.tool)
        {
        case Tool::adapters:
            adapters(out, cancel);
            break;
        case Tool::routes:
            routes(out, r);
            break;
        case Tool::neighbours:
            neighbours(out, r);
            break;
        case Tool::dns:
            dns(out, r, cancel);
            break;
        case Tool::ping:
        case Tool::traceroute:
        case Tool::mtu:
            probes(out, r, cancel, progress, paused);
            break;
        case Tool::tcp:
            tcp(out, r, cancel);
            break;
        case Tool::wake_on_lan:
            wol(out, r, cancel);
            break;
        case Tool::http:
        case Tool::external_ip: {
            auto response = run_http(r, cancel);
            response.observed_at = out.observed_at;
            response.context = out.context + L"; " + response.context;
            out = std::move(response);
            break;
        }
        }
    }
    catch (const std::bad_alloc &)
    {
        fail(out, ERROR_NOT_ENOUGH_MEMORY, L"Bounded result allocation");
    }
    catch (...)
    {
        fail(out, ERROR_GEN_FAILURE, L"Network operation");
    }
    if (out.truncated)
        out.summary += L" Retained table truncated at 10,000 rows / 16 MiB text.";
    if (cancel)
    {
        out.status = Status::cancelled;
        out.summary = L"Stopped; completed observations retained. " + out.summary;
    }
    return out;
}
} // namespace veu::net
