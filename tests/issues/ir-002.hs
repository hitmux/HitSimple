func main() {
    new value8 as i8 = -128
    new value16 as i16 = -32768
    new value32 as i32 = -2147483648
    new value64 as i64 = -9223372036854775808
    new divisor8 as i8 = -1
    new divisor16 as i16 = -1
    new divisor32 as i32 = -1
    new divisor64 as i64 = -1
    new result8 as i8 = value8 / divisor8
    new result16 as i16 = value16 / divisor16
    new result32 as i32 = value32 / divisor32
    new result64 as i64 = value64 / divisor64
    if (result8 == value8 && result16 == value16 && result32 == value32 && result64 == value64) {
        return 0
    }
    return 1
}
