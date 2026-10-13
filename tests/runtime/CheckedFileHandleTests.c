/* Standalone lifecycle tests: no token below ever reaches the host stdio.
 * Compile this file alone, without linking hitsimple_runtime. The software
 * f128 stubs keep this file independent of the platform's binary128 backend.
 * Run negative cases in separate processes with run_checked_file_handles.py.
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#define HITSIMPLE_SOFTWARE_F128 1

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/types.h>
#endif

enum { TOKEN_COUNT = 4096, TEST_FAILURE = 121 };
static FILE tokens[TOKEN_COUNT + 1];
static FILE *nextOpen;
static FILE *expectedFile;
static int forbidCalls;
static int closeResult;
static int checkDuringClose;
static unsigned calls;
static unsigned opens;
static unsigned closes;

void hs_check_file_handle(FILE *file);
int32_t hs_fget(FILE *file);

static void require(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "test failure: %s\n", message);
    exit(TEST_FAILURE);
  }
}

static void recordCall(FILE *file, const char *operation) {
  if (forbidCalls) {
    fprintf(stderr, "FORBIDDEN_LIBC_CALL: %s\n", operation);
    exit(TEST_FAILURE);
  }
  require(file == expectedFile, "stdio received the wrong handle");
  ++calls;
}

static FILE *testFopen(const char *name, const char *mode) {
  require(strcmp(name, "test-file") == 0, "fopen name changed");
  require(strcmp(mode, "r+") == 0, "fopen mode changed");
  ++opens;
  return nextOpen;
}

static int testFclose(FILE *file) {
  recordCall(file, "fclose");
  ++closes;
  if (checkDuringClose) {
    /* Check the ordering without asking the real libc to close anything. */
    forbidCalls = 1;
    (void)hs_fget(file);
    require(0, "closing handle remained open inside libc fclose");
  }
  return closeResult;
}

static int testFgetc(FILE *file) {
  recordCall(file, "fgetc");
  return 'x';
}

static int testFflush(FILE *file) {
  recordCall(file, "fflush");
  return 0;
}

static int testFeof(FILE *file) {
  recordCall(file, "feof");
  return 0;
}

static int testFerror(FILE *file) {
  recordCall(file, "ferror");
  return 0;
}

#if defined(_WIN32)
typedef int64_t TestOffset;
#else
typedef off_t TestOffset;
#endif

static int testFseek(FILE *file, TestOffset offset, int whence) {
  recordCall(file, "fseek");
  require(offset == 37 && whence == SEEK_SET, "fseek arguments changed");
  return 0;
}

static TestOffset testFtell(FILE *file) {
  recordCall(file, "ftell");
  return 37;
}

static size_t testFread(void *buffer, size_t size, size_t count, FILE *file) {
  recordCall(file, "fread");
  memset(buffer, 'x', size * count);
  return count;
}

static size_t testFwrite(const void *buffer, size_t size, size_t count,
                         FILE *file) {
  recordCall(file, "fwrite");
  require(buffer != NULL && size != 0, "unexpected empty libc fwrite");
  return count;
}

static int testUngetc(int value, FILE *file) {
  recordCall(file, "ungetc");
  return value;
}

/* Some C libraries define these names as macros. Include stdio first and
 * replace them only while compiling the runtime under test. Diagnostics still
 * use real fprintf/fputc on stderr, never on a synthetic FILE token. */
#undef fopen
#undef fclose
#undef fgetc
#undef fflush
#undef feof
#undef ferror
#undef fread
#undef fwrite
#undef ungetc
#define fopen testFopen
#define fclose testFclose
#define fgetc testFgetc
#define fflush testFflush
#define feof testFeof
#define ferror testFerror
#define fread testFread
#define fwrite testFwrite
#define ungetc testUngetc
#if defined(_WIN32)
#define _fseeki64 testFseek
#define _ftelli64 testFtell
#else
#define fseeko testFseek
#define ftello testFtell
#endif
#ifndef HS_TEST_RUNTIME_SOURCE
#define HS_TEST_RUNTIME_SOURCE "../../runtime/hitsimple_runtime.c"
#endif
#include HS_TEST_RUNTIME_SOURCE
#undef fopen
#undef fclose
#undef fgetc
#undef fflush
#undef feof
#undef ferror
#undef fread
#undef fwrite
#undef ungetc
#if defined(_WIN32)
#undef _fseeki64
#undef _ftelli64
#else
#undef fseeko
#undef ftello
#endif

