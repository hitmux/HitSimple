$include <ctype.hsh>

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
    return 0
}
