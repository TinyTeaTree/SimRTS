#pragma once

// Private TCP/UDP shim. Only CommsClient.cpp includes this.
// Windows = WinSock2, Mac = POSIX.

#include <cstdint>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using TcpSocket = SOCKET;
inline constexpr TcpSocket kInvalidTcp = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/time.h>
#include <cerrno>
#include <cstring>
using TcpSocket = int;
inline constexpr TcpSocket kInvalidTcp = -1;
#endif

inline bool TcpInit(std::string* error) {
#ifdef _WIN32
	WSADATA data;
	const int rc = WSAStartup(MAKEWORD(2, 2), &data);
	if (rc != 0) {
		if (error != nullptr) {
			*error = "WSAStartup failed";
		}
		return false;
	}
#else
	(void)error;
#endif
	return true;
}

inline void TcpShutdown() {
#ifdef _WIN32
	WSACleanup();
#endif
}

inline void TcpClose(TcpSocket socket) {
	if (socket == kInvalidTcp) {
		return;
	}
#ifdef _WIN32
	closesocket(socket);
#else
	close(socket);
#endif
}

inline std::string TcpLastError() {
#ifdef _WIN32
	return "winsock error " + std::to_string(WSAGetLastError());
#else
	return std::strerror(errno);
#endif
}

inline TcpSocket TcpConnect(const char* host, uint16_t port, std::string* error) {
	const TcpSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket == kInvalidTcp) {
		if (error != nullptr) {
			*error = "socket: " + TcpLastError();
		}
		return kInvalidTcp;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		if (error != nullptr) {
			*error = "invalid IPv4 host";
		}
		TcpClose(socket);
		return kInvalidTcp;
	}

	if (connect(socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
		if (error != nullptr) {
			*error = "connect: " + TcpLastError();
		}
		TcpClose(socket);
		return kInvalidTcp;
	}
	return socket;
}

inline bool TcpSendAll(TcpSocket socket, const char* data, int length) {
	int sent = 0;
	while (sent < length) {
#ifdef _WIN32
		const int n = ::send(socket, data + sent, length - sent, 0);
		if (n == SOCKET_ERROR || n == 0) {
			return false;
		}
#else
		const ssize_t n = ::send(socket, data + sent, static_cast<size_t>(length - sent), 0);
		if (n <= 0) {
			return false;
		}
#endif
		sent += static_cast<int>(n);
	}
	return true;
}

inline int TcpRecvSome(TcpSocket socket, char* buffer, int capacity) {
#ifdef _WIN32
	return ::recv(socket, buffer, capacity, 0);
#else
	return static_cast<int>(::recv(socket, buffer, static_cast<size_t>(capacity), 0));
#endif
}

using UdpSocket = TcpSocket;
inline constexpr UdpSocket kInvalidUdp = kInvalidTcp;

