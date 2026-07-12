int main(void) {
  unsigned int zero = 0U;
  long zero_long = 0L;
  unsigned long zero_wide = 0UL;
  if (zero != 0U || zero_long != 0L || zero_wide != 0UL) {
    return 4;
  }

  unsigned int value = 0x80000000U;
  int negative = -1;
  if (negative < value) {
    return 1;
  }

  value >>= 31;
  value <<= 4;
  value &= 0x1fU;
  value ^= 1U;
  value |= 2U;
  if (value != 19U) {
    return 2;
  }

  unsigned long wide = 0xffffffffffffffffUL;
  if ((wide >> 63) != 1UL) {
    return 3;
  }
  return 0;
}
