#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../transport/Status.h"
#include "../eip/Encapsulation.h"
#include "Cip.h"
#include "ExplicitMessage.h"

namespace clx {

class TcpConnection;

/*
 * A connected CIP connection (Forward Open / SendUnitData / Forward Close).
 *
 * open() performs Forward Open and negotiates the O->T / T->O connection IDs.
 * send() performs a connected SendUnitData exchange (e.g. Read/Write Tag over
 * the connection). close() performs Forward Close. poll() advances the state
 * machine. No call blocks.
 *
 * Security/robustness: every received packet is bounds-checked, and connected
 * responses are rejected unless the connection ID matches the negotiated T->O
 * ID and the sequence number matches the request. This rejects stale, replayed,
 * misrouted, and malformed data.
 */
class Connection {
public:
    // Maximum connected CIP payload size (bytes).
    static constexpr size_t kMaxDataSize = 256;

    Connection() = default;
    ~Connection();

    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    // Forward Open for the named symbolic tag.
    Status open(TcpConnection &conn, uint32_t sessionHandle, const char *tagName,
                uint32_t timeoutMs);

    // Send a connected CIP request (service + path + data).
    Status send(TcpConnection &conn, uint32_t sessionHandle, uint8_t service,
                const uint8_t *path, size_t pathLen,
                const uint8_t *data, size_t dataLen, uint32_t timeoutMs);

    // Forward Close.
    Status close(TcpConnection &conn, uint32_t sessionHandle, uint32_t timeoutMs);

    // Advance the state machine.
    Status poll();

    // Connection IDs (valid once open).
    uint32_t originatorConnectionId() const { return otConnId_; }  // O->T
    uint32_t targetConnectionId() const { return toConnId_; }      // T->O

    // True once the connection is open.
    bool isOpen() const { return state_ == State::Open; }

    // Response accessors (valid after a successful send()).
    uint8_t replyService() const { return replyService_; }
    uint8_t resultCode() const { return resultCode_; }
    const uint8_t *data() const { return data_; }
    size_t dataLength() const { return dataLen_; }

private:
    enum class State : uint8_t {
        Idle,
        Opening,
        Open,
        Sending,
        Closing,
        Closed,
        Failed,
    };

    ExplicitMessage fwd_;  // Forward Open/Close (unconnected SendRRData)
    TcpConnection *conn_ = nullptr;
    State state_ = State::Idle;

    uint32_t otConnId_ = 0;   // originator -> target connection ID
    uint32_t toConnId_ = 0;   // target -> originator connection ID
    uint16_t sequence_ = 1;   // connected sequence number
    char tagName_[64] = {};   // tag name (for Forward Close)

    // Connected (SendUnitData) transmit/receive buffers. The connected body
    // overhead is 22 bytes: interface handle (4) + timeout (2) + item count (2)
    // + connected-address item (8) + connected-data item header (4) + sequence (2).
    uint8_t tx_[kEncapsulationHeaderSize + 22 + kMaxDataSize];
    size_t txLen_ = 0;
    size_t txSent_ = 0;
    uint8_t rx_[kEncapsulationHeaderSize + 22 + kMaxDataSize];
    size_t rxLen_ = 0;
    size_t rxExpected_ = 0;

    uint8_t replyService_ = 0;
    uint8_t resultCode_ = 0;
    const uint8_t *data_ = nullptr;
    size_t dataLen_ = 0;

    uint32_t deadline_ = 0;
    uint64_t context_ = 1;
    uint64_t sentContext_ = 0;
    uint16_t sentSequence_ = 0;

    Status startOpen(TcpConnection &conn, uint32_t sessionHandle, const char *tagName,
                     uint32_t timeoutMs);
    Status startSend(TcpConnection &conn, uint32_t sessionHandle, uint8_t service,
                     const uint8_t *path, size_t pathLen,
                     const uint8_t *data, size_t dataLen, uint32_t timeoutMs);
    Status startClose(TcpConnection &conn, uint32_t sessionHandle, uint32_t timeoutMs);
    Status pollOpening();
    Status pollSending();
    Status pollClosing();
    Status writePending();
    Status readResponse();
    Status parseConnectedResponse();
};

}  // namespace clx
