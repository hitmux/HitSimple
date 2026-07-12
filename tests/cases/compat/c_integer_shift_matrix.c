int main(void) {
  unsigned int high32 = 0x80000000U;
  long long signed_count = 31LL;
  unsigned long long unsigned_count = 31ULL;
  unsigned int left32 = 1U << signed_count;
  unsigned int logical32 = high32 >> unsigned_count;
  long long signed64 = -8LL;
  long long arithmetic64 = signed64 >> 2ULL;
  unsigned long long high64 = 0x8000000000000000ULL;
  unsigned long long logical64 = high64 >> 63LL;

  if (left32 != 0x80000000U) { return 1; }
  if (logical32 != 1U) { return 2; }
  if (arithmetic64 != -2LL) { return 3; }
  return logical64 == 1ULL ? 0 : 4;
}
