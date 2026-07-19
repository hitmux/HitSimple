$include <ctype.hsh>

extern ctype_reference_score() -> i32

func main() {
    if (is_digit(48)) {
        return ctype_reference_score() - 2
    }
    return 1
}
