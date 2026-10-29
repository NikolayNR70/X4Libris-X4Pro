#include <cstdint>

extern "C" uint32_t uzlib_adler32(const void*, unsigned int, uint32_t previous) { return previous; }
extern "C" uint32_t uzlib_crc32(const void*, unsigned int, uint32_t previous) { return previous; }
