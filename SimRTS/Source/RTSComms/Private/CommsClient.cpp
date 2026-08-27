#include "CommsClient.h"
#include "CommsSockets.h"
#include "RttSampler.h"

#include <chrono>
#include <chrono>
#include <cstdint>
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

constexpr char kUdpMagic[] = {'R', 'T', 'S', '1'};
constexpr int kUdpMaxPacket = 1200;
constexpr int kUdpMaxIdLen = 64;
constexpr uint8_t kUdpHello = 1;
constexpr uint8_t kUdpAck = 2;
constexpr uint8_t kUdpOrder = 3;
constexpr uint8_t kUdpPing = 4;
constexpr uint8_t kUdpPong = 5;
constexpr uint8_t kUdpKickoff = 6;
constexpr uint8_t kUdpKickoffAck = 7;

void PutU8(std::vector<uint8_t>& out, uint8_t value) {
	out.push_back(value);
}

void PutI32(std::vector<uint8_t>& out, int32_t value) {
	const uint32_t u = static_cast<uint32_t>(value);
	out.push_back(static_cast<uint8_t>(u));
	out.push_back(static_cast<uint8_t>(u >> 8));
	out.push_back(static_cast<uint8_t>(u >> 16));
	out.push_back(static_cast<uint8_t>(u >> 24));
}

void PutU32(std::vector<uint8_t>& out, uint32_t value) {
	out.push_back(static_cast<uint8_t>(value));
	out.push_back(static_cast<uint8_t>(value >> 8));
	out.push_back(static_cast<uint8_t>(value >> 16));
	out.push_back(static_cast<uint8_t>(value >> 24));
}

void PutU16(std::vector<uint8_t>& out, uint16_t value) {
	out.push_back(static_cast<uint8_t>(value));
	out.push_back(static_cast<uint8_t>(value >> 8));
}

bool PutStr(std::vector<uint8_t>& out, const std::string& value) {
	if (value.size() > kUdpMaxIdLen) {
		return false;
	}
	out.push_back(static_cast<uint8_t>(value.size()));
	out.insert(out.end(), value.begin(), value.end());
	return true;
}

bool ReadStr(const uint8_t* data, int size, int& off, std::string& out) {
	if (off >= size) {
		return false;
	}
	const int n = data[off];
	++off;
	if (n > kUdpMaxIdLen || off + n > size) {
		return false;
	}
	out.assign(reinterpret_cast<const char*>(data + off), static_cast<size_t>(n));
	off += n;
	return true;
}

bool ReadU8(const uint8_t* data, int size, int& off, uint8_t& out) {
	if (off >= size) {
		return false;
	}
	out = data[off];
	++off;
	return true;
}

bool ReadU16(const uint8_t* data, int size, int& off, uint16_t& out) {
	if (off + 2 > size) {
		return false;
	}
	out = static_cast<uint16_t>(data[off] | (data[off + 1] << 8));
	off += 2;
	return true;
}

bool ReadI32(const uint8_t* data, int size, int& off, int32_t& out) {
	if (off + 4 > size) {
		return false;
	}
	const uint32_t u = static_cast<uint32_t>(data[off])
		| (static_cast<uint32_t>(data[off + 1]) << 8)
		| (static_cast<uint32_t>(data[off + 2]) << 16)
		| (static_cast<uint32_t>(data[off + 3]) << 24);
	out = static_cast<int32_t>(u);
	off += 4;
	return true;
}

bool ReadU32(const uint8_t* data, int size, int& off, uint32_t& out) {
	if (off + 4 > size) {
		return false;
	}
	out = static_cast<uint32_t>(data[off])
		| (static_cast<uint32_t>(data[off + 1]) << 8)
		| (static_cast<uint32_t>(data[off + 2]) << 16)
		| (static_cast<uint32_t>(data[off + 3]) << 24);
	off += 4;
	return true;
}

bool DecodeUdpHeader(
	const uint8_t* data,
	int size,
	uint8_t& kind,
	std::string& room_id,
	std::string& player_id,
	std::string& token,
	int& off) {
	if (size < 5 || data[0] != 'R' || data[1] != 'T' || data[2] != 'S' || data[3] != '1') {
		return false;
	}
	kind = data[4];
	off = 5;
	if (!ReadStr(data, size, off, room_id) || !ReadStr(data, size, off, player_id)) {
		return false;
	}
	token.clear();
	if (kind == kUdpHello && !ReadStr(data, size, off, token)) {
		return false;
	}
	return true;
}

