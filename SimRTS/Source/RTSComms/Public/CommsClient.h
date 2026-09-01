#pragma once

#include "RTSCommsAPI.h"
#include "CommsTypes.h"

#include <memory>
#include <string>

namespace SimRTS {

// Non-blocking matchmaking client. Enqueue methods return immediately.
// SimRTS must pump TryPop on the game thread and must never wait on I/O.
class RTSCOMMS_API CommsClient {
public:
	CommsClient();
	~CommsClient();

	CommsClient(const CommsClient&) = delete;
	CommsClient& operator=(const CommsClient&) = delete;

	void SetHost(std::string host, int port, int udp_port = 8081);
	void SetPingConfig(int interval_ms, int keep_amount);

	void Start();
	void Stop();

	void Login(std::string username);
	void GetRooms();
	void CreateRoom(std::string room_id);
	void JoinRoom(std::string room_id);
	void LeaveRoom(std::string room_id);
	void StartRoom(std::string room_id);
	void SendOrder(CommsOrder order);

	bool TryPop(CommsEvent& out);

	std::string SessionToken() const;
	std::string PlayerId() const;
	std::string Nickname() const;
	int MinRttMs() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace SimRTS
