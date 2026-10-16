$include <stdio.hsh>
$include <stdlib.hsh>
$include <string.hsh>
$include <ctype.hsh>

template Record { word[4] as u32; text[8] as u64; tail[4] as u32; }
template Box { value[4] as i32; }
impl Box {
    op = (dst as Box, src as Box) -> Box {
        dst.value = src.value + 1
        return dst
    }
}
func last_offset() -> u64 { return 2; }
func consume(a as i32, b as i32) -> i32 { return a * 100 + b; }
func replace(p as addr) -> i32 { [4]*p = 8; return 3; }
func repair_string(p as addr) -> result[2] as cstr {
    [1]*p = 0
    return ""
}
func main() -> i32 {
    new failures as i32 = 0
    new a as u8 = 1
    new b as u8 = 9
    new saved as addr = &a
    a &= &b
    if (a != 9 || &a != &b || saved == &a) { failures++; }
    a = 7
    if (b != 7 || [1]*saved != 1) { failures++; }

    new three[3] = 11
    new other_three[3] = 23
    new three_definition as addr = &three
    three &= &other_three
    if (length(three) != 3 || three != 23) { failures++; }
    three = 41
    if (other_three != 41 || [3]*three_definition != 11) { failures++; }

    if (&three[last_offset()] != &other_three[2] || length(three[last_offset()]) != 1) { failures++; }
    three[last_offset()] = 3
    if (other_three[2] != 3) { failures++; }
    new bytes[40] as bytes
    memset(bytes, 0, 40)
    new four as i32 = 4
    four &= &bytes[1:+4]
    four = 42
    if (four != 42 || [4]*(&bytes[1:+4]) != 42) { failures++; }
    four[0] = 3
    if (bytes[1] != 3) { failures++; }
    four[1:+2] = 17
    if (four[1:+2] != 17 || &four[2] != &bytes[3]) { failures++; }

    new eight as u64 = 8
    new other_eight as u64 = 18
    eight &= &other_eight
    eight = 19
    if (other_eight != 19) { failures++; }
    new sixteen_seed as i32 = 16
    new other_sixteen_seed as i32 = 26
    new sixteen[16] = sixteen_seed
    new other_sixteen[16] = other_sixteen_seed
    sixteen &= &other_sixteen
    if (sixteen != 26 || length(sixteen) != 16) { failures++; }
    new thirty_two[32] as bytes
    new other_thirty_two[32] as bytes
    memset(other_thirty_two, 5, 32)
    thirty_two &= &other_thirty_two
    new source_thirty_two[32] as bytes
    memset(source_thirty_two, 6, 32)
    thirty_two = source_thirty_two
    if (memcmp(other_thirty_two, source_thirty_two, 32) != 0) { failures++; }
    thirty_two[31] = 9
    if (other_thirty_two[31] != 9 || length(thirty_two) != 32) { failures++; }

    new float_name as f64 = 1.0
    new float_target as f64 = 2.5
    float_name &= &float_target
    float_name = 3.5
    if (float_name != 3.5 || float_target != 3.5) { failures++; }
    new half_name as f16 = 1.0
    new half_target as f16 = 2.5
    half_name &= &half_target
    half_name = 3.5
    new half_expected as f16 = 3.5
    if (half_target != half_expected) { failures++; }
    new quad_name as f128 = 1.0
    new quad_target as f128 = 2.5
    quad_name &= &quad_target
    quad_name = 3.5
    new quad_expected as f128 = 3.5
    if (quad_target != quad_expected) { failures++; }
    new boolean_name as bool = false
    new boolean_target as bool = false
    boolean_name &= &boolean_target
    boolean_name = 17
    if (!boolean_target || !boolean_name) { failures++; }
    new string_name[8] as cstr = "old"
    new string_target[8] as cstr = "new"
    string_name &= &string_target
    string_name = "yes"
    new string_copy[8] as cstr = string_name
    if (string_target != "yes" || string_copy != "yes") { failures++; }

    new record as Record
    new other_record as Record
    set record.text as cstr
    record &= &other_record
    record.word = 17
    record.text = "ok"
    if (other_record.word != 17 || record.text != "ok") { failures++; }
    if (&record.tail != &other_record.tail) { failures++; }
    new box as Box
    new other_box as Box
    new source_box as Box
    source_box.value = 33
    box &= &other_box
    box = source_box
    if (other_box.value != 34) { failures++; }

    new snapshot_name as i32 = 1
    new snapshot_target as i32 = 7
    new observed as i32 = consume((snapshot_name &= &snapshot_target), replace(&snapshot_target))
    if (observed != 703 || snapshot_name != 8) { failures++; }

    // Rebinding is legal without immediately reading the declared region.
    new invalid[32]
    invalid &= 0
    if (length(invalid) != 32 || &invalid != 0) { failures++; }
    new signed_small as i8 = -1
    invalid &= signed_small
    new signed_address as addr = signed_small
    if (&invalid != signed_address) { failures++; }
    invalid &= true ? signed_small : signed_small
    if (&invalid != signed_address) { failures++; }
    new unsigned_small as u8 = 255
    invalid &= unsigned_small
    new unsigned_address as addr = unsigned_small
    if (&invalid != unsigned_address) { failures++; }
    invalid &= false ? unsigned_small : unsigned_small
    if (&invalid != unsigned_address) { failures++; }

    new oversized_string[32] as cstr
    new tiny_string[2] as cstr = "z"
    oversized_string &= &tiny_string
    new copied_string[8] as cstr = oversized_string
    if (copied_string != "z" || strlen(oversized_string) != 1 || strcmp(oversized_string, "z") != 0) { failures++; }

    // The larger declaration can locate a smaller valid member region.
    new partial as Record
    new small as u32 = 31
    partial &= &small
    partial.word = 32
    if (partial.word != 32 || small != 32) { failures++; }
    new low_byte_name[16] as bytes
    new low_byte_target as u8 = 97
    low_byte_name &= &low_byte_target
    new low_byte_output[2] as bytes
    memset(low_byte_output, low_byte_name, 2)
    if (low_byte_output[0] != 97 || low_byte_output[1] != 97) { failures++; }
    if (!is_alpha(low_byte_name) || !is_alnum(low_byte_name) || is_digit(low_byte_name) || is_space(low_byte_name)) { failures++; }
    if (to_upper(low_byte_name) != 65 || to_lower(low_byte_name) != 97) { failures++; }
    new searched[3] as cstr = "ab"
    if (strchr(searched, low_byte_name) != &searched) { failures++; }
    // File I/O consumes the addr View's bytes, not the addressed payload.
    new pointer_content as addr = &other_sixteen
    new pointer_name as addr
    pointer_name &= &pointer_content
    new restored_pointer as addr = 0
    new restored_name as addr
    restored_name &= &restored_pointer
    new file as handle = fopen("rebinding-pointer-view.dat", "wb+")
    if (fwrite(pointer_name, 1, length(pointer_name), file) != length(pointer_name)) { failures++; }
    if (fseek(file, 0, 0) != 0) { failures++; }
    if (fread(restored_name, 1, length(restored_name), file) != length(restored_name)) { failures++; }
    if (restored_pointer != pointer_content) { failures++; }
    if (fclose(file) != 0) { failures++; }

    // Argument formation does not scan bytes before a later argument repairs them.
    new repaired_raw[2] as bytes = 'AB'
    set repaired_raw as cstr
    new repaired_alias[2] as cstr
    repaired_alias &= &repaired_raw
    if (strcmp(repaired_alias, repair_string(&repaired_raw)) != 0) { failures++; }
    new comparison_raw[2] as bytes = 'CD'
    set comparison_raw as cstr
    repaired_alias &= &comparison_raw
    if (repaired_alias != repair_string(&comparison_raw)) { failures++; }
    printf("true rebinding %d\n", failures)
    return failures
}
