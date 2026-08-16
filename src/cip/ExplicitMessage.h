#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../transport/Status.h"
#include "../eip/Encapsulation.h"

namespace clx {

class TcpConnection;

/*
 * One CIP explicit-message exchange (SendRRData / unconnected messaging) over a
 * TcpConnection. Non-blocking: send() starts the exchange, poll() advances it.
 *
 * The response CIP data pointer returned by data() is valid until the next
 * send() call (it points into this object's receive buffer).
 */
class ExplicitMessage {
public:
    // Maximum CIP request/response payload (service + path + data).
    static constexpr size_t kMaxCipData = 256;

    ExplicitMessage() = default;
    ~ExplicitMessage();

    ExplicitMessage(const ExplicitMessage &) = delete;
    ExplicitMessage &operator=(const ExplicitMessage &) = delete;

    // Start a SendRRData exchange. conn must be connected; sessionHandle is the
    // registered session handle. path is the encoded CIP path (segment bytes,
    // whole 16-bit words); data is optional service-specific data appended
    // after the path (e.g. an attribute segment).
    Status send(TcpConnection &conn, uint32_t sessionHandle, uint8_t service,
                const uint8_t *path, size_t pathLen,
                const uint8_t *data, size_t dataLen, uint32_t timeoutMs);

    // Advance the exchange. Returns Pending while in flight, Ok once the
    // response is received and framed, or an error Status.
    Status poll();

    // Abort an in-flight exchange, returning to Idle.
    void abort();

    // Response accessors (valid once poll() returns Ok).
    uint8_t replyService() const { return replyService_; }
    uint8_t resultCode() const { return resultCode_; }
    const uint8_t *data() const { return data_; }
    size_t dataLength() const { return dataLen_; }

private:
    enum class State : uint8_t {
        Idle,
        Sending,
        Receiving,
        Done,
        Failed,
    };

    TcpConnection *conn_ = nullptr;
    State state_ = State::Idle;
    uint32_t deadline_ = 0;
    uint64_t context_ = 1;
    uint64_t sentContext_ = 0;

    // Transmit buffer: header + body (interface handle/timeout/items + CIP).
    uint8_t tx_[kEncapsulationHeaderSize + 16 + kMaxCipData];
    size_t txLen_ = 0;
    size_t txSent_ = 0;

    // Receive buffer: header + body.
    uint8_t rx_[kEncapsulationHeaderSize + 16 + kMaxCipData];
    size_t rxLen_ = 0;
    size_t rxExpected_ = 0;

    // Parsed response.
    uint8_t replyService_ = 0;
    uint8_t resultCode_ = 0;
    const uint8_t *data_ = nullptr;
    size_t dataLen_ = 0;

    Status writePending();
    Status readResponse();
    Status parseResponse();
};

}  // namespace clx
