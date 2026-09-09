// Small socket portability layer shared by the production server.
//
// Keeping the native handle and error conventions here avoids leaking POSIX
// file-descriptor assumptions into the protocol/session code.  The public
// server therefore builds with Winsock, BSD sockets, or Linux sockets while
// retaining one implementation of the Minecraft wire state machine.
#pragma once

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
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
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace cppfm::platform {

#ifdef _WIN32
using socket_t = SOCKET;
using socket_length_t = int;
using io_count_t = int;
inline constexpr socket_t invalid_socket = INVALID_SOCKET;
#else
using socket_t = int;
using socket_length_t = socklen_t;
using io_count_t = ssize_t;
inline constexpr socket_t invalid_socket = -1;
#endif

inline bool isValid(socket_t socket) noexcept {
    return socket != invalid_socket;
}

inline bool initializeSockets() noexcept {
#ifdef _WIN32
    static std::once_flag once;
    static bool initialized = false;
    std::call_once(once, [] {
        WSADATA data{};
        initialized = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    });
    return initialized;
#else
    return true;
#endif
}

inline int lastSocketError() noexcept {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

inline bool isInterrupted(int error) noexcept {
#ifdef _WIN32
    return error == WSAEINTR;
#else
    return error == EINTR;
#endif
}

inline bool isWouldBlock(int error) noexcept {
#ifdef _WIN32
    // SO_RCVTIMEO/SO_SNDTIMEO may report WSAETIMEDOUT rather than
    // WSAEWOULDBLOCK on Winsock.  Connection treats all three as a bounded
    // retry/timeout outcome, matching the POSIX path.
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS ||
           error == WSAETIMEDOUT;
#else
    return error == EAGAIN || error == EWOULDBLOCK || error == ETIMEDOUT;
#endif
}

inline std::string socketErrorText(int error) {
#ifdef _WIN32
    return "socket error " + std::to_string(error);
#else
    return std::strerror(error);
#endif
}

inline std::uint64_t socketNumber(socket_t socket) noexcept {
    return static_cast<std::uint64_t>(socket);
}

inline socket_t createTcpSocket() noexcept {
    return ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

inline int bindSocket(socket_t socket, const sockaddr* address,
                      socket_length_t length) noexcept {
    return ::bind(socket, address, length);
}

inline int listenSocket(socket_t socket, int backlog) noexcept {
    return ::listen(socket, backlog);
}

inline socket_t acceptSocket(socket_t socket, sockaddr* address,
                             socket_length_t* length) noexcept {
    return ::accept(socket, address, length);
}

inline int closeSocket(socket_t socket) noexcept {
#ifdef _WIN32
    return ::closesocket(socket);
#else
    return ::close(socket);
#endif
}

inline int shutdownSocket(socket_t socket) noexcept {
#ifdef _WIN32
    return ::shutdown(socket, SD_BOTH);
#else
    return ::shutdown(socket, SHUT_RDWR);
#endif
}

inline int setSocketOption(socket_t socket, int level, int option,
                           const void* value, socket_length_t length) noexcept {
#ifdef _WIN32
    return ::setsockopt(socket, level, option,
                        static_cast<const char*>(value), length);
#else
    return ::setsockopt(socket, level, option, value, length);
#endif
}

inline bool setSocketTimeout(socket_t socket, int option,
                             unsigned seconds) noexcept {
#ifdef _WIN32
    const auto milliseconds = static_cast<DWORD>(std::min<unsigned long long>(
        static_cast<unsigned long long>(seconds) * 1000ULL,
        static_cast<unsigned long long>(std::numeric_limits<DWORD>::max())));
    return setSocketOption(socket, SOL_SOCKET, option, &milliseconds,
                           static_cast<socket_length_t>(sizeof(milliseconds))) == 0;
#else
    timeval timeout{};
    timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(seconds);
    return setSocketOption(socket, SOL_SOCKET, option, &timeout,
                           static_cast<socket_length_t>(sizeof(timeout))) == 0;
#endif
}

inline int waitReadable(socket_t socket, int timeoutMs) noexcept {
#ifdef _WIN32
    WSAPOLLFD descriptor{};
    descriptor.fd = socket;
    descriptor.events = POLLRDNORM;
    return WSAPoll(&descriptor, 1, timeoutMs);
#else
    pollfd descriptor{};
    descriptor.fd = socket;
    descriptor.events = POLLIN;
    return ::poll(&descriptor, 1, timeoutMs);
#endif
}

inline io_count_t receive(socket_t socket, void* buffer, std::size_t size,
                          int flags = 0) noexcept {
    const auto count = static_cast<int>(std::min<std::size_t>(size, INT_MAX));
#ifdef _WIN32
    return ::recv(socket, static_cast<char*>(buffer), count, flags);
#else
    return ::recv(socket, buffer, static_cast<std::size_t>(count), flags);
#endif
}

inline io_count_t send(socket_t socket, const void* buffer, std::size_t size,
                       int flags = 0) noexcept {
    const auto count = static_cast<int>(std::min<std::size_t>(size, INT_MAX));
#ifdef _WIN32
    return ::send(socket, static_cast<const char*>(buffer), count, flags);
#else
    return ::send(socket, buffer, static_cast<std::size_t>(count), flags);
#endif
}

inline int sendFlags() noexcept {
#ifdef _WIN32
    return 0;
#else
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
#endif
}

inline int peerName(socket_t socket, sockaddr* address,
                    socket_length_t* length) noexcept {
    return ::getpeername(socket, address, length);
}

} // namespace cppfm::platform
