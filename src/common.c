#include "common.h"
#include "build/tables.h"

#include <proto/dos.h>

UWORD abs(WORD value) {
  WORD abs_mask = value >> (kBitsPerWord - 1);
  return (value ^ abs_mask) - abs_mask;
}

UWORD str_len(STRPTR str) {
  UWORD len;
  for (len = 0; str[len]; ++ len);
  return len;
}

VOID print_error(STRPTR msg) {
  if (DOSBase) {
    STRPTR out_strs[] = { "beep: assert(", msg, ") failed\n" };
    BPTR out_handle = Output();

    for (UWORD i = 0; i < ARRAY_SIZE(out_strs); ++ i) {
      Write(out_handle, out_strs[i], str_len(out_strs[i]));
    }
  }
}
