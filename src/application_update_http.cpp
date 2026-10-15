#include "application_updates.h"

#ifdef WITH_UPDATE_CHECKER
#include <libaegisub/exception.h>

#include <memory>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace {
constexpr size_t MaxResponseSize = 4 * 1024 * 1024;
DEFINE_EXCEPTION(ApplicationUpdateHttpError, agi::Exception);

[[noreturn]] void RequestError(std::string const& detail) {
	throw ApplicationUpdateHttpError("Could not check Toshi-ban releases: " + detail);
}

#ifndef _WIN32
size_t ReceiveBody(char *data, size_t size, size_t count, void *context) {
	auto& body = *static_cast<std::string *>(context);
	const size_t bytes = size * count;
	if (bytes > MaxResponseSize - body.size()) return 0;
	try { body.append(data, bytes); }
	catch (...) { return 0; }
	return bytes;
}
#endif
}

std::string DownloadApplicationUpdateJson(std::string const& path) {
	std::string body;
#ifdef _WIN32
	using Handle = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>;
	Handle session(WinHttpOpen(L"Aegisub Toshi-ban update checker", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), WinHttpCloseHandle);
	if (!session) // Windows 7 does not support automatic proxy discovery.
		session.reset(WinHttpOpen(L"Aegisub Toshi-ban update checker", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if (!session) RequestError("could not initialize HTTPS.");
	WinHttpSetTimeouts(session.get(), 10000, 10000, 15000, 15000);
	DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
	if (!WinHttpSetOption(session.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols)))
		RequestError("could not enable TLS 1.2.");
	std::wstring host(ApplicationApiHost, ApplicationApiHost + sizeof(ApplicationApiHost) - 1);
	Handle connection(WinHttpConnect(session.get(), host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0), WinHttpCloseHandle);
	if (!connection) RequestError("could not connect to GitHub.");
	std::wstring request_path(path.begin(), path.end()); // API paths are percent-encoded ASCII.
	Handle request(WinHttpOpenRequest(connection.get(), L"GET", request_path.c_str(), nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE), WinHttpCloseHandle);
	if (!request) RequestError("could not create request.");
	if (!WinHttpSendRequest(request.get(), L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n",
		DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.get(), nullptr))
		RequestError("HTTPS request failed.");
	DWORD status = 0, length = sizeof(status);
	if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX))
		RequestError("could not read HTTP status.");
	if (status != 200) RequestError("GitHub returned HTTP " + std::to_string(status) + ".");
	char buffer[16384];
	for (;;) {
		DWORD read = 0;
		if (!WinHttpReadData(request.get(), buffer, sizeof(buffer), &read)) RequestError("could not read response.");
		if (!read) break;
		if (read > MaxResponseSize - body.size()) RequestError("response is too large.");
		body.append(buffer, read);
	}
#else
	// Function-static initialization is serialized; every request owns its handle.
	static const CURLcode initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
	if (initialized != CURLE_OK) RequestError("could not initialize HTTPS.");
	std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
	if (!handle) RequestError("could not create request.");
	std::string url = std::string("https://") + ApplicationApiHost + path;
	curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
	curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "Aegisub Toshi-ban update checker");
	curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, 35L);
	curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, ReceiveBody);
	curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &body);
	// libcurl's defaults verify both the server certificate and hostname.
	auto result = curl_easy_perform(handle.get());
	if (result != CURLE_OK) RequestError(curl_easy_strerror(result));
	long status = 0;
	curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
	if (status != 200) RequestError("GitHub returned HTTP " + std::to_string(status) + ".");
#endif
	return body;
}
#endif
