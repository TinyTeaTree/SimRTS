#include "CommsClient.h"
#include "CommsSockets.h"

#include <cstdlib>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace SimRTS {
namespace {

struct HttpResponse {
	bool transport_ok = false;
	int status = 0;
	std::string body;
	std::string error;
};

size_t SkipWs(const std::string& s, size_t i) {
	while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
		++i;
	}
	return i;
}

bool ParseJsonString(const std::string& s, size_t& i, std::string& out) {
	i = SkipWs(s, i);
	if (i >= s.size() || s[i] != '"') {
		return false;
	}
	++i;
	out.clear();
	while (i < s.size()) {
		const char c = s[i++];
		if (c == '"') {
			return true;
		}
		if (c == '\\' && i < s.size()) {
			out.push_back(s[i++]);
			continue;
		}
		out.push_back(c);
	}
	return false;
}

bool FindKeyValue(const std::string& json, const char* key, size_t start, size_t end, size_t& value_pos) {
	const std::string needle = std::string("\"") + key + "\"";
	size_t p = json.find(needle, start);
	if (p == std::string::npos || p >= end) {
		return false;
	}
	p = json.find(':', p + needle.size());
	if (p == std::string::npos || p >= end) {
		return false;
	}
	value_pos = p + 1;
	return true;
}

bool JsonStringField(const std::string& json, const char* key, std::string& out) {
	size_t v = 0;
	if (!FindKeyValue(json, key, 0, json.size(), v)) {
		return false;
	}
	return ParseJsonString(json, v, out);
}

bool JsonStringArrayField(const std::string& json, size_t start, size_t end, const char* key, std::vector<std::string>& out) {
	size_t v = 0;
	if (!FindKeyValue(json, key, start, end, v)) {
		return false;
	}
	v = SkipWs(json, v);
	if (v >= end || json[v] != '[') {
		return false;
	}
	++v;
	out.clear();
	while (v < end) {
		v = SkipWs(json, v);
		if (v >= end) {
			return false;
		}
		if (json[v] == ']') {
			return true;
		}
		if (json[v] == ',') {
			++v;
			continue;
		}
		std::string item;
		if (!ParseJsonString(json, v, item)) {
			return false;
		}
		out.push_back(std::move(item));
	}
	return false;
}

size_t ObjectEnd(const std::string& json, size_t open_brace) {
	int depth = 0;
	for (size_t i = open_brace; i < json.size(); ++i) {
		if (json[i] == '{') {
			++depth;
		} else if (json[i] == '}') {
			--depth;
			if (depth == 0) {
				return i + 1;
			}
		}
	}
	return std::string::npos;
}

bool ParseRoomObject(const std::string& json, size_t start, size_t end, CommsRoom& room) {
	size_t v = 0;
	if (!FindKeyValue(json, "id", start, end, v) || !ParseJsonString(json, v, room.id)) {
		return false;
	}
	JsonStringArrayField(json, start, end, "player_ids", room.player_ids);
	return true;
}

bool ParseRoomsArray(const std::string& json, std::vector<CommsRoom>& rooms) {
	size_t v = 0;
	if (!FindKeyValue(json, "rooms", 0, json.size(), v)) {
		return false;
	}
	v = SkipWs(json, v);
	if (v >= json.size() || json[v] != '[') {
		return false;
	}
	++v;
	rooms.clear();
	while (v < json.size()) {
		v = SkipWs(json, v);
		if (v >= json.size()) {
			return false;
		}
		if (json[v] == ']') {
			return true;
		}
		if (json[v] == ',') {
			++v;
			continue;
		}
		if (json[v] != '{') {
			return false;
		}
		const size_t end = ObjectEnd(json, v);
		if (end == std::string::npos) {
			return false;
		}
		CommsRoom room;
		if (!ParseRoomObject(json, v, end, room)) {
			return false;
		}
		rooms.push_back(std::move(room));
		v = end;
	}
	return false;
}

std::string JsonEscape(const std::string& s) {
	std::string out;
	out.reserve(s.size());
	for (const char c : s) {
		if (c == '\\' || c == '"') {
			out.push_back('\\');
		}
		out.push_back(c);
	}
	return out;
}

