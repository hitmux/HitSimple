$include <ctype.hsh>
$include <stdio.hsh>
$include <stdlib.hsh>

// Canonical bool values must remain 0/1 through every consumer. ASCII inputs
// isolate this regression from the separate ctype low-byte normalization suite.
new global_flag as bool = false

template BoolSlot {
    value[1] as bool
}

// Parameter passing copies the bool View without a normalizing assignment.
// Reinterpreting that copy observes the producer's actual byte representation.
func bool_byte(value as bool) -> u8 {
    return value as u8
}

func forward_bool(value as bool) -> bool {
    return value
}

func return_digit(ch as i32) -> bool {
    return is_digit(ch)
}

func check_digit(ch as i32, expected as bool) -> i32 {
    new yes as bool = true
    new no as bool = false
    new expected_byte as u8 = to_u8(expected)
    if (bool_byte(is_digit(ch)) != expected_byte) {
        return 1
    }
    // Both operand orders, both equality operators, and literal/stored bools.
    if ((is_digit(ch) == true) != expected || (true == is_digit(ch)) != expected || (is_digit(ch) != false) != expected || (false != is_digit(ch)) != expected) {
        return 2
    }
    if ((is_digit(ch) == false) == expected || (false == is_digit(ch)) == expected || (is_digit(ch) != true) == expected || (true != is_digit(ch)) == expected) {
        return 3
    }
    if ((is_digit(ch) == yes) != expected || (yes == is_digit(ch)) != expected || (is_digit(ch) != no) != expected || (no != is_digit(ch)) != expected) {
        return 4
    }
    if ((is_digit(ch) == no) == expected || (no == is_digit(ch)) == expected || (is_digit(ch) != yes) == expected || (yes != is_digit(ch)) == expected) {
        return 5
    }
    if (((is_digit(ch) == true) == true) != expected || (false == (is_digit(ch) != true)) != expected) {
        return 6
    }
    if (bool_byte(forward_bool(is_digit(ch))) != expected_byte || bool_byte(return_digit(ch)) != expected_byte) {
        return 7
    }
    new choose as bool = ch != 0
    if (bool_byte(choose ? is_digit(ch) : false) != expected_byte || bool_byte(!choose ? true : is_digit(ch)) != expected_byte) {
        return 8
    }
    new local_flag as bool = is_digit(ch)
    global_flag = is_digit(ch)
    new slot as BoolSlot
    slot.value = is_digit(ch)
    if (bool_byte(local_flag) != expected_byte || bool_byte(global_flag) != expected_byte || bool_byte(slot.value) != expected_byte) {
        return 9
    }
    new signed_value as i32 = is_digit(ch)
    new unsigned_value as u32 = is_digit(ch)
    new signed_explicit as i64
    new unsigned_explicit as u64
    signed_explicit %d= is_digit(ch)
    unsigned_explicit %d= is_digit(ch)
    if (signed_value != to_i32(expected) || unsigned_value != to_u32(expected) || signed_explicit != to_i64(expected) || unsigned_explicit != to_u64(expected)) {
        return 10
    }
    if (to_i8(is_digit(ch)) != to_i8(expected) || to_u8(is_digit(ch)) != expected_byte || to_i16(is_digit(ch)) != to_i16(expected) || to_u16(is_digit(ch)) != to_u16(expected)) {
        return 11
    }
    if (to_i32(is_digit(ch)) != to_i32(expected) || to_u32(is_digit(ch)) != to_u32(expected) || to_i64(is_digit(ch)) != to_i64(expected) || to_u64(is_digit(ch)) != to_u64(expected)) {
        return 12
    }
    return 0
}

func return_alpha(ch as i32) -> bool {
    return is_alpha(ch)
}

