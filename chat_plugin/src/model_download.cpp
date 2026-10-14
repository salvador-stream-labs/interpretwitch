// SPDX-License-Identifier: GPL-2.0-or-later
#include "model_download.hpp"
#include <array>
#include <exception>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace awl {
void fetch_https(const std::string &url, const DownloadSink &sink,
                 const std::atomic<bool> &cancelled) {
    if (!url.starts_with("https://"))
        throw std::runtime_error("Model downloads require HTTPS.");
    if (cancelled)
        throw std::runtime_error("Download cancelled.");
#ifdef _WIN32
    // Only ASCII URLs compiled into the model catalogue are passed here.
    const std::wstring address(url.begin(), url.end());
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
    if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
        throw std::runtime_error("Invalid model download address.");
    struct Handle {
        HINTERNET value;
        ~Handle() {
            if (value)
                WinHttpCloseHandle(value);
        }
    };
    auto require = [&](bool okay) {
        if (cancelled)
            throw std::runtime_error("Download cancelled.");
        if (!okay)
            throw std::runtime_error(
                "Download failed. Check your connection and click Download model to retry.");
    };
    Handle session{WinHttpOpen(L"Interpretwitch", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    require(session.value != nullptr);
    require(WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000));
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    Handle connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)};
    require(connection.value != nullptr);
    Handle request{WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      WINHTTP_FLAG_SECURE)};
    require(request.value != nullptr);
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    require(WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect,
                             sizeof(redirect)));
    DWORD disable = WINHTTP_DISABLE_COOKIES;
    require(
        WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)));
    DWORD auth = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    require(WinHttpSetOption(request.value, WINHTTP_OPTION_AUTOLOGON_POLICY, &auth, sizeof(auth)));
    require(WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0));
    require(WinHttpReceiveResponse(request.value, nullptr));
    DWORD status = 0, length = sizeof(status);
    require(WinHttpQueryHeaders(
        request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX));
    require(status == 200);
    std::array<char, 65536> buffer{};
    for (;;) {
        require(true);
        DWORD count = 0;
        require(WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()),
                                &count));
        if (!count)
            break;
        sink(buffer.data(), count);
    }
#else
    // Process-lifetime initialization; never clean up another plugin's libcurl state.
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized != CURLE_OK)
        throw std::runtime_error("Cannot initialize model downloads.");
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> request(curl_easy_init(),
                                                                curl_easy_cleanup);
    if (!request)
        throw std::runtime_error("Cannot initialize model downloads.");
    struct Transfer {
        const DownloadSink &sink;
        const std::atomic<bool> &cancelled;
        std::exception_ptr error;
    } transfer{sink, cancelled, {}};
    curl_easy_setopt(request.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(request.get(), CURLOPT_PROTOCOLS, long(CURLPROTO_HTTPS));
    curl_easy_setopt(request.get(), CURLOPT_REDIR_PROTOCOLS, long(CURLPROTO_HTTPS));
    curl_easy_setopt(request.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(request.get(), CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(request.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(request.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(request.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(request.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(request.get(), CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(request.get(), CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(request.get(), CURLOPT_LOW_SPEED_TIME, 15L);
    curl_easy_setopt(request.get(), CURLOPT_USERAGENT, "Interpretwitch");
    curl_easy_setopt(
        request.get(), CURLOPT_WRITEFUNCTION,
        +[](char *data, size_t size, size_t count, void *pointer) -> size_t {
            auto &value = *static_cast<Transfer *>(pointer);
            try {
                value.sink(data, size * count);
                return size * count;
            } catch (...) {
                value.error = std::current_exception();
                return 0;
            }
        });
    curl_easy_setopt(request.get(), CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(request.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(
        request.get(), CURLOPT_XFERINFOFUNCTION,
        +[](void *pointer, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
            return static_cast<Transfer *>(pointer)->cancelled ? 1 : 0;
        });
    curl_easy_setopt(request.get(), CURLOPT_XFERINFODATA, &transfer);
    const auto result = curl_easy_perform(request.get());
    if (transfer.error)
        std::rethrow_exception(transfer.error);
    if (cancelled)
        throw std::runtime_error("Download cancelled.");
    long status = 0;
    curl_easy_getinfo(request.get(), CURLINFO_RESPONSE_CODE, &status);
    if (result != CURLE_OK || status != 200)
        throw std::runtime_error(
            std::string("Download failed (") +
            (result != CURLE_OK ? curl_easy_strerror(result) : "unexpected HTTP response") +
            "). Check your connection and click Download model to retry.");
#endif
}
} // namespace awl