HttpResponse HttpOnce(
	const std::string& host,
	int port,
	const char* method,
	const char* path,
	const std::string& body,
	const std::string& session_token) {
	HttpResponse response;
	std::string error;
	if (!TcpInit(&error)) {
		response.error = error;
		return response;
	}

	const TcpSocket socket = TcpConnect(host.c_str(), static_cast<uint16_t>(port), &error);
	if (socket == kInvalidTcp) {
		response.error = error;
		return response;
	}

	std::ostringstream req;
	req << method << ' ' << path << " HTTP/1.0\r\n";
	req << "Host: " << host << ':' << port << "\r\n";
	req << "Connection: close\r\n";
	if (!session_token.empty()) {
		req << "X-Session-Token: " << session_token << "\r\n";
	}
	if (!body.empty()) {
		req << "Content-Type: application/json\r\n";
		req << "Content-Length: " << body.size() << "\r\n";
	}
	req << "\r\n";
	req << body;
	const std::string raw = req.str();
	if (!TcpSendAll(socket, raw.data(), static_cast<int>(raw.size()))) {
		response.error = "send failed";
		TcpClose(socket);
		return response;
	}

	std::string received;
	char buf[2048];
	for (;;) {
		const int n = TcpRecvSome(socket, buf, sizeof(buf));
		if (n < 0) {
			response.error = "recv failed";
			TcpClose(socket);
			return response;
		}
		if (n == 0) {
			break;
		}
		received.append(buf, static_cast<size_t>(n));
	}
	TcpClose(socket);

	const size_t header_end = received.find("\r\n\r\n");
	if (header_end == std::string::npos) {
		response.error = "malformed HTTP response";
		return response;
	}

	const size_t line_end = received.find("\r\n");
	int status = 0;
	if (line_end != std::string::npos) {
		const std::string status_line = received.substr(0, line_end);
		const size_t sp1 = status_line.find(' ');
		if (sp1 != std::string::npos) {
			status = std::atoi(status_line.c_str() + sp1 + 1);
		}
	}

	response.transport_ok = true;
	response.status = status;
	response.body = received.substr(header_end + 4);
	return response;
}

CommsResult FailLocal(const char* message) {
	CommsResult result;
	result.error = message;
	return result;
}

CommsResult FromHttp(const HttpResponse& http, CommsEventKind kind) {
	CommsResult result;
	if (!http.transport_ok) {
		result.error = http.error.empty() ? "transport failed" : http.error;
		return result;
	}
	result.http_status = http.status;
	std::string err;
	if (JsonStringField(http.body, "error", err) && !err.empty()) {
		result.error = err;
	}
	if (http.status >= 200 && http.status < 300) {
		result.ok = true;
		if (kind == CommsEventKind::Login) {
			JsonStringField(http.body, "session_token", result.session.session_token);
			JsonStringField(http.body, "player_id", result.session.player_id);
			JsonStringField(http.body, "nickname", result.session.nickname);
			if (result.session.session_token.empty()) {
				result.ok = false;
				result.error = "login missing session_token";
			}
		} else if (kind == CommsEventKind::GetRooms) {
			if (!ParseRoomsArray(http.body, result.rooms)) {
				result.ok = false;
				result.error = "malformed rooms list";
			}
		} else {
			if (!ParseRoomObject(http.body, 0, http.body.size(), result.room)) {
				result.ok = false;
				if (result.error.empty()) {
					result.error = "malformed room";
				}
			}
		}
	} else if (result.error.empty()) {
		result.error = "HTTP " + std::to_string(http.status);
	}
	return result;
}

} // namespace

struct CommsRequest {
	CommsEventKind kind = CommsEventKind::Login;
	std::string arg;
};

struct CommsClient::Impl {
	std::string host = "127.0.0.1";
	int port = 8080;

	mutable std::mutex session_mu;
	CommsSession session;

	std::mutex request_mu;
	std::condition_variable request_cv;
	std::queue<CommsRequest> requests;
	bool stop = false;
	bool running = false;
	std::thread worker;

	std::mutex result_mu;
	std::queue<CommsEvent> results;

	void Enqueue(CommsRequest request) {
		{
			std::lock_guard<std::mutex> lock(request_mu);
			requests.push(std::move(request));
		}
		request_cv.notify_one();
	}

	void Push(CommsEvent event) {
		std::lock_guard<std::mutex> lock(result_mu);
		results.push(std::move(event));
	}

	CommsSession CopySession() {
		std::lock_guard<std::mutex> lock(session_mu);
		return session;
	}

	void StoreSession(CommsSession next) {
		std::lock_guard<std::mutex> lock(session_mu);
		session = std::move(next);
	}