bool DecodeUdpOrderBody(const uint8_t* data, int size, int off, CommsOrder& order) {
	uint8_t type = 0;
	uint8_t is_next = 0;
	uint16_t count = 0;
	if (!ReadU8(data, size, off, type) || !ReadU8(data, size, off, is_next)) {
		return false;
	}
	if (!ReadI32(data, size, off, order.target_x) || !ReadI32(data, size, off, order.target_y)) {
		return false;
	}
	if (!ReadU16(data, size, off, count)) {
		return false;
	}
	order.type = type;
	order.is_next = is_next != 0;
	order.unit_ids.clear();
	order.unit_ids.reserve(count);
	for (uint16_t i = 0; i < count; ++i) {
		int32_t id = 0;
		if (!ReadI32(data, size, off, id)) {
			return false;
		}
		order.unit_ids.push_back(id);
	}
	if (!ReadI32(data, size, off, order.sim_player_id)) {
		return false;
	}
	if (!ReadU32(data, size, off, order.order_id)) {
		return false;
	}
	return ReadI32(data, size, off, order.actual_tick);
}

bool EncodeUdpHello(const std::string& room_id, const std::string& player_id, const std::string& token, std::vector<uint8_t>& out) {
	out.clear();
	out.insert(out.end(), kUdpMagic, kUdpMagic + 4);
	PutU8(out, kUdpHello);
	if (!PutStr(out, room_id) || !PutStr(out, player_id) || !PutStr(out, token)) {
		return false;
	}
	return out.size() <= kUdpMaxPacket;
}

bool EncodeUdpOrder(
	const std::string& room_id,
	const std::string& player_id,
	const CommsOrder& order,
	std::vector<uint8_t>& out) {
	if (order.unit_ids.size() > 64) {
		return false;
	}
	out.clear();
	out.insert(out.end(), kUdpMagic, kUdpMagic + 4);
	PutU8(out, kUdpOrder);
	if (!PutStr(out, room_id) || !PutStr(out, player_id)) {
		return false;
	}
	PutU8(out, order.type);
	PutU8(out, order.is_next ? 1 : 0);
	PutI32(out, order.target_x);
	PutI32(out, order.target_y);
	PutU16(out, static_cast<uint16_t>(order.unit_ids.size()));
	for (int32_t id : order.unit_ids) {
		PutI32(out, id);
	}
	PutI32(out, order.sim_player_id);
	PutU32(out, order.order_id);
	PutI32(out, order.actual_tick);
	return out.size() <= kUdpMaxPacket;
}

bool EncodeUdpPing(
	const std::string& room_id,
	const std::string& player_id,
	uint32_t seq,
	std::vector<uint8_t>& out) {
	out.clear();
	out.insert(out.end(), kUdpMagic, kUdpMagic + 4);
	PutU8(out, kUdpPing);
	if (!PutStr(out, room_id) || !PutStr(out, player_id)) {
		return false;
	}
	PutU32(out, seq);
	return out.size() <= kUdpMaxPacket;
}

bool EncodeUdpKickoffAck(
	const std::string& room_id,
	const std::string& player_id,
	uint32_t kickoff_id,
	std::vector<uint8_t>& out) {
	out.clear();
	out.insert(out.end(), kUdpMagic, kUdpMagic + 4);
	PutU8(out, kUdpKickoffAck);
	if (!PutStr(out, room_id) || !PutStr(out, player_id)) {
		return false;
	}
	PutU32(out, kickoff_id);
	return out.size() <= kUdpMaxPacket;
}

} // namespace

struct CommsRequest {
	CommsEventKind kind = CommsEventKind::Login;
	std::string arg;
};

struct CommsClient::Impl {
	std::string host = "127.0.0.1";
	int port = 8080;
	int udp_port = 8081;

	mutable std::mutex session_mu;
	CommsSession session;
	std::string joined_room;

	std::mutex request_mu;
	std::condition_variable request_cv;
	std::queue<CommsRequest> requests;
	bool stop = false;
	bool running = false;
	std::thread worker;
	std::thread udp_worker;

	std::mutex result_mu;
	std::queue<CommsEvent> results;

	UdpSocket udp_socket = kInvalidUdp;
	std::mutex udp_mu;
	std::condition_variable udp_cv;
	std::queue<std::vector<uint8_t>> udp_out;
	std::string hello_room;
	bool hello_acked = false;
	RttSampler rtt;

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