func check_alpha(ch as i32, expected as bool) -> i32 {
    new yes as bool = true
    new no as bool = false
    new expected_byte as u8 = to_u8(expected)
    if (bool_byte(is_alpha(ch)) != expected_byte) {
        return 1
    }
    // Both operand orders, both equality operators, and literal/stored bools.
    if ((is_alpha(ch) == true) != expected || (true == is_alpha(ch)) != expected || (is_alpha(ch) != false) != expected || (false != is_alpha(ch)) != expected) {
        return 2
    }
    if ((is_alpha(ch) == false) == expected || (false == is_alpha(ch)) == expected || (is_alpha(ch) != true) == expected || (true != is_alpha(ch)) == expected) {
        return 3
    }
    if ((is_alpha(ch) == yes) != expected || (yes == is_alpha(ch)) != expected || (is_alpha(ch) != no) != expected || (no != is_alpha(ch)) != expected) {
        return 4
    }
    if ((is_alpha(ch) == no) == expected || (no == is_alpha(ch)) == expected || (is_alpha(ch) != yes) == expected || (yes != is_alpha(ch)) == expected) {
        return 5
    }
    if (((is_alpha(ch) == true) == true) != expected || (false == (is_alpha(ch) != true)) != expected) {
        return 6
    }
    if (bool_byte(forward_bool(is_alpha(ch))) != expected_byte || bool_byte(return_alpha(ch)) != expected_byte) {
        return 7
    }
    new choose as bool = ch != 0
    if (bool_byte(choose ? is_alpha(ch) : false) != expected_byte || bool_byte(!choose ? true : is_alpha(ch)) != expected_byte) {
        return 8
    }
    new local_flag as bool = is_alpha(ch)
    global_flag = is_alpha(ch)
    new slot as BoolSlot
    slot.value = is_alpha(ch)
    if (bool_byte(local_flag) != expected_byte || bool_byte(global_flag) != expected_byte || bool_byte(slot.value) != expected_byte) {
        return 9
    }
    new signed_value as i32 = is_alpha(ch)
    new unsigned_value as u32 = is_alpha(ch)
    new signed_explicit as i64
    new unsigned_explicit as u64
    signed_explicit %d= is_alpha(ch)
    unsigned_explicit %d= is_alpha(ch)
    if (signed_value != to_i32(expected) || unsigned_value != to_u32(expected) || signed_explicit != to_i64(expected) || unsigned_explicit != to_u64(expected)) {
        return 10
    }
    if (to_i8(is_alpha(ch)) != to_i8(expected) || to_u8(is_alpha(ch)) != expected_byte || to_i16(is_alpha(ch)) != to_i16(expected) || to_u16(is_alpha(ch)) != to_u16(expected)) {
        return 11
    }
    if (to_i32(is_alpha(ch)) != to_i32(expected) || to_u32(is_alpha(ch)) != to_u32(expected) || to_i64(is_alpha(ch)) != to_i64(expected) || to_u64(is_alpha(ch)) != to_u64(expected)) {
        return 12
    }
    return 0
}

func return_alnum(ch as i32) -> bool {
    return is_alnum(ch)
}

func check_alnum(ch as i32, expected as bool) -> i32 {
    new yes as bool = true
    new no as bool = false
    new expected_byte as u8 = to_u8(expected)
    if (bool_byte(is_alnum(ch)) != expected_byte) {
        return 1
    }
    // Both operand orders, both equality operators, and literal/stored bools.
    if ((is_alnum(ch) == true) != expected || (true == is_alnum(ch)) != expected || (is_alnum(ch) != false) != expected || (false != is_alnum(ch)) != expected) {
        return 2
    }
    if ((is_alnum(ch) == false) == expected || (false == is_alnum(ch)) == expected || (is_alnum(ch) != true) == expected || (true != is_alnum(ch)) == expected) {
        return 3
    }
    if ((is_alnum(ch) == yes) != expected || (yes == is_alnum(ch)) != expected || (is_alnum(ch) != no) != expected || (no != is_alnum(ch)) != expected) {
        return 4
    }
    if ((is_alnum(ch) == no) == expected || (no == is_alnum(ch)) == expected || (is_alnum(ch) != yes) == expected || (yes != is_alnum(ch)) == expected) {
        return 5
    }
    if (((is_alnum(ch) == true) == true) != expected || (false == (is_alnum(ch) != true)) != expected) {
        return 6
    }
    if (bool_byte(forward_bool(is_alnum(ch))) != expected_byte || bool_byte(return_alnum(ch)) != expected_byte) {
        return 7
    }
    new choose as bool = ch != 0
    if (bool_byte(choose ? is_alnum(ch) : false) != expected_byte || bool_byte(!choose ? true : is_alnum(ch)) != expected_byte) {
        return 8
    }
    new local_flag as bool = is_alnum(ch)
    global_flag = is_alnum(ch)
    new slot as BoolSlot
    slot.value = is_alnum(ch)
    if (bool_byte(local_flag) != expected_byte || bool_byte(global_flag) != expected_byte || bool_byte(slot.value) != expected_byte) {
        return 9
    }
    new signed_value as i32 = is_alnum(ch)
    new unsigned_value as u32 = is_alnum(ch)
    new signed_explicit as i64
    new unsigned_explicit as u64
    signed_explicit %d= is_alnum(ch)
    unsigned_explicit %d= is_alnum(ch)
    if (signed_value != to_i32(expected) || unsigned_value != to_u32(expected) || signed_explicit != to_i64(expected) || unsigned_explicit != to_u64(expected)) {
        return 10
    }
    if (to_i8(is_alnum(ch)) != to_i8(expected) || to_u8(is_alnum(ch)) != expected_byte || to_i16(is_alnum(ch)) != to_i16(expected) || to_u16(is_alnum(ch)) != to_u16(expected)) {
        return 11
    }
    if (to_i32(is_alnum(ch)) != to_i32(expected) || to_u32(is_alnum(ch)) != to_u32(expected) || to_i64(is_alnum(ch)) != to_i64(expected) || to_u64(is_alnum(ch)) != to_u64(expected)) {
        return 12
    }
    return 0
}

