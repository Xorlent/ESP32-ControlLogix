#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../transport/Status.h"
#include "Encapsulation.h"

namespace clx {

class TcpConnection;

/*
 * EtherNet/IP encapsulation session over a TcpConnection.
 *
 * open() sends RegisterSession and returns immediately; poll() advances the
 * exchange using non-blocking I/O and bounded receive framing. close() sends
 * UnregisterSession. No call blocks; timeouts are deadlines checked from
 * poll().
 */
class Session {
public:
    // Maximum accepted encapsulation body size (bounded receive framing).
    static constexpr size_t kMaxBodySize = 512;

    Session() = default;
    ~Session();

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    // Begin RegisterSession. conn must already be connected. Returns Pending
    // while the exchange is in flight, or an error Status immediately.
    Status open(TcpConnection &conn, uint32_t timeoutMs);

    // Advance the session state machine. Returns Pending while the current
    // exchange is in flight, Ok once registered/closed, or an error Status.
    Status poll();

    // Begin a graceful UnregisterSession. Returns Pending while the exchange
    // is in flight, Busy if an exchange is already active, or Ok if aborted.
    Status close();

    // Immediately drop the session without UnregisterSession.
    void abort();

    // Session handle (valid once registered()).
    uint32_t handle() const { return handle_; }

    // True once the session is registered.
    bool registered() const { return state_ == State::Registered; }

private:
    enum class State : uint8_t {
        Idle,
        Registering,
        Registered,
        Unregistering,
        Closed,
        Failed,
    };

    TcpConnection *conn_ = nullptr;
    State state_ = State::Idle;
    uint32_t handle_ = 0;
    uint32_t deadline_ = 0;
    uint64_t context_ = 1;

    // Transmit buffer (header + body) for the in-flight request.
    uint8_t tx_[kEncapsulationHeaderSize + kMaxBodySize];
    size_t txLen_ = 0;
    size_t txSent_ = 0;

    // Receive buffer (header + body) for the in-flight response.
    uint8_t rx_[kEncapsulationHeaderSize + kMaxBodySize];
    size_t rxLen_ = 0;       // bytes received so far
    size_t rxExpected_ = 0;  // total expected once the header is parsed

    Status startRegister();
    Status startUnregister();
    Status pollRegistering();
    Status pollUnregistering();
    Status writePending();
    Status readResponse();
};

}  // namespace clx
