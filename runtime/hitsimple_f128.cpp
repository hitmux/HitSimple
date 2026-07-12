#include <boost/multiprecision/cpp_bin_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace {

using QuadBackend = boost::multiprecision::cpp_bin_float<
    113, boost::multiprecision::digit_base_2, void, std::int32_t, -20000,
    20000>;
using Quad = boost::multiprecision::number<
    QuadBackend, boost::multiprecision::et_off>;
using BigInt = boost::multiprecision::cpp_int;
using Bits = unsigned __int128;

constexpr Bits kFractionMask = (Bits{1} << 112U) - 1U;
constexpr Bits kExponentMask = Bits{0x7fff} << 112U;
constexpr Bits kSignMask = Bits{1} << 127U;
constexpr int kExponentBias = 16383;

bool isZero(Bits value) { return (value & ~kSignMask) == 0; }
bool isInfinity(Bits value) {
  return (value & kExponentMask) == kExponentMask &&
         (value & kFractionMask) == 0;
}
bool isNaN(Bits value) {
  return (value & kExponentMask) == kExponentMask &&
         (value & kFractionMask) != 0;
}
Bits negate(Bits value) { return value ^ kSignMask; }
Bits signedZero(bool negative) { return negative ? kSignMask : 0; }
Bits quietNaN() { return kExponentMask | (Bits{1} << 111U); }

BigInt bigInteger(Bits value) {
  BigInt result = static_cast<std::uint64_t>(value >> 64U);
  result <<= 64U;
  result += static_cast<std::uint64_t>(value);
  return result;
}

Bits bitsFromBigInteger(const BigInt& value) {
  const BigInt mask = (BigInt{1} << 64U) - 1U;
  const auto low = static_cast<std::uint64_t>(value & mask);
  const auto high = static_cast<std::uint64_t>(value >> 64U);
  return (Bits{high} << 64U) | Bits{low};
}

BigInt roundToNearestEven(const Quad& value) {
  const Quad lower = boost::multiprecision::floor(value);
  BigInt result = lower.convert_to<BigInt>();
  const Quad remainder = value - lower;
  const Quad half = Quad(0.5);
  if (remainder > half ||
      (remainder == half && static_cast<bool>(result & 1))) {
    ++result;
  }
  return result;
}

BigInt roundRationalToNearestEven(const BigInt& numerator,
                                  const BigInt& denominator) {
  BigInt quotient = numerator / denominator;
  const BigInt remainder = numerator % denominator;
  const BigInt twiceRemainder = remainder << 1U;
  if (twiceRemainder > denominator ||
      (twiceRemainder == denominator && static_cast<bool>(quotient & 1))) {
    ++quotient;
  }
  return quotient;
}

BigInt powerOfTen(unsigned exponent) {
  BigInt result = 1;
  BigInt base = 10;
  while (exponent != 0) {
    if ((exponent & 1U) != 0) {
      result *= base;
    }
    exponent >>= 1U;
    if (exponent != 0) {
      base *= base;
    }
  }
  return result;
}

int rationalBinaryExponent(const BigInt& numerator,
                           const BigInt& denominator) {
  int exponent = static_cast<int>(boost::multiprecision::msb(numerator)) -
                 static_cast<int>(boost::multiprecision::msb(denominator));
  if (exponent >= 0) {
    if (numerator < (denominator << static_cast<unsigned>(exponent))) {
      --exponent;
    }
  } else if ((numerator << static_cast<unsigned>(-exponent)) < denominator) {
    --exponent;
  }
  return exponent;
}

