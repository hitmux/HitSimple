#include <stdint.h>
#include <stdio.h>

#undef feof
#undef ferror
#undef fflush
#undef fclose

/* Deterministic C status values expose a truncated-to-byte or truncated-to-i1
   feof result even on hosts whose libc normally returns only zero or one.
   Override the C functions so both unchecked libc calls and checked hs_*
   wrappers traverse the same public C-int boundary. */
static int result;

void set_stdio_result(int32_t value) { result = (int)value; }

int feof(FILE *file) {
  (void)file;
  return result;
}

int ferror(FILE *file) {
  (void)file;
  return result;
}

int fflush(FILE *file) {
  (void)file;
  return result;
}

int fclose(FILE *file) {
  (void)file;
  /* The fixture opens only five streams. Leave their actual cleanup to process
     exit so no implementation-specific alternate fclose symbol is necessary. */
  return result;
}
