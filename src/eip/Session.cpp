#include "Session.h"

#include <Arduino.h>

#include "../transport/TcpConnection.h"

namespace clx {

Session::~Session() {
    abort();
}

Status Session::open(TcpConnection &conn, uint32_t timeoutMs) {
    if (state_ == State::Registering || state_ == State::Registered) {
        return Status::Busy;
    }
    if (!conn.connected()) {
        return Status::NotReady;
    }
    conn_ = &conn;
    deadline_ = millis() + timeoutMs;
    return startRegister();
}

Status Session::close() {
    if (state_ == State::Registered) {
        return startUnregister();
    }
    if (state_ == State::Registering || state_ == State::Unregistering) {
        return Status::Busy;
    }
    abort();
    return Status::Ok;
}

void Session::abort() {
    conn_ = nullptr;
    state_ = State::Closed;
    handle_ = 0;
    txLen_ = 0;
    txSent_ = 0;
    rxLen_ = 0;
    rxExpected_ = 0;
}

Status Session::startRegister() {
    // RegisterSession request: header + 4-byte body (protocol version=1,
    // options flags=0).
    EncapsulationHeader h;
    h.command = static_cast<uint16_t>(Command::RegisterSession);
    h.length = 4;
    h.session = 0;
    h.status = 0;
    h.context = context_;
    h.options = 0;

    encodeHeader(tx_, h);
    putU16(tx_ + kEncapsulationHeaderSize, 1);      // protocol version
    putU16(tx_ + kEncapsulationHeaderSize + 2, 0);  // options flags

    txLen_ = kEncapsulationHeaderSize + 4;
    txSent_ = 0;
    rxLen_ = 0;
    rxExpected_ = kEncapsulationHeaderSize;
    state_ = State::Registering;
    return Status::Pending;
}

Status Session::startUnregister() {
    // UnregisterSession request: header only (no body).
    EncapsulationHeader h;
    h.command = static_cast<uint16_t>(Command::UnregisterSession);
    h.length = 0;
    h.session = handle_;
    h.status = 0;
    h.context = context_;
    h.options = 0;

    encodeHeader(tx_, h);
    txLen_ = kEncapsulationHeaderSize;
    txSent_ = 0;
    rxLen_ = 0;
    rxExpected_ = kEncapsulationHeaderSize;
    state_ = State::Unregistering;
    return Status::Pending;
}

Status Session::poll() {
    switch (state_) {
        case State::Registering:   return pollRegistering();
        case State::Unregistering: return pollUnregistering();
        case State::Registered:    return Status::Ok;
        case State::Closed:        return Status::Closed;
        case State::Failed:        return Status::Error;
        case State::Idle:          return Status::NotReady;
    }
    return Status::Error;
}

Status Session::pollRegistering() {
    if ((int32_t)(millis() - deadline_) >= 0) {
        state_ = State::Failed;
        return Status::Timeout;
    }

    Status st = writePending();
    if (st != Status::Ok) {
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

    // Validate the RegisterSession response.
    EncapsulationHeader h = decodeHeader(rx_);
    if (h.command != static_cast<uint16_t>(Command::RegisterSession) ||
        h.status != 0 || h.length < 4) {
        state_ = State::Failed;
        return Status::Error;
    }
    handle_ = h.session;
    if (handle_ == 0) {
        state_ = State::Failed;
        return Status::Error;
    }

    state_ = State::Registered;
    return Status::Ok;
}

Status Session::pollUnregistering() {
    if ((int32_t)(millis() - deadline_) >= 0) {
        state_ = State::Failed;
        return Status::Timeout;
    }

    Status st = writePending();
    if (st != Status::Ok) {
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

    // UnregisterSession response has no body; contents are not meaningful.
    state_ = State::Closed;
    return Status::Ok;
}

Status Session::writePending() {
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

Status Session::readResponse() {
    while (rxLen_ < rxExpected_) {
        int n = conn_->read(rx_ + rxLen_, rxExpected_ - rxLen_);
        if (n > 0) {
            rxLen_ += size_t(n);
            // Once the header is complete, determine the body length.
            if (rxLen_ == kEncapsulationHeaderSize) {
                EncapsulationHeader h = decodeHeader(rx_);
                if (h.length > kMaxBodySize) {
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

}  // namespace clx
