#include "gencommon.h"

#include <stdlib.h>
#include <string.h>

#define IFF_CHUNK_ID(a, b, c, d) (((a) << 0x18) | ((b) << 0x10) | ((c) << 0x8) | (d))

typedef struct {
  uint16_t bmh_Width;
  uint16_t bmh_Height;
  int16_t  bmh_Left;
  int16_t  bmh_Top;
  uint8_t  bmh_Depth;
  uint8_t  bmh_Masking;
  uint8_t  bmh_Compression;
  uint8_t  bmh_Pad;
  uint16_t bmh_Transparent;
  uint8_t  bmh_XAspect;
  uint8_t  bmh_YAspect;
  int16_t  bmh_PageWidth;
  int16_t  bmh_PageHeight;
} BitMapHeader;

bool load_ilbm(const char* path,
               uint8_t** out_bpls,
               uint32_t* out_bpls_size_b) {
  bool ret = true;
  FILE* iff_file = NULL;
  uint8_t* iff_data = NULL;
  BitMapHeader* bmh = NULL;
  uint8_t* bpls = NULL;

  // Read the IFF/ILBM glyph image into memory.
  long iff_size = 0;

  CHECK(iff_file = fopen(path, "rb"));
  CHECK(fseek(iff_file, 0, SEEK_END) == 0);
  CHECK((iff_size = ftell(iff_file)) != -1);
  CHECK(fseek(iff_file, 0, SEEK_SET) == 0);

  CHECK(iff_data = malloc(iff_size));
  CHECK(fread(iff_data, 1, iff_size, iff_file) == iff_size);

  // Walk through each chunk in the IFF data.
  uint32_t bpls_stride_b = 0;
  uint32_t bpls_size_b = 0;

  for (uint32_t iff_offset = 0; iff_offset < iff_size; ) {
    CHECK(iff_offset + 8 <= iff_size);

    uint32_t chunk_id = BE32_TO_LE(*(uint32_t*)(iff_data + iff_offset));
    uint32_t chunk_size = BE32_TO_LE(*(uint32_t*)(iff_data + iff_offset + 4));

    switch (chunk_id) {
    case IFF_CHUNK_ID('F', 'O', 'R', 'M'):
      iff_offset += 8;
      continue;

    case IFF_CHUNK_ID('I', 'L', 'B', 'M'):
      iff_offset += 4;
      continue;

    case IFF_CHUNK_ID('B', 'M', 'H', 'D'): {
      CHECK(iff_offset + chunk_size <= iff_size);
      CHECK(chunk_size == sizeof(BitMapHeader));

      bmh = (BitMapHeader*)(iff_data + iff_offset + 8);
      uint16_t width = BE16_TO_LE(bmh->bmh_Width);
      uint16_t height = BE16_TO_LE(bmh->bmh_Height);

      bpls_stride_b = ((width + 0xF) / 0x10) * 0x2;
      bpls_size_b = bpls_stride_b * bmh->bmh_Depth * height;

      break;
    }

    case IFF_CHUNK_ID('B', 'O', 'D', 'Y'):
      // Decompress bitplane data if needed.
      CHECK(iff_offset + chunk_size <= iff_size);
      CHECK(bpls == NULL);

      CHECK(bpls = (uint8_t*)malloc(bpls_size_b));

      if (bmh->bmh_Compression == 0) {
        CHECK(chunk_size == bpls_size_b);
        memcpy(bpls, iff_data + iff_offset + 8, chunk_size);
      }
      else {
        // ILBM RLE decompression.
        uint8_t* body_data = iff_data + iff_offset + 8;
        uint32_t bpls_idx = 0;

        for (uint32_t body_idx = 0; body_idx < chunk_size; ) {
          uint8_t in_byte = body_data[body_idx ++];

          if (in_byte > 0x80) {
            // Repeat following byte into output.
            uint8_t rep_count = (uint8_t)(0x101 - in_byte);
            CHECK(body_idx < chunk_size);
            CHECK(bpls_idx + rep_count <= bpls_size_b);

            uint8_t rep_byte = body_data[body_idx ++];

            for (uint8_t rep_idx = 0; rep_idx < rep_count; ++ rep_idx) {
              bpls[bpls_idx ++] = rep_byte;
            }
          }
          else if (in_byte < 0x80) {
            // Copy following bytes into output.
            uint8_t lit_count = in_byte + 1;
            CHECK(body_idx + lit_count <= chunk_size);
            CHECK(bpls_idx + lit_count <= bpls_size_b);

            for (uint32_t lit_idx = 0; lit_idx < lit_count; ++ lit_idx) {
              bpls[bpls_idx ++] = body_data[body_idx ++];
            }
          }
          else {
            break;
          }
        }

        CHECK(bpls_idx == bpls_size_b);
      }

      break;
    }

    iff_offset += 8 + chunk_size;
  }

  CHECK(bpls);
  *out_bpls = bpls;
  *out_bpls_size_b = bpls_size_b;

cleanup:
  if (ret == false) {
    free(bpls);
  }

  free(iff_data);

  if (iff_file) {
    fclose(iff_file);
  }

  return ret;
}
