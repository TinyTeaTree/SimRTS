#pragma once

#include "RTSCommsAPI.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace SimRTS {

// UDP RTT on the I/O thread. Game thread only reads MinRttMs().
class RTSCOMMS_API RttSampler {
public:
	void SetConfig(int interval_ms, int keep_amount);
	void Start();
	void Stop();
	bool Running() const;

	bool ShouldSend(std::chrono::steady_clock::time_point now) const;
	uint32_t BeginPing(std::chrono::steady_clock::time_point now);
	void OnPong(uint32_t seq, std::chrono::steady_clock::time_point now);

	// Minimum of the last keep_amount samples. -1 if none yet.
	int MinRttMs() const;

private:
	mutable std::mutex mu_;
	bool running_ = false;
	int interval_ms_ = 200;
	int keep_amount_ = 10;
	uint32_t next_seq_ = 1;
	std::chrono::steady_clock::time_point last_send_{};
	bool sent_once_ = false;
	std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> inflight_;
	std::deque<int> samples_;
};

} // namespace SimRTS
