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
	std::vector<uint8_t> seats;
};

struct CommsAckEntry {
	uint8_t seat = 0;
	uint32_t last_click_order_id = 0;
};

struct CommsOrder {
	uint8_t seat = 0;
	uint32_t order_id = 0;
	int32_t actual_tick = 0;
	int32_t hash_tick = 0;
	uint64_t state_hash = 0;
	std::vector<int32_t> unit_ids;
	int32_t target_x = 0;
	int32_t target_y = 0;
	uint8_t type = 0;
	bool is_next = false;
	std::vector<CommsAckEntry> acks;
	std::vector<CommsAckEntry> watermarks;
	std::vector<CommsOrder> piggybacks;
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
