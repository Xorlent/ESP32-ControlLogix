#pragma once

#include <stdint.h>

namespace clx {

/*
 * Result status for non-blocking transport operations.
 *
 * Convention: Pending (0) and Ok (1) are the progress/success path; Busy and
 * WouldBlock are informational; every failure value is negative. This matters
 * for TcpConnection::read()/write(), which return a positive byte count, 0 for
 * "would block", or a negative Status on error/close - so error values must be
 * negative to stay distinguishable from byte counts.
 */
enum class Status : int8_t {
    Pending = 0,     // operation in progress; call poll() again
    Ok = 1,          // operation completed successfully
    Busy = 2,        // resource already active (e.g. already connected)
    WouldBlock = 3,  // non-blocking I/O would block; retry later
    Closed = -1,     // connection closed / not connected
    Timeout = -2,    // deadline exceeded
    NotReady = -3,   // transport not ready (no link / no IP)
    NoMemory = -4,   // allocation failure
    InvalidArg = -5, // invalid argument
    Error = -6,      // generic / unspecified error
};

// Human-readable name for a Status value (never null).
inline const char *statusString(Status s) {
    switch (s) {
        case Status::Pending:    return "pending";
        case Status::Ok:         return "ok";
        case Status::Busy:       return "busy";
        case Status::WouldBlock: return "would-block";
        case Status::Closed:     return "closed";
        case Status::Timeout:    return "timeout";
        case Status::NotReady:   return "not-ready";
        case Status::NoMemory:   return "no-memory";
        case Status::InvalidArg: return "invalid-argument";
        case Status::Error:      return "error";
    }
    return "unknown";
}

}  // namespace clx