Bits encodeRational(BigInt numerator, BigInt denominator, bool negative) {
  const Bits sign = negative ? kSignMask : 0;
  if (numerator == 0) {
    return sign;
  }

  int exponent = rationalBinaryExponent(numerator, denominator);
  if (exponent > 16383) {
    return sign | kExponentMask;
  }

  if (exponent < -16382) {
    BigInt fraction =
        roundRationalToNearestEven(numerator << 16494U, denominator);
    if (fraction == 0) {
      return sign;
    }
    if (fraction >= (BigInt{1} << 112U)) {
      return sign | (Bits{1} << 112U);
    }
    return sign | bitsFromBigInteger(fraction);
  }

  const int shift = 112 - exponent;
  BigInt significand;
  if (shift >= 0) {
    significand = roundRationalToNearestEven(
        numerator << static_cast<unsigned>(shift), denominator);
  } else {
    significand = roundRationalToNearestEven(
        numerator, denominator << static_cast<unsigned>(-shift));
  }
  if (significand >= (BigInt{1} << 113U)) {
    significand >>= 1U;
    ++exponent;
    if (exponent > 16383) {
      return sign | kExponentMask;
    }
  }
  const Bits exponentBits =
      Bits{static_cast<unsigned>(exponent + kExponentBias)} << 112U;
  return sign | exponentBits |
         bitsFromBigInteger(significand - (BigInt{1} << 112U));
}

std::optional<Bits> parseDecimalBits(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }
  bool negative = false;
  if (text.front() == '+' || text.front() == '-') {
    negative = text.front() == '-';
    text.remove_prefix(1);
  }
  if (text.empty()) {
    return std::nullopt;
  }

  std::string lower;
  lower.reserve(text.size());
  for (const char ch : text) {
    lower.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch))));
  }
  if (lower == "inf" || lower == "infinity") {
    return (negative ? kSignMask : 0) | kExponentMask;
  }
  if (lower == "nan") {
    return (negative ? kSignMask : 0) | quietNaN();
  }

  std::string digits;
  digits.reserve(text.size());
  std::size_t fractionalDigits = 0;
  bool sawDigit = false;
  bool sawPoint = false;
  std::size_t index = 0;
  for (; index < text.size(); ++index) {
    const char ch = text[index];
    if (ch >= '0' && ch <= '9') {
      digits.push_back(ch);
      sawDigit = true;
      if (sawPoint) {
        ++fractionalDigits;
      }
      continue;
    }
    if (ch == '.' && !sawPoint) {
      sawPoint = true;
      continue;
    }
    break;
  }
  if (!sawDigit) {
    return std::nullopt;
  }

  int explicitExponent = 0;
  if (index < text.size() && (text[index] == 'e' || text[index] == 'E')) {
    ++index;
    bool exponentNegative = false;
    if (index < text.size() && (text[index] == '+' || text[index] == '-')) {
      exponentNegative = text[index] == '-';
      ++index;
    }
    const std::size_t exponentBegin = index;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
      if (explicitExponent < 100000) {
        explicitExponent = explicitExponent * 10 + (text[index] - '0');
      }
      ++index;
    }
    if (index == exponentBegin) {
      return std::nullopt;
    }
    if (exponentNegative) {
      explicitExponent = -explicitExponent;
    }
  }
  if (index != text.size()) {
    return std::nullopt;
  }

  const auto firstNonZero = digits.find_first_not_of('0');
  if (firstNonZero == std::string::npos) {
    return negative ? kSignMask : Bits{0};
  }
  digits.erase(0, firstNonZero);
  const long long decimalExponent =
      static_cast<long long>(explicitExponent) -
      static_cast<long long>(fractionalDigits);
  const long long decimalMagnitude =
      static_cast<long long>(digits.size()) + decimalExponent;
  if (decimalMagnitude > 6000) {
    return (negative ? kSignMask : 0) | kExponentMask;
  }
  if (decimalMagnitude < -6000) {
    return negative ? kSignMask : Bits{0};
  }

  BigInt significand = 0;
  for (const char ch : digits) {
    significand *= 10;
    significand += ch - '0';
  }
  BigInt numerator = significand;
  BigInt denominator = 1;
  if (decimalExponent >= 0) {
    numerator *= powerOfTen(static_cast<unsigned>(decimalExponent));
  } else {
    denominator = powerOfTen(static_cast<unsigned>(-decimalExponent));
  }
  return encodeRational(std::move(numerator), std::move(denominator), negative);
}

