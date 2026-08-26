#include "RttSampler.h"

#include <algorithm>

namespace SimRTS {

void RttSampler::SetConfig(int interval_ms, int keep_amount) {
	std::lock_guard<std::mutex> lock(mu_);
	interval_ms_ = interval_ms < 1 ? 1 : interval_ms;
	keep_amount_ = keep_amount < 1 ? 1 : keep_amount;
	while (static_cast<int>(samples_.size()) > keep_amount_) {
		samples_.pop_front();
	}
}

void RttSampler::Start() {
	std::lock_guard<std::mutex> lock(mu_);
	running_ = true;
	sent_once_ = false;
	next_seq_ = 1;
	inflight_.clear();
}

void RttSampler::Stop() {
	std::lock_guard<std::mutex> lock(mu_);
	running_ = false;
	inflight_.clear();
	samples_.clear();
	sent_once_ = false;
}

bool RttSampler::Running() const {
	std::lock_guard<std::mutex> lock(mu_);
	return running_;
}

bool RttSampler::ShouldSend(std::chrono::steady_clock::time_point now) const {
	std::lock_guard<std::mutex> lock(mu_);
	if (!running_) {
		return false;
	}
	if (!sent_once_) {
		return true;
	}
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_send_);
	return elapsed.count() >= interval_ms_;
}

uint32_t RttSampler::BeginPing(std::chrono::steady_clock::time_point now) {
	std::lock_guard<std::mutex> lock(mu_);
	if (inflight_.size() > 32) {
		inflight_.clear();
	}
	const uint32_t seq = next_seq_++;
	inflight_[seq] = now;
	last_send_ = now;
	sent_once_ = true;
	return seq;
}

void RttSampler::OnPong(uint32_t seq, std::chrono::steady_clock::time_point now) {
	std::lock_guard<std::mutex> lock(mu_);
	const auto it = inflight_.find(seq);
	if (it == inflight_.end()) {
		return;
	}
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
	inflight_.erase(it);
	int ms = static_cast<int>(elapsed.count());
	if (ms < 0) {
		ms = 0;
	}
	samples_.push_back(ms);
	while (static_cast<int>(samples_.size()) > keep_amount_) {
		samples_.pop_front();
	}
}

int RttSampler::MinRttMs() const {
	std::lock_guard<std::mutex> lock(mu_);
	if (samples_.empty()) {
		return -1;
	}
	int min_ms = samples_.front();
	for (int sample : samples_) {
		min_ms = std::min(min_ms, sample);
	}
	return min_ms;
}

} // namespace SimRTS
