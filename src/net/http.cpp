#include "veu/network.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

namespace veu::net
{
namespace
{
using Clock = std::chrono::steady_clock;
std::atomic_uint http_contexts{0};
struct HttpState
{
    std::vector<char> buffer = std::vector<char>(8192);
    bool within_limit = http_contexts.fetch_add(1) < 8;
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::mutex mutex;
    DWORD notification = 0, error = 0, bytes = 0;

    ~HttpState()
    {
        http_contexts.fetch_sub(1);
        if (event)
            CloseHandle(event);
    }
};
void CALLBACK completed(HINTERNET, DWORD_PTR context, DWORD status, LPVOID data, DWORD bytes)
{
    auto *holder = reinterpret_cast<std::shared_ptr<HttpState> *>(context);
    if (!holder)
        return;
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
    {
        delete holder;
        return;
    }
    auto state = *holder;
    if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE && status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE &&
        status != WINHTTP_CALLBACK_STATUS_READ_COMPLETE && status != WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
        return;
    std::lock_guard lock(state->mutex);
    state->notification = status;
    state->bytes = bytes;
    if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR && data && bytes >= sizeof(WINHTTP_ASYNC_RESULT))
        state->error = static_cast<WINHTTP_ASYNC_RESULT *>(data)->dwError;
    SetEvent(state->event);
}
struct Internet
{
    HINTERNET h = nullptr;
    ~Internet()
    {
        if (h)
            WinHttpCloseHandle(h);
    }
    Internet() = default;
    explicit Internet(HINTERNET v) : h(v)
    {
    }
    Internet(const Internet &) = delete;
};
void reset(const std::shared_ptr<HttpState> &s)
{
    std::lock_guard lock(s->mutex);
    s->notification = 0;
    s->error = 0;
    s->bytes = 0;
    ResetEvent(s->event);
}
DWORD wait(const std::shared_ptr<HttpState> &s, std::atomic_bool &cancel, Clock::time_point deadline, DWORD expected)
{
    while (WaitForSingleObject(s->event, 25) != WAIT_OBJECT_0)
    {
        if (cancel)
            return ERROR_CANCELLED;
        if (Clock::now() >= deadline)
            return ERROR_TIMEOUT;
    }
    std::lock_guard lock(s->mutex);
    if (s->error)
        return s->error;
    return s->notification == expected ? ERROR_SUCCESS : ERROR_INVALID_DATA;
}
std::wstring native(DWORD e)
{
    wchar_t *text = nullptr;
    DWORD count =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, e, 0, reinterpret_cast<wchar_t *>(&text), 0, nullptr);
    std::wstring out = count ? std::wstring(text, count) : L"HTTP/TLS native error";
    if (text)
        LocalFree(text);
    while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n'))
        out.pop_back();
    return out + L" (" + std::to_wstring(e) + L")";
}
void failure(Result &r, DWORD e, const wchar_t *phase)
{
    r.native_status = e;
    r.status = e == ERROR_CANCELLED ? Status::cancelled : Status::failed;
    r.summary = std::wstring(phase) + L": " + native(e);
}
std::wstring cert_name(PCCERT_CONTEXT certificate, DWORD flags)
{
    wchar_t name[1024]{};
    CertGetNameStringW(certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, flags, nullptr, name, _countof(name));
    return name;
}
std::wstring cert_date(FILETIME date)
{
    SYSTEMTIME t{};
    if (!FileTimeToSystemTime(&date, &t))
        return L"unavailable";
    wchar_t text[64]{};
    swprintf_s(text, L"%04u-%02u-%02u %02u:%02u:%02u UTC", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return text;
}
struct HttpResponse
{
    Result result;
    std::string body;
    DWORD status = 0;
};
HttpResponse inspect(const Request &options, const std::wstring &url, bool external, std::atomic_bool &cancel)
{
    HttpResponse response;
    auto &out = response.result;
    out.columns = {L"Field", L"Value"};
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        parts.dwUserNameLength = parts.dwPasswordLength = DWORD(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts))
    {
        failure(out, GetLastError(), L"URL");
        return response;
    }
    if ((parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) || parts.dwUserNameLength ||
        parts.dwPasswordLength || parts.dwHostNameLength == 0 || url.find(L'#') != url.npos)
    {
        failure(out, ERROR_INVALID_PARAMETER, L"URL must have an HTTP(S) host, no embedded credentials or fragment");
        return response;
    }
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path = parts.dwUrlPathLength ? std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) : L"/";
    if (parts.dwExtraInfoLength)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    const auto start = Clock::now();
    const auto deadline =
        start + std::chrono::milliseconds(external ? std::min(options.deadline_ms, 10000u) : options.deadline_ms);
    Internet session(WinHttpOpen(L"VEU NetTools/1.0",
                                 options.direct ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
    if (!session.h)
    {
        failure(out, GetLastError(), L"WinHTTP session");
        return response;
    }
    int phase_timeout = static_cast<int>(std::min(options.timeout_ms, 10000u));
    if (!WinHttpSetTimeouts(session.h, phase_timeout, phase_timeout, phase_timeout, phase_timeout))
    {
        failure(out, GetLastError(), L"HTTP phase timeouts");
        return response;
    }
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    if (!WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols)))
    {
        failure(out, GetLastError(), L"TLS 1.2 policy");
        return response;
    }
    Internet connection(WinHttpConnect(session.h, host.c_str(), parts.nPort, 0));
    if (!connection.h)
    {
        failure(out, GetLastError(), L"HTTP connection");
        return response;
    }
    const wchar_t *method = external ? L"GET" : options.method.c_str();
    Internet request(WinHttpOpenRequest(connection.h, method, path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request.h)
    {
        failure(out, GetLastError(), L"HTTP request");
        return response;
    }
    DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    if (external || !options.follow_redirects)
        disabled |= WINHTTP_DISABLE_REDIRECTS;
    DWORD auth = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH, headers_limit = external ? 16384 : 65536, redirect_limit = 5,
          redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
        !WinHttpSetOption(request.h, WINHTTP_OPTION_AUTOLOGON_POLICY, &auth, sizeof(auth)) ||
        !WinHttpSetOption(request.h, WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &headers_limit, sizeof(headers_limit)) ||
        !WinHttpSetOption(request.h, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &redirect_limit,
                          sizeof(redirect_limit)) ||
        !WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy)))
    {
        failure(out, GetLastError(), L"HTTP diagnostic safety options");
        return response;
    }
    auto state = std::make_shared<HttpState>();
    if (!state->within_limit)
    {
        failure(out, ERROR_BUSY, L"Pending HTTP request limit");
        return response;
    }
    if (!state->event)
    {
        failure(out, GetLastError(), L"HTTP completion event");
        return response;
    }
    auto *holder = new std::shared_ptr<HttpState>(state);
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(holder);
    if (!WinHttpSetOption(request.h, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)))
    {
        delete holder;
        failure(out, GetLastError(), L"HTTP context");
        return response;
    }
    if (WinHttpSetStatusCallback(request.h, completed,
                                 WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES,
                                 0) == WINHTTP_INVALID_STATUS_CALLBACK)
    {
        delete holder;
        failure(out, GetLastError(), L"HTTP callback");
        return response;
    }
    reset(state);
    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, context))
    {
        failure(out, GetLastError(), L"HTTP send");
        return response;
    }
    DWORD e = wait(state, cancel, deadline, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
    if (e)
    {
        failure(out, e, L"HTTP send/deadline");
        return response;
    }
    reset(state);
    if (!WinHttpReceiveResponse(request.h, nullptr))
    {
        failure(out, GetLastError(), L"HTTP receive");
        return response;
    }
    e = wait(state, cancel, deadline, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
    if (e)
    {
        failure(out, e, L"HTTP headers/deadline");
        return response;
    }
    DWORD size = sizeof(response.status);
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &response.status, &size, WINHTTP_NO_HEADER_INDEX))
    {
        failure(out, GetLastError(), L"HTTP status");
        return response;
    }
    out.rows.push_back({L"Method", method});
    out.rows.push_back({L"Requested URL", url});
    out.rows.push_back({L"HTTP status", std::to_wstring(response.status)});
    DWORD final_size = 0;
    WinHttpQueryOption(request.h, WINHTTP_OPTION_URL, nullptr, &final_size);
    if (final_size && final_size <= 16384)
    {
        std::vector<wchar_t> final_url(final_size / sizeof(wchar_t) + 1);
        if (WinHttpQueryOption(request.h, WINHTTP_OPTION_URL, final_url.data(), &final_size))
            out.rows.push_back({L"Final URL", final_url.data()});
    }
    out.context = options.direct ? L"Proxy mode: explicit direct"
                                 : L"Proxy mode: Windows automatic configuration; effective proxy details unavailable";
    WINHTTP_PROXY_INFO proxy{};
    DWORD proxy_size = sizeof(proxy);
    if (WinHttpQueryOption(request.h, WINHTTP_OPTION_PROXY, &proxy, &proxy_size))
    {
        if (proxy.lpszProxy)
        {
            out.context += L"; configured proxy=" + std::wstring(proxy.lpszProxy);
            GlobalFree(proxy.lpszProxy);
        }
        if (proxy.lpszProxyBypass)
            GlobalFree(proxy.lpszProxyBypass);
    }
    if (external && response.status != 200)
    {
        failure(out, ERROR_WINHTTP_INVALID_SERVER_RESPONSE, L"ipify requires HTTP 200; redirects rejected");
        return response;
    }
    DWORD raw_size = 0;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &raw_size,
                        WINHTTP_NO_HEADER_INDEX);
    if (raw_size > headers_limit)
    {
        failure(out, ERROR_BUFFER_OVERFLOW, L"Response header ceiling");
        return response;
    }
    if (!external && raw_size)
    {
        std::vector<wchar_t> raw(raw_size / sizeof(wchar_t) + 1);
        if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(),
                                &raw_size, WINHTTP_NO_HEADER_INDEX))
        {
            std::wistringstream stream(raw.data());
            std::wstring line;
            while (std::getline(stream, line) && out.rows.size() < 10000)
            {
                if (!line.empty() && line.back() == L'\r')
                    line.pop_back();
                auto colon = line.find(L':');
                std::wstring label = colon == line.npos ? L"Status line" : line.substr(0, colon);
                std::wstring lower = label;
                std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
                if (lower == L"set-cookie" || lower == L"set-cookie2")
                    out.rows.push_back({label, L"[redacted]"});
                else if (!line.empty())
                    out.rows.push_back({label, colon == line.npos ? line : line.substr(colon + 1)});
            }
        }
    }
    if (secure && !external)
    {
        PCCERT_CONTEXT certificate = nullptr;
        DWORD certificate_size = sizeof(certificate);
        if (WinHttpQueryOption(request.h, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &certificate, &certificate_size) &&
            certificate)
        {
            out.rows.push_back({L"Certificate subject", cert_name(certificate, 0)});
            out.rows.push_back({L"Certificate issuer", cert_name(certificate, CERT_NAME_ISSUER_FLAG)});
            out.rows.push_back({L"Certificate valid from", cert_date(certificate->pCertInfo->NotBefore)});
            out.rows.push_back({L"Certificate valid until", cert_date(certificate->pCertInfo->NotAfter)});
            CertFreeCertificateContext(certificate);
        }
        else
            out.rows.push_back({L"Certificate", L"Unavailable: " + native(GetLastError())});
        out.rows.push_back({L"TLS policy", L"TLS 1.2; standard hostname, validity and trust checks enabled"});
        out.rows.push_back({L"Revocation", L"Not independently verified; no revocation-success claim"});
        out.rows.push_back({L"Negotiated TLS version / cipher", L"Unavailable in the baseline inspection backend"});
    }
    if (external || options.method == L"GET")
    {
        const size_t body_cap = external ? 1024 : 1024 * 1024;
        size_t total = 0;
        for (;;)
        {
            reset(state);
            if (!WinHttpReadData(request.h, state->buffer.data(),
                                 static_cast<DWORD>(std::min(state->buffer.size(), body_cap - total + 1)), nullptr))
            {
                failure(out, GetLastError(), L"HTTP body");
                return response;
            }
            e = wait(state, cancel, deadline, WINHTTP_CALLBACK_STATUS_READ_COMPLETE);
            if (e)
            {
                failure(out, e, L"HTTP body/deadline");
                return response;
            }
            DWORD bytes = 0;
            {
                std::lock_guard lock(state->mutex);
                bytes = state->bytes;
            }
            if (bytes == 0)
                break;
            if (bytes > body_cap - total)
            {
                failure(out, ERROR_BUFFER_OVERFLOW, L"Response body ceiling");
                return response;
            }
            total += bytes;
            if (external)
                response.body.append(state->buffer.data(), bytes);
        }
        out.rows.push_back({L"Body bytes received", std::to_wstring(total)});
    }
    out.rows.push_back(
        {L"Elapsed ms",
         std::to_wstring(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count())});
    out.summary = L"HTTP response received; status " + std::to_wstring(response.status) +
                  L". Cookies and automatic credentials disabled.";
    if (options.follow_redirects && !external)
        out.summary +=
            L" At most five redirects; HTTPS downgrade prohibited. Intermediate redirect headers unavailable.";
    return response;
}
} // namespace
Result run_http(const Request &request, std::atomic_bool &cancel)
{
    if (request.tool == Tool::http)
        return inspect(request, request.target, false, cancel).result;
    Result out;
    out.columns = {L"Family", L"Provider endpoint", L"State", L"Observed address / error"};
    out.summary = L"External IP · Provided by ipify. Each address is observed for its HTTPS request; NAT/VPN/proxy can "
                  L"affect it.";
    bool success = false;
    for (auto family : {Family::ipv4, Family::ipv6})
    {
        if (cancel)
            break;
        if (request.family != Family::any && request.family != family)
            continue;
        std::wstring endpoint = family == Family::ipv4 ? L"https://api.ipify.org/" : L"https://api6.ipify.org/";
        auto response = inspect(request, endpoint, true, cancel);
        auto parsed = response.result.status == Status::complete ? parse_external_ip(response.body, family)
                                                                 : std::optional<std::wstring>{};
        std::wstring value = parsed ? *parsed
                             : response.result.status == Status::complete
                                 ? L"Invalid/non-global response; no address accepted"
                                 : response.result.summary;
        out.rows.push_back({family == Family::ipv4 ? L"IPv4" : L"IPv6", endpoint,
                            parsed   ? L"Observed"
                            : cancel ? L"Cancelled"
                                     : L"Unavailable",
                            value});
        out.context = response.result.context;
        success |= parsed.has_value();
    }
    out.status = cancel ? Status::cancelled : success ? Status::complete : Status::failed;
    return out;
}
} // namespace veu::net