Quad decode(Bits bits) {
  const bool negative = (bits & kSignMask) != 0;
  const unsigned exponent =
      static_cast<unsigned>((bits & kExponentMask) >> 112U);
  const Bits fraction = bits & kFractionMask;
  Quad value;
  if (exponent == 0x7fffU) {
    value = fraction == 0 ? std::numeric_limits<Quad>::infinity()
                          : std::numeric_limits<Quad>::quiet_NaN();
  } else if (exponent == 0) {
    value = boost::multiprecision::ldexp(Quad(bigInteger(fraction)), -16494);
  } else {
    const BigInt significand = (BigInt{1} << 112U) + bigInteger(fraction);
    value = boost::multiprecision::ldexp(
        Quad(significand), static_cast<int>(exponent) - kExponentBias - 112);
  }
  return negative ? -value : value;
}

Bits encode(const Quad& input) {
  const bool negative = boost::multiprecision::signbit(input);
  const Bits sign = negative ? kSignMask : 0;
  Quad value = negative ? -input : input;
  if (boost::multiprecision::isnan(value)) {
    return sign | kExponentMask | (Bits{1} << 111U);
  }
  if (boost::multiprecision::isinf(value)) {
    return sign | kExponentMask;
  }
  if (value == 0) {
    return sign;
  }

  int exponent = 0;
  const Quad mantissa = boost::multiprecision::frexp(value, &exponent);
  int unbiased = exponent - 1;
  if (unbiased > 16383) {
    return sign | kExponentMask;
  }
  if (unbiased < -16382) {
    const Quad scaled = boost::multiprecision::ldexp(value, 16494);
    BigInt fraction = roundToNearestEven(scaled);
    if (fraction <= 0) {
      return sign;
    }
    if (fraction >= (BigInt{1} << 112U)) {
      return sign | (Bits{1} << 112U);
    }
    return sign | bitsFromBigInteger(fraction);
  }

  const Quad scaled = boost::multiprecision::ldexp(mantissa, 113);
  BigInt significand = scaled.convert_to<BigInt>();
  if (significand == (BigInt{1} << 113U)) {
    significand >>= 1U;
    ++unbiased;
    if (unbiased > 16383) {
      return sign | kExponentMask;
    }
  }
  const Bits exponentBits = Bits{static_cast<unsigned>(unbiased + kExponentBias)}
                            << 112U;
  const BigInt fraction = significand - (BigInt{1} << 112U);
  return sign | exponentBits | bitsFromBigInteger(fraction);
}

template <typename Function>
Bits unary(Bits value, Function function) {
  return encode(function(decode(value)));
}

template <typename Function>
Bits binary(Bits left, Bits right, Function function) {
  return encode(function(decode(left), decode(right)));
}

Bits loadBits(const void* source) {
  Bits value = quietNaN();
  if (source != nullptr) {
    std::memcpy(&value, source, sizeof(value));
  }
  return value;
}

void storeBits(void* destination, Bits value) {
  if (destination != nullptr) {
    std::memcpy(destination, &value, sizeof(value));
  }
}

} // namespace

