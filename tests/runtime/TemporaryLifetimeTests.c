/* Registry-only checks: never dereference an expired C object. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#define HITSIMPLE_SOFTWARE_F128 1
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef HS_TEST_RUNTIME_SOURCE
#define HS_TEST_RUNTIME_SOURCE "../../runtime/hitsimple_runtime.c"
#endif
#include HS_TEST_RUNTIME_SOURCE

int hs_f128_format(const void *bits, char *buffer, size_t capacity) {
  (void)bits; (void)buffer; (void)capacity; abort();
}
int hs_f128_parse(const char *text, void *bits) {
  (void)text; (void)bits; abort();
}
static void require(int condition) {
  if (!condition) { fputs("registry assertion failed\n", stderr); exit(121); }
}
int main(int argc, char **argv) {
  unsigned char caller[8] = {0}, callee[8] = {0}, local[8] = {0};
  static unsigned char persistent[8];
  require(argc == 2);
  hs_frame_enter();
  hs_scope_enter();
  hs_register_local(local, sizeof(local));
  hs_register_static(persistent, sizeof(persistent));
  void *heap = hs_alloc(8);
  hs_register_temporary(caller, sizeof(caller), 1);
  if (strcmp(argv[1], "nested") == 0 ||
      strcmp(argv[1], "nested_expired") == 0) {
    hs_frame_enter(); hs_scope_enter();
    hs_register_temporary(callee, sizeof(callee), 1);
    hs_full_expression_end();
    require(hs_find(caller) != NULL && hs_find(callee) == NULL);
    hs_check_load(caller, sizeof(caller));
    hs_scope_exit(); hs_frame_exit();
    require(hs_find(caller) != NULL);
    hs_full_expression_end();
  } else if (strcmp(argv[1], "repeated4096") == 0) {
    for (unsigned i = 0; i < 4096; ++i) {
      hs_register_temporary(caller, sizeof(caller), 1);
      hs_check_load(caller, sizeof(caller));
      hs_full_expression_end();
      require(hs_find(caller) == NULL);
    }
  } else if (strcmp(argv[1], "empty") == 0) {
    for (unsigned i = 0; i < 4096; ++i) {
      hs_register_temporary(caller, sizeof(caller), 1);
      hs_register_temporary(caller, 0, 1);
      require(hs_find(caller) == NULL);
      hs_full_expression_end();
    }
  } else if (strcmp(argv[1], "shrink") == 0) {
    hs_register_temporary(caller, 1, 1);
    require(hs_find(caller) != NULL && hs_find(caller + 1) == NULL);
    hs_full_expression_end();
  } else if (strcmp(argv[1], "replace") == 0) {
    hs_register_temporary(callee, 2, 1);
    require(hs_find(caller) == NULL && hs_find(callee) != NULL);
    hs_full_expression_end();
    require(hs_find(callee) == NULL);
  } else {
    require(strcmp(argv[1], "expired") == 0);
    hs_full_expression_end();
  }
  require(hs_find(caller) == NULL);
  hs_check_load(local, sizeof(local));
  hs_check_load(persistent, sizeof(persistent));
  hs_check_load(heap, 8);
  if (strstr(argv[1], "expired") != NULL) {
    hs_set_source_location("temporary-lifetime.hs", 17, 9);
    hs_check_load(caller, 1);
    require(0);
  }
  hs_free(heap); hs_scope_exit(); hs_frame_exit();
  require(hs_find(local) == NULL && hs_find(persistent) != NULL);
  return 0;
}