	void QueueUdp(std::vector<uint8_t> packet) {
		std::lock_guard<std::mutex> lock(udp_mu);
		udp_out.push(std::move(packet));
	}

	bool EnsureUdp(std::string* error) {
		std::lock_guard<std::mutex> lock(udp_mu);
		if (udp_socket != kInvalidUdp) {
			return true;
		}
		std::string init_error;
		if (!TcpInit(&init_error)) {
			if (error != nullptr) {
				*error = init_error;
			}
			return false;
		}
		udp_socket = UdpOpenBind(error);
		return udp_socket != kInvalidUdp;
	}

	void MaybeSendPing() {
		const auto now = std::chrono::steady_clock::now();
		if (!rtt.ShouldSend(now)) {
			return;
		}
		std::string room_id;
		std::string player_id;
		{
			std::lock_guard<std::mutex> lock(session_mu);
			room_id = joined_room;
			player_id = session.player_id;
		}
		if (room_id.empty() || player_id.empty()) {
			return;
		}
		const uint32_t seq = rtt.BeginPing(now);
		std::vector<uint8_t> packet;
		if (!EncodeUdpPing(room_id, player_id, seq, packet)) {
			return;
		}
		QueueUdp(std::move(packet));
	}

	void DrainUdpOut() {
		std::string host_copy;
		int udp_port_copy = 0;
		{
			std::lock_guard<std::mutex> lock(session_mu);
			host_copy = host;
			udp_port_copy = udp_port;
		}
		for (;;) {
			std::vector<uint8_t> packet;
			{
				std::lock_guard<std::mutex> lock(udp_mu);
				if (udp_out.empty() || udp_socket == kInvalidUdp) {
					return;
				}
				packet = std::move(udp_out.front());
				udp_out.pop();
			}
			UdpSendTo(
				udp_socket,
				host_copy.c_str(),
				static_cast<uint16_t>(udp_port_copy),
				reinterpret_cast<const char*>(packet.data()),
				static_cast<int>(packet.size()),
				nullptr);
		}
	}

	void HandleUdpPacket(const uint8_t* data, int size) {
		uint8_t kind = 0;
		std::string room_id;
		std::string player_id;
		std::string token;
		int off = 0;
		if (!DecodeUdpHeader(data, size, kind, room_id, player_id, token, off)) {
			return;
		}
		if (kind == kUdpAck) {
			std::lock_guard<std::mutex> lock(udp_mu);
			if (room_id == hello_room) {
				hello_acked = true;
			}
			udp_cv.notify_all();
			return;
		}
		if (kind == kUdpPong) {
			uint32_t seq = 0;
			if (ReadU32(data, size, off, seq)) {
				rtt.OnPong(seq, std::chrono::steady_clock::now());
			}
			return;
		}
		if (kind == kUdpKickoff) {
			uint32_t kickoff_id = 0;
			uint32_t remaining_ms = 0;
			if (!ReadU32(data, size, off, kickoff_id) || !ReadU32(data, size, off, remaining_ms)) {
				return;
			}
			std::string ack_room;
			std::string ack_player;
			{
				std::lock_guard<std::mutex> lock(session_mu);
				ack_room = joined_room;
				ack_player = session.player_id;
			}
			if (ack_room.empty()) {
				ack_room = room_id;
			}
			std::vector<uint8_t> ack;
			if (!ack_player.empty() && EncodeUdpKickoffAck(ack_room, ack_player, kickoff_id, ack)) {
				QueueUdp(std::move(ack));
			}
			CommsEvent event;
			event.kind = CommsEventKind::Kickoff;
			event.result.ok = true;
			event.result.room.id = std::move(room_id);
			event.result.kickoff.kickoff_id = kickoff_id;
			event.result.kickoff.remaining_ms = static_cast<int32_t>(remaining_ms);
			Push(std::move(event));
			return;
		}
		if (kind != kUdpOrder) {
			return;
		}
		CommsEvent event;
		event.kind = CommsEventKind::Order;
		event.result.ok = DecodeUdpOrderBody(data, size, off, event.result.order);
		event.result.session.player_id = std::move(player_id);
		event.result.room.id = std::move(room_id);
		if (!event.result.ok) {
			event.result.error = "malformed udp order";
		}
		Push(std::move(event));
	}