extern "C" {

void hs_f128_literal(void* result, const char* text) {
  storeBits(result, text == nullptr
                        ? kExponentMask | (Bits{1} << 111U)
                        : parseDecimalBits(text).value_or(quietNaN()));
}

void hs_f128_add(void* result, const void* leftBits, const void* rightBits) {
  const Bits left = loadBits(leftBits);
  const Bits right = loadBits(rightBits);
  if (isZero(left) && isZero(right)) {
    storeBits(result,
              signedZero((left & kSignMask) != 0 &&
                         (right & kSignMask) != 0));
    return;
  }
  if (isZero(left) && !isNaN(right)) {
    storeBits(result, right);
    return;
  }
  if (isZero(right) && !isNaN(left)) {
    storeBits(result, left);
    return;
  }
  storeBits(result, binary(left, right, [](const Quad& a, const Quad& b) {
              return a + b;
            }));
}
void hs_f128_sub(void* result, const void* leftBits, const void* rightBits) {
  const Bits left = loadBits(leftBits);
  const Bits right = loadBits(rightBits);
  if (isZero(left) && isZero(right)) {
    storeBits(result,
              signedZero((left & kSignMask) != 0 &&
                         (right & kSignMask) == 0));
    return;
  }
  if (isZero(left) && !isNaN(right)) {
    storeBits(result, negate(right));
    return;
  }
  if (isZero(right) && !isNaN(left)) {
    storeBits(result, left);
    return;
  }
  storeBits(result, binary(left, right, [](const Quad& a, const Quad& b) {
              return a - b;
            }));
}
void hs_f128_mul(void* result, const void* leftBits, const void* rightBits) {
  const Bits left = loadBits(leftBits);
  const Bits right = loadBits(rightBits);
  if ((isZero(left) && isInfinity(right)) ||
      (isInfinity(left) && isZero(right))) {
    storeBits(result, quietNaN());
    return;
  }
  if (isZero(left) || isZero(right)) {
    storeBits(result, signedZero(((left ^ right) & kSignMask) != 0));
    return;
  }
  storeBits(result, binary(left, right, [](const Quad& a, const Quad& b) {
              return a * b;
            }));
}
void hs_f128_div(void* result, const void* leftBits, const void* rightBits) {
  const Bits left = loadBits(leftBits);
  const Bits right = loadBits(rightBits);
  if ((isZero(left) && isZero(right)) ||
      (isInfinity(left) && isInfinity(right))) {
    storeBits(result, quietNaN());
    return;
  }
  if (isZero(left) || isInfinity(right)) {
    storeBits(result, signedZero(((left ^ right) & kSignMask) != 0));
    return;
  }
  storeBits(result, binary(left, right, [](const Quad& a, const Quad& b) {
              return a / b;
            }));
}

std::uint8_t hs_f128_eq(const void* left, const void* right) {
  return decode(loadBits(left)) == decode(loadBits(right));
}
std::uint8_t hs_f128_ne(const void* left, const void* right) {
  return decode(loadBits(left)) != decode(loadBits(right));
}
std::uint8_t hs_f128_lt(const void* left, const void* right) {
  return decode(loadBits(left)) < decode(loadBits(right));
}
std::uint8_t hs_f128_le(const void* left, const void* right) {
  return decode(loadBits(left)) <= decode(loadBits(right));
}
std::uint8_t hs_f128_gt(const void* left, const void* right) {
  return decode(loadBits(left)) > decode(loadBits(right));
}
std::uint8_t hs_f128_ge(const void* left, const void* right) {
  return decode(loadBits(left)) >= decode(loadBits(right));
}

void hs_f128_from_i64(void* result, std::int64_t value) {
  storeBits(result, encode(Quad(value)));
}
void hs_f128_from_u64(void* result, std::uint64_t value) {
  storeBits(result, encode(Quad(value)));
}
void hs_f128_from_f32(void* result, float value) {
  storeBits(result, value == 0 && std::signbit(value) ? kSignMask
                                                      : encode(Quad(value)));
}
void hs_f128_from_f64(void* result, double value) {
  storeBits(result, value == 0 && std::signbit(value) ? kSignMask
                                                      : encode(Quad(value)));
}
std::int64_t hs_f128_to_i64(const void* bits) {
  return decode(loadBits(bits)).convert_to<std::int64_t>();
}
std::uint64_t hs_f128_to_u64(const void* bits) {
  return decode(loadBits(bits)).convert_to<std::uint64_t>();
}
float hs_f128_to_f32(const void* bits) {
  const Bits value = loadBits(bits);
  if (isZero(value)) return (value & kSignMask) != 0 ? -0.0F : 0.0F;
  return decode(value).convert_to<float>();
}
double hs_f128_to_f64(const void* bits) {
  const Bits value = loadBits(bits);
  if (isZero(value)) return (value & kSignMask) != 0 ? -0.0 : 0.0;
  return decode(value).convert_to<double>();
}

void hs_f128_abs(void* result, const void* bits) {
  storeBits(result, loadBits(bits) & ~kSignMask);
}
void hs_f128_sqrt(void* result, const void* bits) {
  const Bits value = loadBits(bits);
  storeBits(result, isZero(value)
                        ? value
                        : unary(value, [](const Quad& a) {
                            return boost::multiprecision::sqrt(a);
                          }));
}
void hs_f128_pow(void* result, const void* left, const void* right) {
  storeBits(result, binary(loadBits(left), loadBits(right),
                           [](const Quad& a, const Quad& b) {
                             return boost::multiprecision::pow(a, b);
                           }));
}
void hs_f128_sin(void* result, const void* bits) {
  const Bits value = loadBits(bits);
  storeBits(result, isZero(value)
                        ? value
                        : unary(value, [](const Quad& a) {
                            return boost::multiprecision::sin(a);
                          }));
}
void hs_f128_cos(void* result, const void* bits) {
  storeBits(result, unary(loadBits(bits), [](const Quad& a) {
              return boost::multiprecision::cos(a);
            }));
}
void hs_f128_tan(void* result, const void* bits) {
  const Bits value = loadBits(bits);
  storeBits(result, isZero(value)
                        ? value
                        : unary(value, [](const Quad& a) {
                            return boost::multiprecision::tan(a);
                          }));
}
void hs_f128_log(void* result, const void* bits) {
  storeBits(result, unary(loadBits(bits), [](const Quad& a) {
              return boost::multiprecision::log(a);
            }));
}
void hs_f128_exp(void* result, const void* bits) {
  storeBits(result, unary(loadBits(bits), [](const Quad& a) {
              return boost::multiprecision::exp(a);
            }));
}
void hs_f128_floor(void* result, const void* bits) {
  const Bits value = loadBits(bits);
  storeBits(result, isZero(value)
                        ? value
                        : unary(value, [](const Quad& a) {
                            return boost::multiprecision::floor(a);
                          }));
}
void hs_f128_ceil(void* result, const void* bits) {
  const Bits value = loadBits(bits);
  storeBits(result, isZero(value)
                        ? value
                        : unary(value, [](const Quad& a) {
                            return boost::multiprecision::ceil(a);
                          }));
}
void hs_f128_round(void* result, const void* bits) {
  const Bits value = loadBits(bits);
  storeBits(result, isZero(value)
                        ? value
                        : unary(value, [](const Quad& a) {
                            return boost::multiprecision::round(a);
                          }));
}

int hs_f128_format(const void* bits, char* buffer, std::size_t capacity) {
  if (bits == nullptr || buffer == nullptr || capacity == 0) {
    return -1;
  }
  Bits valueBits = 0;
  std::memcpy(&valueBits, bits, sizeof(valueBits));
  std::string text;
  if (isZero(valueBits)) {
    text = (valueBits & kSignMask) != 0 ? "-0" : "0";
  } else {
    const Quad value = decode(valueBits);
    std::ostringstream stream;
    stream << std::setprecision(36) << value;
    text = stream.str();
  }
  if (text.size() >= capacity) {
    return -1;
  }
  std::memcpy(buffer, text.data(), text.size());
  buffer[text.size()] = '\0';
  return static_cast<int>(text.size());
}

int hs_f128_parse(const char* text, void* bits) {
  if (text == nullptr || bits == nullptr) {
    return 0;
  }
  const auto parsed = parseDecimalBits(text);
  if (!parsed) {
    return 0;
  }
  std::memcpy(bits, &*parsed, sizeof(*parsed));
  return 1;
}

} // extern "C"
