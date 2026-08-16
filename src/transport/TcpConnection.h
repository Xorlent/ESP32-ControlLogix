#pragma once

#include <stddef.h>
#include <stdint.h>
#include <IPAddress.h>
#include "Status.h"

namespace clx {

/*
 * Non-blocking TCP connection over lwIP sockets.
 *
 * connect() starts a non-blocking connect and returns immediately; poll()
 * advances the connect state and checks the connect deadline. read()/write()
 * use non-blocking socket operations and return available progress without
 * waiting. close() releases the socket. No call blocks.
 */
class TcpConnection {
public:
    TcpConnection() = default;
    ~TcpConnection();

    TcpConnection(const TcpConnection &) = delete;
    TcpConnection &operator=(const TcpConnection &) = delete;

    // Begin a non-blocking connect. Returns Pending while the connect is in
    // progress, or an error Status immediately. timeoutMs sets the connect
    // deadline checked by poll().
    Status connect(const IPAddress &ip, uint16_t port, uint32_t timeoutMs);

    // Advance connection state. Returns Pending while connecting, Ok once
    // connected, or Timeout/Error on failure.
    Status poll();

    // Non-blocking read. Returns the number of bytes read (>0), 0 if no data
    // is available yet (retry later), or a negative Status (Closed/Error/...).
    int read(uint8_t *buffer, size_t length);

    // Non-blocking write. Returns the number of bytes written (>0), 0 if the
    // socket buffer is full (retry later), or a negative Status (Closed/Error).
    int write(const uint8_t *buffer, size_t length);

    // Close the connection and release the socket.
    void close();

    // True once the connection is established.
    bool connected() const { return state_ == Status::Ok; }

    // Raw socket descriptor (-1 when closed). For advanced use only.
    int fd() const { return fd_; }

private:
    int fd_ = -1;
    IPAddress remoteIp_;
    uint16_t remotePort_ = 0;
    uint32_t deadline_ = 0;
    Status state_ = Status::Closed;
};

}  // namespace clx
