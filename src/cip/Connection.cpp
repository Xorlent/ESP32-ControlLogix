#include "Connection.h"

#include <Arduino.h>
#include <esp_random.h>
#include <string.h>

#include "../transport/TcpConnection.h"

namespace clx {

Connection::~Connection() {
    state_ = State::Idle;
}

Status Connection::open(TcpConnection &conn, uint32_t sessionHandle, const char *tagName,
                       uint32_t timeoutMs) {
    if (tagName == nullptr || strlen(tagName) >= sizeof(tagName_)) {
        return Status::InvalidArg;
    }
    if (state_ == State::Opening || state_ == State::Sending || state_ == State::Closing) {
        return Status::Busy;
    }
    return startOpen(conn, sessionHandle, tagName, timeoutMs);
}

Status Connection::send(TcpConnection &conn, uint32_t sessionHandle, uint8_t service,
                        const uint8_t *path, size_t pathLen,
                        const uint8_t *data, size_t dataLen, uint32_t timeoutMs) {
    if (state_ != State::Open) {
        return Status::NotReady;
    }
    return startSend(conn, sessionHandle, service, path, pathLen, data, dataLen, timeoutMs);
}

Status Connection::close(TcpConnection &conn, uint32_t sessionHandle, uint32_t timeoutMs) {
    if (state_ != State::Open) {
        return Status::NotReady;
    }
    return startClose(conn, sessionHandle, timeoutMs);
}

Status Connection::startOpen(TcpConnection &conn, uint32_t sessionHandle, const char *tagName,
                             uint32_t timeoutMs) {
    // Choose a random originator->target connection ID (non-zero).
    otConnId_ = esp_random();
    if (otConnId_ == 0) {
        otConnId_ = 1;
    }
    toConnId_ = 0;
    strncpy(tagName_, tagName, sizeof(tagName_) - 1);
    tagName_[sizeof(tagName_) - 1] = 0;

    // Request path to the Connection Manager (class 6, instance 1).
    const uint8_t reqPath[4] = {0x20, 0x06, 0x24, 0x01};

    // Forward Open connection parameters + connection path.
    uint8_t data[128];
    size_t d = 0;
    data[d++] = 0x0A;                          // priority/tick time
    data[d++] = 0x0E;                          // timeout ticks
    putU32(data + d, otConnId_); d += 4;       // O->T connection ID
    putU32(data + d, 0); d += 4;               // T->O connection ID (0)
    putU16(data + d, 1); d += 2;               // connection serial number
    putU16(data + d, 0x0001); d += 2;          // originator vendor ID
    putU32(data + d, 0x00000001); d += 4;      // originator serial number
    data[d++] = 0x03;                          // connection timeout multiplier
    data[d++] = 0; data[d++] = 0; data[d++] = 0;  // reserved (3)
    putU32(data + d, 10000); d += 4;           // O->T RPI (us)
    putU16(data + d, 0x4200); d += 2;          // O->T network params
    putU32(data + d, 10000); d += 4;           // T->O RPI (us)
    putU16(data + d, 0x4200); d += 2;          // T->O network params
    data[d++] = 0xA3;                          // transport type/trigger (class 3)

    // Connection path (symbolic tag). Use the truncated tagName_ (not the
    // caller's tagName) so a long name cannot overflow the data buffer.
    size_t pathLen = appendSymbolic(data + d + 1, tagName_);
    data[d] = uint8_t(pathLen / 2);            // connection path size (words)
    d += 1 + pathLen;

    Status st = fwd_.send(conn, sessionHandle, 0x54, reqPath, sizeof(reqPath), data, d, timeoutMs);
    if (st != Status::Pending) {
        return st;
    }
    state_ = State::Opening;
    return Status::Pending;
}

Status Connection::startClose(TcpConnection &conn, uint32_t sessionHandle, uint32_t timeoutMs) {
    const uint8_t reqPath[4] = {0x20, 0x06, 0x24, 0x01};

    uint8_t data[128];
    size_t pathLen = appendSymbolic(data + 1, tagName_);
    data[0] = uint8_t(pathLen / 2);  // connection path size (words)
    size_t d = 1 + pathLen;

    // Forward Close also carries the connection identity (matching Forward
    // Open) so the target can identify the connection being closed.
    putU16(data + d, 1); d += 2;               // connection serial number
    putU16(data + d, 0x0001); d += 2;          // originator vendor ID
    putU32(data + d, 0x00000001); d += 4;      // originator serial number

    Status st = fwd_.send(conn, sessionHandle, 0x4E, reqPath, sizeof(reqPath), data, d, timeoutMs);
    if (st != Status::Pending) {
        return st;
    }
    state_ = State::Closing;
    return Status::Pending;
}

Status Connection::startSend(TcpConnection &conn, uint32_t sessionHandle, uint8_t service,
                             const uint8_t *path, size_t pathLen,
                             const uint8_t *data, size_t dataLen, uint32_t timeoutMs) {
    const size_t cipLen = 2 + pathLen + dataLen;
    if (cipLen > kMaxDataSize || (pathLen & 1) != 0 ||
        (pathLen > 0 && path == nullptr) || (dataLen > 0 && data == nullptr)) {
        return Status::InvalidArg;
    }
    conn_ = &conn;

    // Encapsulation header (SendUnitData).
    EncapsulationHeader h;
    h.command = static_cast<uint16_t>(Command::SendUnitData);
    h.length = uint16_t(22 + cipLen);
    h.session = sessionHandle;
    h.status = 0;
    h.context = context_;
    h.options = 0;
    encodeHeader(tx_, h);

    // Body: interface handle, timeout, item count, connected-address item,
    // connected-data item.
    uint8_t *body = tx_ + kEncapsulationHeaderSize;
    putU32(body, 0);                          // interface handle
    putU16(body + 4, 0);                      // timeout
    putU16(body + 6, 2);                      // CPF item count = 2
    putU16(body + 8, 0x00A1);                 // item 1: connected address type
    putU16(body + 10, 4);                     // item 1: length 4
    putU32(body + 12, otConnId_);             // item 1: O->T connection ID
    putU16(body + 16, 0x00B1);                // item 2: connected data type
    putU16(body + 18, uint16_t(2 + cipLen));  // item 2: length (sequence + CIP)
    putU16(body + 20, sequence_);             // item 2: sequence number

    // CIP request payload.
    uint8_t *cip = body + 22;
    cip[0] = service;
    cip[1] = uint8_t(pathLen / 2);
    if (pathLen) {
        memcpy(cip + 2, path, pathLen);
    }
    if (dataLen) {
        memcpy(cip + 2 + pathLen, data, dataLen);
    }

    txLen_ = kEncapsulationHeaderSize + 22 + cipLen;
    txSent_ = 0;
    rxLen_ = 0;
    rxExpected_ = kEncapsulationHeaderSize;
    sentContext_ = context_;
    sentSequence_ = sequence_;
    ++context_;
    ++sequence_;
    deadline_ = millis() + timeoutMs;
    state_ = State::Sending;
    return Status::Pending;
}

Status Connection::poll() {
    switch (state_) {
        case State::Opening: return pollOpening();
        case State::Sending: return pollSending();
        case State::Closing: return pollClosing();
        case State::Open:    return Status::Ok;
        case State::Closed:  return Status::Closed;
        case State::Failed:  return Status::Error;
        case State::Idle:    return Status::NotReady;
    }
    return Status::Error;
}

Status Connection::pollOpening() {
    Status st = fwd_.poll();
    if (st == Status::Pending) {
        return Status::Pending;
    }
    if (st != Status::Ok) {
        state_ = State::Failed;
        return st;
    }
    if (fwd_.resultCode() != 0 || fwd_.dataLength() < 12) {
        state_ = State::Failed;
        return Status::Error;
    }
    // Forward Open response: O->T conn ID (4), T->O conn ID (4), serial (2), vendor (2).
    uint32_t ot = getU32(fwd_.data());
    uint32_t to = getU32(fwd_.data() + 4);
    if (ot == 0 || to == 0) {
        state_ = State::Failed;
        return Status::Error;
    }
    // The target echoes our O->T ID and supplies the T->O ID.
    if (ot != otConnId_) {
        state_ = State::Failed;
        return Status::Error;  // mismatched O->T connection ID
    }
    toConnId_ = to;
    state_ = State::Open;
    return Status::Ok;
}

Status Connection::pollSending() {
    if ((int32_t)(millis() - deadline_) >= 0) {
        state_ = State::Failed;
        return Status::Timeout;
    }
    Status st = writePending();
    if (st != Status::Ok) {
        if (st != Status::Pending) {
            state_ = State::Failed;
        }
        return st;
    }
    st = readResponse();
    if (st == Status::Pending) {
        return Status::Pending;
    }
    if (st != Status::Ok) {
        state_ = State::Failed;
        return st;
    }
    st = parseConnectedResponse();
    state_ = (st == Status::Ok) ? State::Open : State::Failed;
    return st;
}

Status Connection::pollClosing() {
    Status st = fwd_.poll();
    if (st == Status::Pending) {
        return Status::Pending;
    }
    if (st != Status::Ok) {
        state_ = State::Failed;
        return st;
    }
    state_ = State::Closed;
    return Status::Ok;
}

Status Connection::writePending() {
    while (txSent_ < txLen_) {
        int n = conn_->write(tx_ + txSent_, txLen_ - txSent_);
        if (n > 0) {
            txSent_ += size_t(n);
            continue;
        }
        if (n == 0) {
            return Status::Pending;  // socket buffer full; retry on next poll
        }
        return static_cast<Status>(n);  // Closed/Error
    }
    return Status::Ok;
}

Status Connection::readResponse() {
    while (rxLen_ < rxExpected_) {
        int n = conn_->read(rx_ + rxLen_, rxExpected_ - rxLen_);
        if (n > 0) {
            rxLen_ += size_t(n);
            if (rxLen_ == kEncapsulationHeaderSize) {
                EncapsulationHeader h = decodeHeader(rx_);
                if (h.length > 22 + kMaxDataSize) {
                    return Status::Error;  // oversized / malformed packet
                }
                rxExpected_ = kEncapsulationHeaderSize + h.length;
            }
            continue;
        }
        if (n == 0) {
            return Status::Pending;  // no data yet; retry on next poll
        }
        return static_cast<Status>(n);  // Closed/Error
    }
    return Status::Ok;
}

Status Connection::parseConnectedResponse() {
    // Validate the encapsulation header.
    EncapsulationHeader h = decodeHeader(rx_);
    if (h.command != static_cast<uint16_t>(Command::SendUnitData) || h.status != 0) {
        return Status::Error;
    }
    if (h.context != sentContext_) {
        return Status::Error;  // stale / mismatched response
    }

    const uint8_t *body = rx_ + kEncapsulationHeaderSize;
    size_t bodyLen = rxLen_ - kEncapsulationHeaderSize;
    if (bodyLen < 8) {  // interface handle 4 + timeout 2 + item count 2
        return Status::Error;
    }

    uint16_t itemCount = getU16(body + 6);
    const uint8_t *p = body + 8;
    size_t remaining = bodyLen - 8;

    // Parse CPF items; locate the connected-address and connected-data items.
    uint32_t connId = 0;
    bool haveConnId = false;
    const uint8_t *connData = nullptr;
    size_t connDataLen = 0;
    for (uint16_t i = 0; i < itemCount; ++i) {
        if (remaining < 4) {
            return Status::Error;
        }
        uint16_t type = getU16(p);
        uint16_t len = getU16(p + 2);
        p += 4;
        remaining -= 4;
        if (len > remaining) {
            return Status::Error;
        }
        if (type == 0x00A1) {  // connected address item
            if (len < 4) {
                return Status::Error;
            }
            connId = getU32(p);
            haveConnId = true;
        } else if (type == 0x00B1) {  // connected data item
            connData = p;
            connDataLen = len;
        }
        p += len;
        remaining -= len;
    }

    if (!haveConnId || connData == nullptr || connDataLen < 4) {
        return Status::Error;
    }

    // Validate the connection ID (must be the negotiated T->O ID). Rejects
    // data addressed to a different connection (misrouted/replayed).
    if (connId != toConnId_) {
        return Status::Error;
    }

    // Validate the sequence number (must match the request). Rejects stale or
    // replayed responses.
    uint16_t seq = getU16(connData);
    if (seq != sentSequence_) {
        return Status::Error;
    }

    // Parse the CIP response: reply service, reserved, general status, ext status size.
    const uint8_t *cip = connData + 2;
    size_t cipLen = connDataLen - 2;
    if (cipLen < 4) {
        return Status::Error;
    }
    replyService_ = cip[0];
    resultCode_ = cip[2];
    data_ = cip + 4;
    dataLen_ = cipLen - 4;

    return Status::Ok;
}

}  // namespace clx