inline bool UdpSetNonBlock(UdpSocket socket) {
#ifdef _WIN32
	u_long mode = 1;
	return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
	const int flags = fcntl(socket, F_GETFL, 0);
	if (flags < 0) {
		return false;
	}
	return fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

struct UdpWake {
	UdpSocket socket = kInvalidUdp;
	uint16_t port = 0;
};

inline bool UdpWakeOpen(UdpWake* wake, std::string* error) {
	if (wake == nullptr) {
		return false;
	}
	wake->socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (wake->socket == kInvalidUdp) {
		if (error != nullptr) {
			*error = "udp wake socket: " + TcpLastError();
		}
		return false;
	}
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;
	if (bind(wake->socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
		if (error != nullptr) {
			*error = "udp wake bind: " + TcpLastError();
		}
		TcpClose(wake->socket);
		wake->socket = kInvalidUdp;
		return false;
	}
	sockaddr_in bound{};
#ifdef _WIN32
	int len = sizeof(bound);
#else
	socklen_t len = sizeof(bound);
#endif
	if (getsockname(wake->socket, reinterpret_cast<sockaddr*>(&bound), &len) != 0) {
		if (error != nullptr) {
			*error = "udp wake name: " + TcpLastError();
		}
		TcpClose(wake->socket);
		wake->socket = kInvalidUdp;
		return false;
	}
	wake->port = ntohs(bound.sin_port);
	if (!UdpSetNonBlock(wake->socket)) {
		if (error != nullptr) {
			*error = "udp wake nonblock: " + TcpLastError();
		}
		TcpClose(wake->socket);
		wake->socket = kInvalidUdp;
		wake->port = 0;
		return false;
	}
	return true;
}

inline void UdpWakeClose(UdpWake* wake) {
	if (wake == nullptr) {
		return;
	}
	TcpClose(wake->socket);
	wake->socket = kInvalidUdp;
	wake->port = 0;
}

inline void UdpWakeNotify(const UdpWake& wake) {
	if (wake.socket == kInvalidUdp || wake.port == 0) {
		return;
	}
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(wake.port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	const char byte = 0;
#ifdef _WIN32
	::sendto(wake.socket, &byte, 1, 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#else
	::sendto(wake.socket, &byte, 1, 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#endif
}

inline void UdpWakeDrain(UdpSocket socket) {
	if (socket == kInvalidUdp) {
		return;
	}
	char buf[8];
	for (;;) {
#ifdef _WIN32
		const int n = ::recvfrom(socket, buf, sizeof(buf), 0, nullptr, nullptr);
		if (n == SOCKET_ERROR) {
			break;
		}
#else
		const int n = static_cast<int>(::recvfrom(socket, buf, sizeof(buf), 0, nullptr, nullptr));
		if (n < 0) {
			break;
		}
#endif
		if (n <= 0) {
			break;
		}
	}
}

struct UdpPollResult {
	bool udp = false;
	bool wake = false;
};

inline UdpPollResult UdpPollWait(UdpSocket udp, UdpSocket wake, int timeout_ms) {
	UdpPollResult out;
#ifdef _WIN32
	WSAPOLLFD fds[2];
	ULONG n = 0;
	int udp_index = -1;
	int wake_index = -1;
	if (udp != kInvalidUdp) {
		udp_index = static_cast<int>(n);
		fds[n].fd = udp;
		fds[n].events = POLLIN;
		fds[n].revents = 0;
		++n;
	}
	if (wake != kInvalidUdp) {
		wake_index = static_cast<int>(n);
		fds[n].fd = wake;
		fds[n].events = POLLIN;
		fds[n].revents = 0;
		++n;
	}
	if (n == 0) {
		return out;
	}
	if (WSAPoll(fds, n, timeout_ms) <= 0) {
		return out;
	}
	if (udp_index >= 0 && (fds[udp_index].revents & POLLIN) != 0) {
		out.udp = true;
	}
	if (wake_index >= 0 && (fds[wake_index].revents & POLLIN) != 0) {
		out.wake = true;
	}
#else
	pollfd fds[2];
	nfds_t n = 0;
	int udp_index = -1;
	int wake_index = -1;
	if (udp != kInvalidUdp) {
		udp_index = static_cast<int>(n);
		fds[n].fd = udp;
		fds[n].events = POLLIN;
		fds[n].revents = 0;
		++n;
	}
	if (wake != kInvalidUdp) {
		wake_index = static_cast<int>(n);
		fds[n].fd = wake;
		fds[n].events = POLLIN;
		fds[n].revents = 0;
		++n;
	}
	if (n == 0) {
		return out;
	}
	if (poll(fds, n, timeout_ms) <= 0) {
		return out;
	}
	if (udp_index >= 0 && (fds[udp_index].revents & POLLIN) != 0) {
		out.udp = true;
	}
	if (wake_index >= 0 && (fds[wake_index].revents & POLLIN) != 0) {
		out.wake = true;
	}
#endif
	return out;
}

inline UdpSocket UdpOpenBind(std::string* error) {
	const UdpSocket socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (socket == kInvalidUdp) {
		if (error != nullptr) {
			*error = "udp socket: " + TcpLastError();
		}
		return kInvalidUdp;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = 0;
	if (bind(socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
		if (error != nullptr) {
			*error = "udp bind: " + TcpLastError();
		}
		TcpClose(socket);
		return kInvalidUdp;
	}

	if (!UdpSetNonBlock(socket)) {
		if (error != nullptr) {
			*error = "udp nonblock: " + TcpLastError();
		}
		TcpClose(socket);
		return kInvalidUdp;
	}
	return socket;
}

inline bool UdpSendTo(
	UdpSocket socket,
	const char* host,
	uint16_t port,
	const char* data,
	int length,
	std::string* error) {
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		if (error != nullptr) {
			*error = "invalid IPv4 host";
		}
		return false;
	}
#ifdef _WIN32
	const int n = ::sendto(socket, data, length, 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
	if (n == SOCKET_ERROR || n != length) {
		if (error != nullptr) {
			*error = "udp send: " + TcpLastError();
		}
		return false;
	}
#else
	const ssize_t n = ::sendto(
		socket,
		data,
		static_cast<size_t>(length),
		0,
		reinterpret_cast<sockaddr*>(&addr),
		sizeof(addr));
	if (n != length) {
		if (error != nullptr) {
			*error = "udp send: " + TcpLastError();
		}
		return false;
	}
#endif
	return true;
}

inline int UdpRecv(UdpSocket socket, char* buffer, int capacity) {
#ifdef _WIN32
	return ::recvfrom(socket, buffer, capacity, 0, nullptr, nullptr);
#else
	return static_cast<int>(::recvfrom(socket, buffer, static_cast<size_t>(capacity), 0, nullptr, nullptr));
#endif
}
