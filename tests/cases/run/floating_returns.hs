$include <stdlib.hsh>

func add_one(value as f64) -> f64 {
    return value %f+ 1.0
}

func pair(value as f64) -> (first as f64, second as f64) {
    return value, add_one(value)
}

func main() {
    new first as f64
    new second as f64
    first, second = pair(40.0)
    new first_value[4] = to_i32(first)
    new second_value[4] = to_i32(second)
    if (first_value != 40) {
        return 1
    }
    if (second_value != 41) {
        return 2
    }
    return 0
}
