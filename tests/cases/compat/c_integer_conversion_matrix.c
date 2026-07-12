int main(void) {
  signed char negative = -1;
  unsigned char maximum = 0xffU;
  unsigned long long source = 0x1ffffffffULL;
  signed char signed8 = (signed char)source;
  short signed16 = (short)source;
  int signed32 = (int)source;
  long long signed_wide = (long long)negative;
  unsigned long long unsigned_wide = (unsigned long long)maximum;
  unsigned char low8 = (unsigned char)source;
  unsigned short low16 = (unsigned short)source;
  unsigned int low32 = (unsigned int)source;

  if (signed8 != -1) { return 1; }
  if (signed16 != -1) { return 2; }
  if (signed32 != -1) { return 3; }
  if (signed_wide != -1LL) { return 4; }
  if (unsigned_wide != 255ULL) { return 5; }
  if (low8 != 255U) { return 6; }
  if (low16 != 65535U) { return 7; }
  return low32 == 0xffffffffU ? 0 : 8;
}
