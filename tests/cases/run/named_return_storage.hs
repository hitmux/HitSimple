$include <stdio.hsh>
$include <stdlib.hsh>
$include <string.hsh>

new evaluations as i32 = 0
new assignments as i32 = 0
new output as i32 = 99

func named_i8(value as i8) -> result as i8 { result = value; return; }
func named_i16(value as i16) -> result as i16 { result = value; return; }
func named_i32(value as i32) -> result as i32 { result = value; return; }
func named_i64(value as i64) -> result as i64 { result = value; return; }
func named_u8(value as u8) -> result as u8 { result = value; return; }
func named_u16(value as u16) -> result as u16 { result = value; return; }
func named_u32(value as u32) -> result as u32 { result = value; return; }
func named_u64(value as u64) -> result as u64 { result = value; return; }
func named_f16(value as f16) -> result as f16 { result = value; return; }
func named_f32(value as f32) -> result as f32 { result = value; return; }
func named_f64(value as f64) -> result as f64 { result = value; return; }
func named_f128(value as f128) -> result as f128 { result = value; return; }
func named_addr(value as addr) -> result as addr { result = value; return; }
func named_handle(value as handle) -> result as handle { result = value; return; }
func named_none(value[8] as none) -> result[8] as none { result = value; return; }
func named_bytes(value[16] as bytes) -> result[16] as bytes { result = value; return; }
func named_text() -> result[8] as cstr { result = "hello"; return; }
func explicit_text() -> result[8] as cstr { return "hello"; }
func forwarded_text() -> result[8] as cstr { return named_text(); }
func truncated_text() -> result[4] as cstr { return named_text(); }
func enlarged_text() -> result[16] as cstr { return named_text(); }
func selected_text(choose as bool) -> result[8] as cstr {
    return choose ? named_text() : explicit_text()
}
func raw_handle(value as handle) -> result[P] as bytes { return value; }
func explicit_raw(value[16] as bytes) -> result[16] as bytes { return value; }
func explicit_bool(value as handle) -> result as bool { return value; }

func bare_bool(raw as u8) -> result as bool {
    set result as u8
    result = raw
    return
}
func explicit_byte(raw as u8) -> result as bool {
    set result as u8
    result = raw
    return result as bool
}
func shadowed() -> output as i32 {
    output = 42
    if (true) { new output as i32 = 7; return; }
    return
}
func explicit_shadowed() -> output as i32 {
    if (true) { new output as i32 = 7; return output; }
    return 0
}
func recursive(depth as i32) -> output as i32 {
    if (depth == 0) { output = 1; return; }
    output = depth * recursive(depth - 1)
    return
}
func counted(value as i32) -> i32 { evaluations++; return value; }
func ordered() -> (first as i32, second as i32) {
    return counted(41), counted(first + 1)
}
func mixed() -> (i32, second as i32) {
    return counted(20), counted(22)
}
func pair() -> (first as i32, second as i32) {
    first = 11
    second = 12
    return
}

template Pair { x[8] as i64; y[8] as i64; }
impl Pair {
    func sum(self as Pair) -> result as i64 {
        result = self.x + self.y
        return
    }
    func copied(self as Pair) -> result as Pair {
        result.x = self.x
        result.y = self.y
        return
    }
    op + (left as Pair, right as Pair) -> result as Pair {
        result.x = left.x + right.x
        result.y = left.y + right.y
        return
    }
}
template Adjusted { value[4] as i32; }
impl Adjusted {
    op = (target as Adjusted, source as Adjusted) -> Adjusted {
        assignments++
        target.value = source.value + 1
        return target
    }
    func explicit_copy(self as Adjusted) -> result as Adjusted { return self; }
}
func assigned(source as Adjusted) -> result as Adjusted { return source; }
func observe(value as Adjusted) -> i32 { return value.value; }
func direct(source as Adjusted) -> result as Adjusted { result.value = source.value; return; }

