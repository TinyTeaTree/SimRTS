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
#include <netinet/in.h>
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

#ifdef _WIN32
	DWORD timeout_ms = 50;
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
	timeval timeout{};
	timeout.tv_sec = 0;
	timeout.tv_usec = 50000;
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
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