	CommsResult Execute(const CommsRequest& request) {
		std::string host_copy;
		int port_copy = 0;
		{
			std::lock_guard<std::mutex> lock(session_mu);
			host_copy = host;
			port_copy = port;
		}

		const CommsSession current = CopySession();
		const bool needs_session = request.kind != CommsEventKind::Login;
		if (needs_session && current.session_token.empty()) {
			return FailLocal("not logged in");
		}

		std::string body;
		const char* method = "POST";
		const char* path = "/Login";
		if (request.kind == CommsEventKind::Login) {
			body = std::string("{\"username\":\"") + JsonEscape(request.arg) + "\"}";
			path = "/Login";
		} else if (request.kind == CommsEventKind::GetRooms) {
			method = "GET";
			path = "/GetRooms";
		} else {
			body = std::string("{\"id\":\"") + JsonEscape(request.arg) + "\"}";
			if (request.kind == CommsEventKind::CreateRoom) {
				path = "/CreateRoom";
			} else if (request.kind == CommsEventKind::JoinRoom) {
				path = "/JoinRoom";
			} else {
				path = "/LeaveRoom";
			}
		}

		const std::string token = needs_session ? current.session_token : std::string();
		const HttpResponse http = HttpOnce(host_copy, port_copy, method, path, body, token);
		CommsResult result = FromHttp(http, request.kind);
		if (request.kind == CommsEventKind::Login && result.ok) {
			StoreSession(result.session);
		}
		return result;
	}

	void WorkerLoop() {
		for (;;) {
			CommsRequest request;
			{
				std::unique_lock<std::mutex> lock(request_mu);
				request_cv.wait(lock, [this] { return stop || !requests.empty(); });
				if (stop && requests.empty()) {
					return;
				}
				request = std::move(requests.front());
				requests.pop();
			}
			CommsEvent event;
			event.kind = request.kind;
			event.result = Execute(request);
			Push(std::move(event));
		}
	}
};

CommsClient::CommsClient() : impl_(new Impl()) {}

CommsClient::~CommsClient() {
	Stop();
}

void CommsClient::SetHost(std::string host, int port) {
	std::lock_guard<std::mutex> lock(impl_->session_mu);
	impl_->host = std::move(host);
	impl_->port = port;
}

void CommsClient::Start() {
	bool launch = false;
	{
		std::lock_guard<std::mutex> lock(impl_->request_mu);
		if (!impl_->running) {
			impl_->stop = false;
			impl_->running = true;
			launch = true;
		}
	}
	if (launch) {
		impl_->worker = std::thread([this] { impl_->WorkerLoop(); });
	}
}

void CommsClient::Stop() {
	{
		std::lock_guard<std::mutex> lock(impl_->request_mu);
		if (!impl_->running) {
			return;
		}
		impl_->stop = true;
	}
	impl_->request_cv.notify_all();
	if (impl_->worker.joinable()) {
		impl_->worker.join();
	}
	std::lock_guard<std::mutex> lock(impl_->request_mu);
	impl_->running = false;
}

void CommsClient::Login(std::string username) {
	Start();
	impl_->Enqueue({CommsEventKind::Login, std::move(username)});
}

void CommsClient::GetRooms() {
	Start();
	impl_->Enqueue({CommsEventKind::GetRooms, {}});
}

void CommsClient::CreateRoom(std::string room_id) {
	Start();
	impl_->Enqueue({CommsEventKind::CreateRoom, std::move(room_id)});
}

void CommsClient::JoinRoom(std::string room_id) {
	Start();
	impl_->Enqueue({CommsEventKind::JoinRoom, std::move(room_id)});
}

void CommsClient::LeaveRoom(std::string room_id) {
	Start();
	impl_->Enqueue({CommsEventKind::LeaveRoom, std::move(room_id)});
}

bool CommsClient::TryPop(CommsEvent& out) {
	std::lock_guard<std::mutex> lock(impl_->result_mu);
	if (impl_->results.empty()) {
		return false;
	}
	out = std::move(impl_->results.front());
	impl_->results.pop();
	return true;
}

std::string CommsClient::SessionToken() const {
	std::lock_guard<std::mutex> lock(impl_->session_mu);
	return impl_->session.session_token;
}

std::string CommsClient::PlayerId() const {
	std::lock_guard<std::mutex> lock(impl_->session_mu);
	return impl_->session.player_id;
}

std::string CommsClient::Nickname() const {
	std::lock_guard<std::mutex> lock(impl_->session_mu);
	return impl_->session.nickname;
}

} // namespace SimRTS