func return_space(ch as i32) -> bool {
    return is_space(ch)
}

func check_space(ch as i32, expected as bool) -> i32 {
    new yes as bool = true
    new no as bool = false
    new expected_byte as u8 = to_u8(expected)
    if (bool_byte(is_space(ch)) != expected_byte) {
        return 1
    }
    // Both operand orders, both equality operators, and literal/stored bools.
    if ((is_space(ch) == true) != expected || (true == is_space(ch)) != expected || (is_space(ch) != false) != expected || (false != is_space(ch)) != expected) {
        return 2
    }
    if ((is_space(ch) == false) == expected || (false == is_space(ch)) == expected || (is_space(ch) != true) == expected || (true != is_space(ch)) == expected) {
        return 3
    }
    if ((is_space(ch) == yes) != expected || (yes == is_space(ch)) != expected || (is_space(ch) != no) != expected || (no != is_space(ch)) != expected) {
        return 4
    }
    if ((is_space(ch) == no) == expected || (no == is_space(ch)) == expected || (is_space(ch) != yes) == expected || (yes != is_space(ch)) == expected) {
        return 5
    }
    if (((is_space(ch) == true) == true) != expected || (false == (is_space(ch) != true)) != expected) {
        return 6
    }
    if (bool_byte(forward_bool(is_space(ch))) != expected_byte || bool_byte(return_space(ch)) != expected_byte) {
        return 7
    }
    new choose as bool = ch != 0
    if (bool_byte(choose ? is_space(ch) : false) != expected_byte || bool_byte(!choose ? true : is_space(ch)) != expected_byte) {
        return 8
    }
    new local_flag as bool = is_space(ch)
    global_flag = is_space(ch)
    new slot as BoolSlot
    slot.value = is_space(ch)
    if (bool_byte(local_flag) != expected_byte || bool_byte(global_flag) != expected_byte || bool_byte(slot.value) != expected_byte) {
        return 9
    }
    new signed_value as i32 = is_space(ch)
    new unsigned_value as u32 = is_space(ch)
    new signed_explicit as i64
    new unsigned_explicit as u64
    signed_explicit %d= is_space(ch)
    unsigned_explicit %d= is_space(ch)
    if (signed_value != to_i32(expected) || unsigned_value != to_u32(expected) || signed_explicit != to_i64(expected) || unsigned_explicit != to_u64(expected)) {
        return 10
    }
    if (to_i8(is_space(ch)) != to_i8(expected) || to_u8(is_space(ch)) != expected_byte || to_i16(is_space(ch)) != to_i16(expected) || to_u16(is_space(ch)) != to_u16(expected)) {
        return 11
    }
    if (to_i32(is_space(ch)) != to_i32(expected) || to_u32(is_space(ch)) != to_u32(expected) || to_i64(is_space(ch)) != to_i64(expected) || to_u64(is_space(ch)) != to_u64(expected)) {
        return 12
    }
    return 0
}

func check_controls() -> i32 {
    // An arbitrary bool View is not a predicate result: as and argument copying
    // must preserve its bytes. Do not impose equality/conversion normalization.
    new raw as u8 = 2
    if (bool_byte(raw as bool) != 2) {
        return 1
    }
    new one as i32 = 1
    new two as i32 = 2
    if (bool_byte(one < two) != 1 || bool_byte(one == two) != 0 || bool_byte(one != two) != 1) {
        return 2
    }
    if (bool_byte(true && true) != 1 || bool_byte(true && false) != 0 || bool_byte(false || true) != 1 || bool_byte(false || false) != 0) {
        return 3
    }
    if (bool_byte(!false) != 1 || bool_byte(!true) != 0) {
        return 4
    }
    // Keep ordinary signed and unsigned integer widening behavior unchanged.
    new signed_byte as i8 = -1
    new unsigned_byte as u8 = 255
    new signed_wide as i64 = signed_byte
    new unsigned_wide as u64 = unsigned_byte
    new signed_explicit as i64
    new unsigned_explicit as u64
    signed_explicit %d= signed_byte
    unsigned_explicit %d= unsigned_byte
    if (signed_wide != -1 || signed_explicit != -1 || to_i64(signed_byte) != -1) {
        return 5
    }
    if (unsigned_wide != 255 || unsigned_explicit != 255 || to_u64(unsigned_byte) != 255) {
        return 6
    }
    return 0
}

func main() -> i32 {
    printf("digit %d %d\n", check_digit(53, true), check_digit(65, false))
    printf("alpha %d %d\n", check_alpha(65, true), check_alpha(53, false))
    printf("alnum %d %d\n", check_alnum(53, true), check_alnum(33, false))
    printf("space %d %d\n", check_space(32, true), check_space(65, false))
    printf("controls %d\n", check_controls())
    return 0
}
