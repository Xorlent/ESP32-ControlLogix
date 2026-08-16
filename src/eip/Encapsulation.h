#pragma once

#include <stddef.h>
#include <stdint.h>

namespace clx {

/*
 * EtherNet/IP encapsulation layer (CIP Vol 2, Chapter 2).
 *
 * Every EtherNet/IP message is a 24-byte encapsulation header followed by an
 * optional body. All multi-byte fields are little-endian.
 */

// EtherNet/IP encapsulation command codes.
enum class Command : uint16_t {
    Nop = 0x0000,
    ListServices = 0x0004,
    ListIdentity = 0x0063,
    ListInterfaces = 0x0064,
    RegisterSession = 0x0065,
    UnregisterSession = 0x0066,
    SendRRData = 0x006F,
    SendUnitData = 0x0070,
    IndicateStatus = 0x0072,
    Cancel = 0x0073,
};

constexpr size_t kEncapsulationHeaderSize = 24;

// The 24-byte encapsulation header (all fields little-endian).
struct EncapsulationHeader {
    uint16_t command = 0;  // Command code
    uint16_t length = 0;   // Bytes following the header
    uint32_t session = 0;  // Session handle (0 on RegisterSession request)
    uint32_t status = 0;   // 0 = success
    uint64_t context = 0;  // Sender context, echoed back
    uint32_t options = 0;  // Options flags
};

// Encode a header into out (must be >= kEncapsulationHeaderSize bytes).
void encodeHeader(uint8_t *out, const EncapsulationHeader &h);

// Decode a header from in (must be >= kEncapsulationHeaderSize bytes).
EncapsulationHeader decodeHeader(const uint8_t *in);

// Little-endian encode/decode helpers (shared by header and body codecs).
void putU16(uint8_t *p, uint16_t v);
void putU32(uint8_t *p, uint32_t v);
void putU64(uint8_t *p, uint64_t v);
uint16_t getU16(const uint8_t *p);
uint32_t getU32(const uint8_t *p);
uint64_t getU64(const uint8_t *p);

}  // namespace clx
