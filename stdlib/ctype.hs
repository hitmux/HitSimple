func __hs_stdlib_ctype_is_digit(ch as i32) -> bool {
    return ch >= 48 && ch <= 57
}

func __hs_stdlib_ctype_is_alpha(ch as i32) -> bool {
    return (ch >= 65 && ch <= 90) || (ch >= 97 && ch <= 122)
}

func __hs_stdlib_ctype_is_alnum(ch as i32) -> bool {
    return __hs_stdlib_ctype_is_digit(ch) || __hs_stdlib_ctype_is_alpha(ch)
}

func __hs_stdlib_ctype_is_space(ch as i32) -> bool {
    return (ch >= 9 && ch <= 13) || ch == 32
}

func __hs_stdlib_ctype_to_upper(ch as i32) -> u8 {
    new result as u8
    if (ch >= 97 && ch <= 122) {
        result %d= ch - 32
    } else {
        result %d= ch
    }
    return result
}

func __hs_stdlib_ctype_to_lower(ch as i32) -> u8 {
    new result as u8
    if (ch >= 65 && ch <= 90) {
        result %d= ch + 32
    } else {
        result %d= ch
    }
    return result
}
