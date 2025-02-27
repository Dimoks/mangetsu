#pragma once

#include <cstdint>

namespace mg {

static inline uint16_t le_to_host_u16(uint16_t v) { return v; }
static inline uint32_t le_to_host_u32(uint32_t v) { return v; }
static inline uint64_t le_to_host_u64(uint64_t v) { return v; }
static inline uint16_t host_to_le_u16(uint16_t v) { return v; }
static inline uint32_t host_to_le_u32(uint32_t v) { return v; }
static inline uint64_t host_to_le_u64(uint64_t v) { return v; }

static inline uint16_t be_to_host_u16(uint16_t v) {
#if defined(_WIN32)
  return _byteswap_ushort(v);
#else
  return __builtin_bswap16(v);
#endif
}
static inline uint32_t be_to_host_u32(uint32_t v) {
#if defined(_WIN32)
  return _byteswap_ulong(v);
#else
  return __builtin_bswap32(v);
#endif
}
static inline uint64_t be_to_host_u64(uint64_t v) {
#if defined(_WIN32)
  return _byteswap_uint64(v);
#else
  return __builtin_bswap64(v);
#endif
}
static inline uint16_t host_to_be_u16(uint16_t v) { return be_to_host_u16(v); }
static inline uint32_t host_to_be_u32(uint32_t v) { return be_to_host_u32(v); }
static inline uint64_t host_to_be_u64(uint64_t v) { return be_to_host_u64(v); }

} // namespace mg
