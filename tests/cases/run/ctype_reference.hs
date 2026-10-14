$include <ctype.hsh>

// Use an explicit expected byte, independent of the provider's normalization.
func check_low_byte(ch as i32, byte as i32) -> i32 {
    new digit as bool = byte >= 48 && byte <= 57
    new upper as bool = byte >= 65 && byte <= 90
    new lower as bool = byte >= 97 && byte <= 122
    new alpha as bool = upper || lower
    new alnum as bool = digit || alpha
    new space as bool = (byte >= 9 && byte <= 13) || byte == 32
    if ((digit && !is_digit(ch)) || (!digit && is_digit(ch))) {
        return 1
    }
    if ((alpha && !is_alpha(ch)) || (!alpha && is_alpha(ch))) {
        return 2
    }
    if ((alnum && !is_alnum(ch)) || (!alnum && is_alnum(ch))) {
        return 3
    }
    if ((space && !is_space(ch)) || (!space && is_space(ch))) {
        return 4
    }
    new expected_upper as u8
    expected_upper %d= byte
    if (lower) {
        expected_upper %d= byte - 32
    }
    if (to_upper(ch) != expected_upper) {
        return 5
    }
    new expected_lower as u8
    expected_lower %d= byte
    if (upper) {
        expected_lower %d= byte + 32
    }
    if (to_lower(ch) != expected_lower) {
        return 6
    }
    return 0
}

func main() {
    new nul as u8 = 0x00
    new delete as u8 = 0x7f
    new non_ascii as u8 = 0x80
    if (is_digit(nul) || is_digit(47) || !is_digit(48) || !is_digit(57) || is_digit(58) || is_digit(non_ascii)) {
        return 1
    }
    if (!is_alpha(65) || !is_alpha(90) || !is_alpha(97) || !is_alpha(122) || is_alpha(nul) || is_alpha(delete) || is_alpha(non_ascii)) {
        return 2
    }
    if (!is_alnum(48) || !is_alnum(57) || !is_alnum(65) || !is_alnum(90) || !is_alnum(97) || !is_alnum(122) || is_alnum(64) || is_alnum(delete) || is_alnum(non_ascii)) {
        return 3
    }
    if (!is_space(9) || !is_space(13) || !is_space(32) || is_space(nul) || is_space(delete) || is_space(non_ascii)) {
        return 4
    }
    if (to_upper(97) != 65) {
        return 5
    }
    if (to_upper(122) != 90) {
        return 6
    }
    if (to_upper(65) != 65) {
        return 7
    }
    if (to_upper(nul) != nul) {
        return 8
    }
    if (to_upper(delete) != delete) {
        return 9
    }
    if (to_upper(non_ascii) != non_ascii) {
        return 10
    }
    if (to_lower(65) != 97) {
        return 11
    }
    if (to_lower(90) != 122) {
        return 12
    }
    if (to_lower(97) != 97) {
        return 13
    }
    if (to_lower(nul) != nul) {
        return 14
    }
    if (to_lower(delete) != delete) {
        return 15
    }
    if (to_lower(non_ascii) != non_ascii) {
        return 16
    }
    // Exhaust all 256 byte values with zero, positive, negative, and i32-edge
    // prefixes. This includes 0x130, 0x161, -208, and -159 explicitly by value.
    new byte as i32 = 0
    while (byte < 256) {
        if (check_low_byte(byte, byte) != 0) {
            return 17
        }
        new positive as i32 = byte + 256
        if (check_low_byte(positive, byte) != 0) {
            return 18
        }
        new negative as i32 = byte - 256
        if (check_low_byte(negative, byte) != 0) {
            return 19
        }
        new high as i32 = byte + 0x123400
        if (check_low_byte(high, byte) != 0) {
            return 20
        }
        new maximum as i32 = byte + 0x7fffff00
        if (check_low_byte(maximum, byte) != 0) {
            return 21
        }
        new minimum as i32 = -2147483648
        minimum = minimum + byte
        if (check_low_byte(minimum, byte) != 0) {
            return 22
        }
        byte++
    }
    return 0
}
