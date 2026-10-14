// Standard 14.7 operates on the lowest byte, including negative and wide inputs.
func __hs_stdlib_ctype_is_digit(ch as i32) -> bool {
    new byte as i32 = ch & 0xff
    return byte >= 48 && byte <= 57
}

func __hs_stdlib_ctype_is_alpha(ch as i32) -> bool {
    new byte as i32 = ch & 0xff
    return (byte >= 65 && byte <= 90) || (byte >= 97 && byte <= 122)
}

func __hs_stdlib_ctype_is_alnum(ch as i32) -> bool {
    return __hs_stdlib_ctype_is_digit(ch) || __hs_stdlib_ctype_is_alpha(ch)
}

func __hs_stdlib_ctype_is_space(ch as i32) -> bool {
    new byte as i32 = ch & 0xff
    return (byte >= 9 && byte <= 13) || byte == 32
}

func __hs_stdlib_ctype_to_upper(ch as i32) -> u8 {
    new byte as i32 = ch & 0xff
    new result as u8
    if (byte >= 97 && byte <= 122) {
        result %d= byte - 32
    } else {
        result %d= byte
    }
    return result
}

func __hs_stdlib_ctype_to_lower(ch as i32) -> u8 {
    new byte as i32 = ch & 0xff
    new result as u8
    if (byte >= 65 && byte <= 90) {
        result %d= byte + 32
    } else {
        result %d= byte
    }
    return result
}
