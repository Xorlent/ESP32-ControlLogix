#include "Encapsulation.h"

namespace clx {

void putU16(uint8_t *p, uint16_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
}

void putU32(uint8_t *p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}

void putU64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = uint8_t(v >> (8 * i));
    }
}

uint16_t getU16(const uint8_t *p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

uint32_t getU32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

uint64_t getU64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) {
        v = (v << 8) | p[i];
    }
    return v;
}

void encodeHeader(uint8_t *out, const EncapsulationHeader &h) {
    putU16(out, h.command);
    putU16(out + 2, h.length);
    putU32(out + 4, h.session);
    putU32(out + 8, h.status);
    putU64(out + 12, h.context);
    putU32(out + 20, h.options);
}

EncapsulationHeader decodeHeader(const uint8_t *in) {
    EncapsulationHeader h;
    h.command = getU16(in);
    h.length = getU16(in + 2);
    h.session = getU32(in + 4);
    h.status = getU32(in + 8);
    h.context = getU64(in + 12);
    h.options = getU32(in + 20);
    return h;
}

}  // namespace clx