int hs_f128_format(const void *bits, char *buffer, size_t capacity) {
  (void)bits;
  (void)buffer;
  (void)capacity;
  require(0, "unexpected f128 formatting");
  return 0;
}

int hs_f128_parse(const char *text, void *bits) {
  (void)text;
  (void)bits;
  require(0, "unexpected f128 scanning");
  return 0;
}

static char fileName[] = "test-file";
static char fileMode[] = "r+";
static char literalFormat[] = "x";
static char emptyFormat[] = "";
static char characterFormat[] = "%c";
static unsigned char bytes[4] = {'x', 'x', 'x', 'x'};

static FILE *openToken(unsigned index) {
  nextOpen = &tokens[index];
  FILE *file = hs_fopen(fileName, fileMode);
  require(file == nextOpen, "fopen result changed");
  expectedFile = file;
  return file;
}

static const char *const operations[] = {
    "check", "close", "get", "flush", "eof", "error", "seek", "tell",
    "read", "write", "put", "format", "scan", "scan_target",
    "read_zero", "write_zero", "put_zero", "format_empty", "scan_empty"};

static void consume(const char *operation, FILE *file) {
  if (strcmp(operation, "check") == 0) {
    hs_check_file_handle(file);
  } else if (strcmp(operation, "close") == 0) {
    require(hs_fclose(file) == closeResult, "fclose result changed");
  } else if (strcmp(operation, "get") == 0) {
    require(hs_fget(file) == 'x', "fget result changed");
  } else if (strcmp(operation, "flush") == 0) {
    require(hs_fflush(file) == 0, "fflush result changed");
  } else if (strcmp(operation, "eof") == 0) {
    require(hs_feof(file) == 0, "feof result changed");
  } else if (strcmp(operation, "error") == 0) {
    require(hs_ferror(file) == 0, "ferror result changed");
  } else if (strcmp(operation, "seek") == 0) {
    require(hs_fseek(file, 37, SEEK_SET) == 0, "fseek result changed");
  } else if (strcmp(operation, "tell") == 0) {
    require(hs_ftell(file) == 37, "ftell result changed");
  } else if (strcmp(operation, "read") == 0) {
    require(hs_fread(bytes, 2, 2, file) == 2, "fread result changed");
  } else if (strcmp(operation, "write") == 0) {
    require(hs_fwrite(bytes, 2, 2, file) == 2, "fwrite result changed");
  } else if (strcmp(operation, "put") == 0) {
    require(hs_fput(file, bytes, sizeof(bytes)) == 4, "fput result changed");
  } else if (strcmp(operation, "format") == 0) {
    require(hs_format_output(file, literalFormat, NULL, 0, 1) == 1,
            "format output result changed");
  } else if (strcmp(operation, "scan") == 0) {
    require(hs_scan_input(file, literalFormat, NULL, 0, 1) == 0,
            "scan input result changed");
  } else if (strcmp(operation, "scan_target") == 0) {
    HsScanTarget target = {bytes, sizeof(bytes), HS_SCAN_ANY};
    require(hs_scan_input(file, characterFormat, &target, 1, 1) == 1,
            "scan target result changed");
    require(bytes[0] == 'x', "scan target was not written");
  } else if (strcmp(operation, "read_zero") == 0) {
    require(hs_fread(NULL, 0, 1, file) == 0, "empty fread result changed");
  } else if (strcmp(operation, "write_zero") == 0) {
    require(hs_fwrite(NULL, 1, 0, file) == 0, "empty fwrite result changed");
  } else if (strcmp(operation, "put_zero") == 0) {
    require(hs_fput(file, NULL, 0) == 0, "empty fput result changed");
  } else if (strcmp(operation, "format_empty") == 0) {
    require(hs_format_output(file, emptyFormat, NULL, 0, 1) == 0,
            "empty format result changed");
  } else if (strcmp(operation, "scan_empty") == 0) {
    require(hs_scan_input(file, emptyFormat, NULL, 0, 1) == 0,
            "empty scan result changed");
  } else {
    require(0, "unknown operation");
  }
}

static void consumeOpen(FILE *file) {
  unsigned before = calls;
  for (size_t i = 0; i < sizeof(operations) / sizeof(operations[0]); ++i) {
    if (strcmp(operations[i], "close") != 0) {
      consume(operations[i], file);
    }
  }
  require(calls - before == 12, "open operations skipped or duplicated stdio");
  consume("close", file);
}

