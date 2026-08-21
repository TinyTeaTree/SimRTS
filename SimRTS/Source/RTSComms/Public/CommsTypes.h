#pragma once

#include <string>
#include <vector>

namespace SimRTS {

enum class CommsEventKind {
	Login,
	GetRooms,
	CreateRoom,
	JoinRoom,
	LeaveRoom,
};

struct CommsSession {
	std::string session_token;
	std::string player_id;
	std::string nickname;
};

struct CommsRoom {
	std::string id;
	std::vector<std::string> player_ids;
};

struct CommsResult {
	bool ok = false;
	int http_status = 0;
	std::string error;
	CommsSession session;
	CommsRoom room;
	std::vector<CommsRoom> rooms;
};

struct CommsEvent {
	CommsEventKind kind = CommsEventKind::Login;
	CommsResult result;
};

} // namespace SimRTS
