#ifndef YMIR_UTIL_DATA_OPS_HLSLI
#define YMIR_UTIL_DATA_OPS_HLSLI

#include "bit_ops.hlsli"

uint Read4(ByteAddressBuffer buf, uint address, uint nibble) {
    return BitExtract(buf.Load(address & ~3), (address & 3) * 8 + nibble * 4, 4);
}

uint Read8(ByteAddressBuffer buf, uint address) {
    return BitExtract(buf.Load(address & ~3), (address & 3) * 8, 8);
}

uint Read16(ByteAddressBuffer buf, uint address) {
    return ByteSwap16(BitExtract(buf.Load(address & ~3), (address & 2) * 8, 16));
}

uint Read32(ByteAddressBuffer buf, uint address) {
    return ByteSwap32(buf.Load(address & ~3));
}

#endif
