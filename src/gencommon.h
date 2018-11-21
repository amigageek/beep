#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define CHECK(x)                                \
  if (! (x)) {                                  \
    fprintf(stderr, "assert(%s) failed\n", #x); \
    ret = false;                                \
    goto cleanup;                               \
  }

#define BE16_TO_LE(x) (                         \
    (((x) & 0x00FF) << 0x8) |                   \
    (((x) & 0xFF00) >> 0x8)                     \
  )

#define LE16_TO_BE(x) BE16_TO_LE(x)

#define BE32_TO_LE(x) (                         \
    (((x) & 0x000000FF) << 0x18) |              \
    (((x) & 0x0000FF00) <<  0x8) |              \
    (((x) & 0x00FF0000) >>  0x8) |              \
    (((x) & 0xFF000000) >> 0x18)                \
  )

bool load_ilbm(const char* path,
               uint8_t** out_bpls,
               uint32_t* out_bpls_size_b);
