#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace SimRTS {

enum class CommsEventKind {
	Login,
	GetRooms,
	CreateRoom,
	JoinRoom,
	LeaveRoom,
	StartRoom,
	Order,
	Kickoff,
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

struct CommsOrder {
	int32_t sim_player_id = 0;
	std::vector<int32_t> unit_ids;
	int32_t target_x = 0;
	int32_t target_y = 0;
	uint8_t type = 0;
	bool is_next = false;
};

struct CommsKickoff {
	uint32_t kickoff_id = 0;
	int32_t remaining_ms = 0;
};

struct CommsResult {
	bool ok = false;
	int http_status = 0;
	std::string error;
	CommsSession session;
	CommsRoom room;
	std::vector<CommsRoom> rooms;
	CommsOrder order;
	CommsKickoff kickoff;
};

struct CommsEvent {
	CommsEventKind kind = CommsEventKind::Login;
	CommsResult result;
};

} // namespace SimRTS