static void growRegistry(void) {
  /* Close an early entry before several growth steps: both open entries and
   * closed tombstones must survive reallocations or hash-table rebuilds. */
  FILE *first = openToken(0);
  consume("close", first);
  for (unsigned i = 1; i < TOKEN_COUNT; ++i) {
    (void)openToken(i);
  }
  for (unsigned i = 1; i < TOKEN_COUNT; ++i) {
    expectedFile = &tokens[i];
    consume("get", expectedFile);
    consume("close", expectedFile);
  }
  require(opens == TOKEN_COUNT && closes == TOKEN_COUNT,
          "registry growth lost an open or close");
}

int main(int argc, char **argv) {
  require(argc >= 2, "expected a scenario");
  hs_register_static(fileName, sizeof(fileName));
  hs_register_static(fileMode, sizeof(fileMode));
  hs_register_static(literalFormat, sizeof(literalFormat));
  hs_register_static(emptyFormat, sizeof(emptyFormat));
  hs_register_static(characterFormat, sizeof(characterFormat));
  hs_register_static(bytes, sizeof(bytes));

  if (strcmp(argv[1], "open") == 0) {
    consumeOpen(openToken(0));
    require(opens == 1 && closes == 1, "open/close count changed");
  } else if (strcmp(argv[1], "unknown") == 0) {
    expectedFile = &tokens[TOKEN_COUNT];
    consumeOpen(expectedFile);
    require(opens == 0 && closes == 1, "unknown FFI handle was not forwarded");
  } else if (strcmp(argv[1], "reuse") == 0) {
    FILE *oldAlias = openToken(0);
    consume("close", oldAlias);
    FILE *reopened = openToken(0);
    require(oldAlias == reopened, "test did not reuse an address");
    /* Raw-pointer ABI cannot distinguish old aliases after address reuse. */
    consume("get", oldAlias);
    consumeOpen(reopened);
    require(opens == 2 && closes == 2, "reopen failed");
  } else if (strcmp(argv[1], "growth") == 0) {
    growRegistry();
  } else if (strcmp(argv[1], "console") == 0) {
    expectedFile = stdout;
    consume("format", NULL);
    consume("format_empty", NULL);
    expectedFile = stdin;
    consume("scan", NULL);
    consume("scan_target", NULL);
    consume("scan_empty", NULL);
    require(calls == 3, "console null sentinel was not forwarded");
  } else if (strcmp(argv[1], "unchecked") == 0) {
    FILE *file = openToken(0);
    consume("close", file);
    /* Checked state must not change the unchecked formatted-I/O contract.
     * The stub makes deliberately unchecked closed-pointer use safe here. */
    require(hs_format_output(file, literalFormat, NULL, 0, 0) == 1,
            "unchecked output unexpectedly checked the handle");
    require(hs_scan_input(file, literalFormat, NULL, 0, 0) == 0,
            "unchecked input unexpectedly checked the handle");
    require(calls == 3, "unchecked stdio was not forwarded");
  } else if (strcmp(argv[1], "failed_open") == 0) {
    nextOpen = NULL;
    require(hs_fopen(fileName, fileMode) == NULL, "failed fopen result changed");
    consumeOpen(openToken(0));
  } else if (strcmp(argv[1], "close_reentrant") == 0) {
    FILE *file = openToken(0);
    checkDuringClose = 1;
    consume("close", file);
    require(0, "reentrant close did not fail");
  } else {
    require(argc == 3, "expected a negative-test operation");
    FILE *file = NULL;
    if (strcmp(argv[1], "closed") == 0 ||
        strcmp(argv[1], "failed_close") == 0 ||
        strcmp(argv[1], "alias") == 0) {
      file = openToken(0);
      FILE *aliases[] = {file, file, file};
      closeResult = strcmp(argv[1], "failed_close") == 0 ? EOF : 0;
      consume("close", aliases[1]);
      file = aliases[2];
    } else if (strcmp(argv[1], "unknown_closed") == 0 ||
               strcmp(argv[1], "unknown_failed_close") == 0) {
      expectedFile = file = &tokens[TOKEN_COUNT];
      closeResult = strcmp(argv[1], "unknown_failed_close") == 0 ? EOF : 0;
      consume("close", file);
    } else if (strcmp(argv[1], "growth_closed") == 0) {
      growRegistry();
      file = &tokens[0];
    } else if (strcmp(argv[1], "null") == 0) {
      file = NULL;
    } else {
      require(0, "unknown scenario");
    }
    forbidCalls = 1;
    consume(argv[2], file);
    require(0, "invalid handle was accepted");
  }
  return 0;
}
