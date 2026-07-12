#define __STDC_WANT_IEC_60559_TYPES_EXT__ 1

#if defined(__clang__) && defined(__GLIBC__) && defined(__x86_64__) && \
    defined(__SIZEOF_FLOAT128__) && defined(__HAVE_FLOAT128) && \
    !__HAVE_FLOAT128
typedef __float128 HsFloat128;
#else
typedef _Float128 HsFloat128;
#endif

extern HsFloat128 fabsf128(HsFloat128);
extern HsFloat128 sqrtf128(HsFloat128);
extern HsFloat128 powf128(HsFloat128, HsFloat128);
extern HsFloat128 sinf128(HsFloat128);
extern HsFloat128 cosf128(HsFloat128);
extern HsFloat128 tanf128(HsFloat128);
extern HsFloat128 logf128(HsFloat128);
extern HsFloat128 expf128(HsFloat128);
extern HsFloat128 floorf128(HsFloat128);
extern HsFloat128 ceilf128(HsFloat128);
extern HsFloat128 roundf128(HsFloat128);

HsFloat128 hs_f128_abs(HsFloat128 value) { return fabsf128(value); }
HsFloat128 hs_f128_sqrt(HsFloat128 value) { return sqrtf128(value); }
HsFloat128 hs_f128_pow(HsFloat128 left, HsFloat128 right) {
  return powf128(left, right);
}
HsFloat128 hs_f128_sin(HsFloat128 value) { return sinf128(value); }
HsFloat128 hs_f128_cos(HsFloat128 value) { return cosf128(value); }
HsFloat128 hs_f128_tan(HsFloat128 value) { return tanf128(value); }
HsFloat128 hs_f128_log(HsFloat128 value) { return logf128(value); }
HsFloat128 hs_f128_exp(HsFloat128 value) { return expf128(value); }
HsFloat128 hs_f128_floor(HsFloat128 value) { return floorf128(value); }
HsFloat128 hs_f128_ceil(HsFloat128 value) { return ceilf128(value); }
HsFloat128 hs_f128_round(HsFloat128 value) { return roundf128(value); }
