#include "gencommon.h"

#include <stdlib.h>

static bool make_img_array(const char* path,
                           const char* arr_name) {
  bool ret = true;
  uint8_t* bpls = NULL;
  uint32_t bpls_size_b = 0;

  CHECK(load_ilbm(path, &bpls, &bpls_size_b));

  printf("\nstatic UWORD %s[] = {", arr_name);

  for (uint32_t i = 0; i < bpls_size_b / 2; ++ i) {
    if (i % 8 == 0) {
      printf("\n ");
    }

    printf(" 0x%04X,", LE16_TO_BE(((uint16_t*)bpls)[i]));
  }

  printf("\n};\n");

cleanup:
  free(bpls);

  return ret;
}

int main() {
  bool ret = true;

  printf("#include <exec/types.h>\n");

  CHECK(make_img_array("font.iff", "font_bpls"));
  CHECK(make_img_array("pointer.iff", "pointer_bpls"));

cleanup:
  return ret ? 0 : 1;
}