	void UdpLoop() {
		char buf[kUdpMaxPacket];
		while (true) {
			{
				std::lock_guard<std::mutex> lock(request_mu);
				if (stop) {
					return;
				}
			}
			DrainUdpOut();
			MaybeSendPing();
			DrainUdpOut();
			UdpSocket socket = kInvalidUdp;
			{
				std::lock_guard<std::mutex> lock(udp_mu);
				socket = udp_socket;
			}
			if (socket == kInvalidUdp) {
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
				continue;
			}
			const int n = UdpRecv(socket, buf, sizeof(buf));
			if (n > 0) {
				HandleUdpPacket(reinterpret_cast<const uint8_t*>(buf), n);
			}
		}
	}

	bool HelloForJoin(const std::string& room_id, std::string& error) {
		if (!EnsureUdp(&error)) {
			return false;
		}
		const CommsSession current = CopySession();
		std::vector<uint8_t> hello;
		if (!EncodeUdpHello(room_id, current.player_id, current.session_token, hello)) {
			error = "udp hello encode failed";
			return false;
		}
		{
			std::lock_guard<std::mutex> lock(udp_mu);
			hello_room = room_id;
			hello_acked = false;
		}
		for (int attempt = 0; attempt < 20; ++attempt) {
			{
				std::lock_guard<std::mutex> lock(request_mu);
				if (stop) {
					error = "stopped";
					return false;
				}
			}
			QueueUdp(hello);
			std::unique_lock<std::mutex> lock(udp_mu);
			if (udp_cv.wait_for(lock, std::chrono::milliseconds(100), [this] { return hello_acked; })) {
				lock.unlock();
				{
					std::lock_guard<std::mutex> session_lock(session_mu);
					joined_room = room_id;
				}
				rtt.Start();
				return true;
			}
		}
		error = "udp hello timeout";
		return false;
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
			} else if (request.kind == CommsEventKind::StartRoom) {
				path = "/StartRoom";
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
		if (request.kind == CommsEventKind::JoinRoom && result.ok) {
			std::string udp_error;
			if (!HelloForJoin(request.arg, udp_error)) {
				result.ok = false;
				result.error = udp_error;
			} else if (result.room.id.empty()) {
				result.room.id = request.arg;
			}
		}
		if (request.kind == CommsEventKind::LeaveRoom && result.ok) {
			{
				std::lock_guard<std::mutex> lock(session_mu);
				joined_room.clear();
			}
			rtt.Stop();
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

void CommsClient::SetHost(std::string host, int port, int udp_port) {
	std::lock_guard<std::mutex> lock(impl_->session_mu);
	impl_->host = std::move(host);
	impl_->port = port;
	impl_->udp_port = udp_port;
}

void CommsClient::SetPingConfig(int interval_ms, int keep_amount) {
	impl_->rtt.SetConfig(interval_ms, keep_amount);
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
		impl_->udp_worker = std::thread([this] { impl_->UdpLoop(); });
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
	impl_->udp_cv.notify_all();
	{
		std::lock_guard<std::mutex> lock(impl_->udp_mu);
		if (impl_->udp_socket != kInvalidUdp) {
			TcpClose(impl_->udp_socket);
			impl_->udp_socket = kInvalidUdp;
		}
	}
	if (impl_->worker.joinable()) {
		impl_->worker.join();
	}
	if (impl_->udp_worker.joinable()) {
		impl_->udp_worker.join();
	}
	std::lock_guard<std::mutex> lock(impl_->request_mu);
	impl_->running = false;
	impl_->rtt.Stop();
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

void CommsClient::StartRoom(std::string room_id) {
	Start();
	impl_->Enqueue({CommsEventKind::StartRoom, std::move(room_id)});
}

void CommsClient::SendOrder(CommsOrder order) {
	Start();
	std::string room_id;
	std::string player_id;
	{
		std::lock_guard<std::mutex> lock(impl_->session_mu);
		room_id = impl_->joined_room;
		player_id = impl_->session.player_id;
	}
	if (room_id.empty() || player_id.empty()) {
		return;
	}
	std::vector<uint8_t> packet;
	if (!EncodeUdpOrder(room_id, player_id, order, packet)) {
		return;
	}
	impl_->QueueUdp(std::move(packet));
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

int CommsClient::MinRttMs() const {
	return impl_->rtt.MinRttMs();
}

} // namespace SimRTS
