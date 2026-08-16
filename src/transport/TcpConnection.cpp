#include "TcpConnection.h"

#include <Arduino.h>
#include <errno.h>
#include <fcntl.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

namespace clx {

TcpConnection::~TcpConnection() {
    close();
}

Status TcpConnection::connect(const IPAddress &ip, uint16_t port, uint32_t timeoutMs) {
    if (fd_ >= 0) {
        close();
    }
    if (ip == IPAddress(0, 0, 0, 0) || port == 0) {
        return Status::InvalidArg;
    }

    int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        return Status::Error;
    }

    int flags = lwip_fcntl(fd, F_GETFL, 0);
    if (flags < 0 || lwip_fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        lwip_close(fd);
        return Status::Error;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = ip;  // IPAddress converts to network byte order

    int rc = lwip_connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        lwip_close(fd);
        return Status::Error;
    }

    fd_ = fd;
    remoteIp_ = ip;
    remotePort_ = port;
    deadline_ = millis() + timeoutMs;
    state_ = Status::Pending;
    return Status::Pending;
}

Status TcpConnection::poll() {
    if (fd_ < 0) {
        return Status::Closed;
    }
    if (state_ == Status::Ok) {
        return Status::Ok;
    }

    // Detect connect completion by testing writability. SO_ERROR alone is
    // unreliable: it returns the pending error and then clears it, so a second
    // read returns 0 (which looks like success) while the connect is still in
    // progress. select() for writability is the correct completion signal.
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd_, &wfds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    int rc = lwip_select(fd_ + 1, nullptr, &wfds, nullptr, &tv);
    if (rc < 0) {
        close();
        return Status::Error;
    }
    if (rc == 0) {
        // Not writable yet; the connect is still in progress.
        if ((int32_t)(millis() - deadline_) >= 0) {
            close();
            return Status::Timeout;
        }
        return Status::Pending;
    }

    // Writable: the connect finished. Read the result exactly once.
    int error = 0;
    socklen_t errorLen = sizeof(error);
    if (lwip_getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &errorLen) < 0) {
        close();
        return Status::Error;
    }
    if (error == 0) {
        state_ = Status::Ok;
        return Status::Ok;
    }
    close();
    return Status::Error;
}

int TcpConnection::read(uint8_t *buffer, size_t length) {
    if (fd_ < 0 || state_ != Status::Ok) {
        return static_cast<int>(Status::Closed);
    }
    if (buffer == nullptr || length == 0) {
        return static_cast<int>(Status::InvalidArg);
    }
    int n = lwip_recv(fd_, buffer, length, 0);
    if (n > 0) {
        return n;
    }
    if (n == 0) {
        state_ = Status::Closed;  // peer closed
        return static_cast<int>(Status::Closed);
    }
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
        return 0;  // no data yet
    }
    return static_cast<int>(Status::Error);
}

int TcpConnection::write(const uint8_t *buffer, size_t length) {
    if (fd_ < 0 || state_ != Status::Ok) {
        return static_cast<int>(Status::Closed);
    }
    if (buffer == nullptr || length == 0) {
        return static_cast<int>(Status::InvalidArg);
    }
    int n = lwip_send(fd_, buffer, length, 0);
    if (n > 0) {
        return n;
    }
    if (n == 0) {
        return 0;
    }
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
        return 0;  // socket buffer full
    }
    if (errno == EPIPE || errno == ECONNRESET) {
        state_ = Status::Closed;
        return static_cast<int>(Status::Closed);
    }
    return static_cast<int>(Status::Error);
}

void TcpConnection::close() {
    if (fd_ >= 0) {
        lwip_close(fd_);
        fd_ = -1;
    }
    state_ = Status::Closed;
    remoteIp_ = IPAddress(0, 0, 0, 0);
    remotePort_ = 0;
    deadline_ = 0;
}

}  // namespace clx
