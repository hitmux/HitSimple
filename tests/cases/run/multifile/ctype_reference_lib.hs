$include <ctype.hsh>

func ctype_reference_score() -> i32 {
    if (is_alpha(65) && is_alnum(57)) {
        return 2
    }
    return 0
}
