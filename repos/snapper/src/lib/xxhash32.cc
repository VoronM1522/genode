#include "xxhash32.h"

Genode::uint32_t xxhash32(const void* input, Genode::uint64_t length, Genode::uint32_t seed) {
    XXHash32 _hash(seed);
    _hash.add(input, length);
    return _hash.hash();
}