func main() -> failures as i32 {
    failures = 0
    new signed8 as i8 = -8
    if (named_i8(signed8) != -8) { failures++; }
    new signed16 as i16 = -16
    if (named_i16(signed16) != -16) { failures++; }
    new signed32 as i32 = -32
    if (named_i32(signed32) != -32) { failures++; }
    new signed64 as i64 = -64
    if (named_i64(signed64) != -64) { failures++; }
    if (named_u8(255) != 255) { failures++; }
    if (named_u16(65535) != 65535) { failures++; }
    if (named_u32(0xFEDCBA98) != 0xFEDCBA98) { failures++; }
    if (named_u64(0xFEDCBA9876543210) != 0xFEDCBA9876543210) { failures++; }
    if (named_f16(to_f16(3)) != to_f16(3)) { failures++; }
    if (named_f32(to_f32(3)) != to_f32(3)) { failures++; }
    if (named_f64(to_f64(3)) != to_f64(3)) { failures++; }
    if (named_f128(to_f128(3)) != to_f128(3)) { failures++; }
    new float16_bits as u16 = 0x8000
    new float32_bits as u32 = 0x7FC12345
    new float64_bits as u64 = 0xFFF8000000000042
    if ((named_f16(float16_bits as f16) as u16) != float16_bits) { failures++; }
    if ((named_f32(float32_bits as f32) as u32) != float32_bits) { failures++; }
    if ((named_f64(float64_bits as f64) as u64) != float64_bits) { failures++; }
    new raw as u64 = 0xFEDCBA9876543210
    if ((named_none(raw as none) as u64) != raw) { failures++; }
    new buffer[16] as bytes = "123456789012345"
    if (memcmp(named_f128(buffer as f128), buffer, 16) != 0) { failures++; }
    if (memcmp(named_bytes(buffer), buffer, 16) != 0) { failures++; }
    if (memcmp(explicit_raw(buffer), buffer, 16) != 0) { failures++; }
    if (strcmp(named_text(), "hello") != 0) { failures++; }
    if (strcmp(explicit_text(), "hello") != 0) { failures++; }
    if (strcmp(forwarded_text(), "hello") != 0) { failures++; }
    if (strcmp(truncated_text(), "hel") != 0) { failures++; }
    if (strcmp(enlarged_text(), "hello") != 0) { failures++; }
    if (strcmp(selected_text(true), "hello") != 0) { failures++; }
    if (strcmp(selected_text(false), "hello") != 0) { failures++; }
    new address as addr = &buffer
    if (named_addr(address) != address) { failures++; }
    printf("views %d\n", failures)

    new bits as u16 = 0
    while (bits < 256) {
        new byte as u8 = to_u8(bits)
        if ((bare_bool(byte) as u8) != byte) { failures++; }
        if ((explicit_byte(byte) as u8) != to_u8(bits != 0)) { failures++; }
        bits++
    }
    if (shadowed() != 42) { failures++; }
    if (explicit_shadowed() != 7) { failures++; }
    if (output != 99) { failures++; }
    if (recursive(5) != 120) { failures++; }
    new first as i32
    new second as i32
    first, second = ordered()
    if (first != 41 || second != 42 || evaluations != 2) { failures++; }
    first, second = mixed()
    if (first != 20 || second != 22 || evaluations != 4) { failures++; }
    first, second = pair()
    if (first != 11 || second != 12) { failures++; }
    printf("scope %d\n", failures)

    new left as Pair
    left.x = 10
    left.y = 11
    new right as Pair
    right.x = 20
    right.y = 1
    new combined as Pair = left + right
    if (combined.sum() != 42) { failures++; }
    if (left.copied().sum() != 21) { failures++; }
    new source as Adjusted
    source.value = 40
    if (observe(assigned(source)) != 41 || assignments != 1) { failures++; }
    if (observe(direct(source)) != 40 || assignments != 1) { failures++; }
    if (observe(source.explicit_copy()) != 41 || assignments != 2) { failures++; }
    new file as handle = fopen("named-return-storage.tmp", "w+")
    if (!file) { return 100; }
    if (named_handle(file) != file) { failures++; }
    if (!explicit_bool(file)) { failures++; }
    if (!raw_handle(file)) { failures++; }
    new closed as i32 = fclose(file)
    if (closed != 0) { failures++; }
    printf("methods %d\n", failures)
    return
}
