$include <stdio.hsh>
$include <stdlib.hsh>
$include <string.hsh>

// Standard 4.8, 6.7, 7.6, and 12.1: ternaries copy the selected View's
// bytes, including floating ABI parameters, and evaluate only that branch.
// Boolean reads inspect every bit, so negative zero is true. Raw quiet NaNs
// are only copied and observed as bytes; no arithmetic NaN payload is assumed.
new condition_count as i32 = 0
new left_count as i32 = 0
new right_count as i32 = 0
new evaluation_order as i32 = 0
func reset_evaluation() {
    condition_count = 0
    left_count = 0
    right_count = 0
    evaluation_order = 0
}
func mark_condition(value as bool) -> bool {
    condition_count++
    evaluation_order = evaluation_order * 10 + 1
    return value
}
func check_evaluation(choose as bool) -> i32 {
    if (condition_count != 1) { return 1; }
    if (choose) {
        if (left_count != 1 || right_count != 0 || evaluation_order != 12) { return 1; }
    } else {
        if (left_count != 0 || right_count != 1 || evaluation_order != 13) { return 1; }
    }
    return 0
}
func check_nested_evaluation(choose as bool) -> i32 {
    if (condition_count != 2) { return 1; }
    if (choose) {
        if (left_count != 1 || right_count != 0 || evaluation_order != 112) { return 1; }
    } else {
        if (left_count != 0 || right_count != 1 || evaluation_order != 113) { return 1; }
    }
    return 0
}
func bool_byte(value as bool) -> u8 { return value as u8; }

func identity_f16(value as f16) -> f16 { return value; }
func select_f16(choose as bool, left as f16, right as f16) -> f16 {
    return choose ? left : right;
}
func truth_f16(choose as bool, left as f16, right as f16) -> bool {
    return choose ? left : right;
}
func condition_f16(choose as bool, left as f16, right as f16) -> bool {
    if (choose ? left : right) { return true; }
    return false;
}
func nested_f16(outer as bool, inner as bool, left as f16, right as f16) -> f16 {
    return outer ? (inner ? left : right) : (inner ? right : left);
}
func computed_f16(choose as bool, left as f16, right as f16) -> f16 {
    return choose ? left + left : right + right;
}
func mark_left_f16(value as f16) -> f16 {
    left_count++
    evaluation_order = evaluation_order * 10 + 2
    return value
}
func mark_right_f16(value as f16) -> f16 {
    right_count++
    evaluation_order = evaluation_order * 10 + 3
    return value
}
func effect_nested_f16(outer as bool, inner as bool, left as f16, right as f16) -> f16 {
    return mark_condition(outer) ? (mark_condition(inner) ? mark_left_f16(left) : mark_right_f16(right)) : (mark_condition(inner) ? mark_right_f16(right) : mark_left_f16(left));
}
func effect_return_f16(choose as bool, left as f16, right as f16) -> f16 {
    return mark_condition(choose) ? mark_left_f16(left) : mark_right_f16(right);
}
func effect_truth_f16(choose as bool, left as f16, right as f16) -> bool {
    return mark_condition(choose) ? mark_left_f16(left) : mark_right_f16(right);
}
func effect_condition_f16(choose as bool, left as f16, right as f16) -> bool {
    if (mark_condition(choose) ? mark_left_f16(left) : mark_right_f16(right)) { return true; }
    return false;
}
func repeated_f16() -> i32 {
    // Reuse the condition and computed/call branch temporaries beyond the
    // checked registry's object limit within one function frame.
    new two as f16 = to_f16(2)
    new zero as f16 = to_f16(0)
    new index as i32 = 0
    while ((index < 2048) ? identity_f16(two) + two : zero) {
        index++
    }
    if (index != 2048) { return 1; }
    return 0
}
func check_f16(left as f16, right as f16, left_truth as u8, right_truth as u8) -> i32 {
    new failures as i32 = 0
    // Identity controls isolate ordinary floating ABI parameter/return copies.
    new left_copy as f16 = identity_f16(left)
    new right_copy as f16 = identity_f16(right)
    if (memcmp(left_copy as bytes, left as bytes, 2) != 0) { failures++; }
    if (memcmp(right_copy as bytes, right as bytes, 2) != 0) { failures++; }
    new index as i32 = 0
    while (index < 2) {
        new choose as bool = index != 0
        new expected as f16 = right
        new opposite as f16 = left
        new expected_truth as u8 = right_truth
        if (choose) { expected = left; opposite = right; expected_truth = left_truth; }
        // Direct as uses the validated ternary View length without conversion.
        new direct[2] as bytes = (choose ? left : right) as bytes
        if (memcmp(direct, expected as bytes, 2) != 0) { failures++; }
        direct = (choose ? identity_f16(left) : identity_f16(right)) as bytes
        if (memcmp(direct, expected as bytes, 2) != 0) { failures++; }
        direct = (choose ? (false ? right : left) : (true ? right : left)) as bytes
        if (memcmp(direct, expected as bytes, 2) != 0) { failures++; }
        reset_evaluation()
        direct = (mark_condition(choose) ? mark_left_f16(left) : mark_right_f16(right)) as bytes
        if (memcmp(direct, expected as bytes, 2) != 0) { failures++; }
        failures = failures + check_evaluation(choose)
        new direct_integer as u16 = (choose ? left : right) as u16
        if (memcmp(direct_integer as bytes, expected as bytes, 2) != 0) { failures++; }
        new selected as f16 = choose ? left : right
        if (memcmp(selected as bytes, expected as bytes, 2) != 0) { failures++; }
        selected = choose ? identity_f16(left) : identity_f16(right)
        if (memcmp(selected as bytes, expected as bytes, 2) != 0) { failures++; }
        selected = identity_f16(choose ? left : right)
        if (memcmp(selected as bytes, expected as bytes, 2) != 0) { failures++; }
        selected = select_f16(choose, left, right)
        if (memcmp(selected as bytes, expected as bytes, 2) != 0) { failures++; }
        selected = nested_f16(choose, true, left, right)
        if (memcmp(selected as bytes, expected as bytes, 2) != 0) { failures++; }
        selected = nested_f16(choose, false, left, right)
        if (memcmp(selected as bytes, opposite as bytes, 2) != 0) { failures++; }
        if (bool_byte(truth_f16(choose, left, right)) != expected_truth) { failures++; }
        if (bool_byte(condition_f16(choose, left, right)) != expected_truth) { failures++; }
        new truth as bool = choose ? left : right
        if (bool_byte(truth) != expected_truth) { failures++; }
        // A selected local is an rvalue copy, independent of both source locals.
        left_copy = left
        right_copy = right
        selected = choose ? left_copy : right_copy
        memset(left_copy as bytes, 0, 2)
        memset(right_copy as bytes, 0, 2)
        if (memcmp(selected as bytes, expected as bytes, 2) != 0) { failures++; }
        reset_evaluation()
        selected = mark_condition(choose) ? mark_left_f16(left) : mark_right_f16(right)
        if (memcmp(selected as bytes, expected as bytes, 2) != 0) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        selected = effect_return_f16(choose, left, right)
        if (memcmp(selected as bytes, expected as bytes, 2) != 0) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        if (bool_byte(effect_truth_f16(choose, left, right)) != expected_truth) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        if (bool_byte(effect_condition_f16(choose, left, right)) != expected_truth) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        truth = mark_condition(choose) ? mark_left_f16(left) : mark_right_f16(right)
        if (bool_byte(truth) != expected_truth) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        selected = effect_nested_f16(choose, true, left, right)
        if (memcmp(selected as bytes, expected as bytes, 2) != 0) { failures++; }
        failures = failures + check_nested_evaluation(choose)
        reset_evaluation()
        selected = effect_nested_f16(choose, false, left, right)
        if (memcmp(selected as bytes, opposite as bytes, 2) != 0) { failures++; }
        failures = failures + check_nested_evaluation(!choose)
        index++
    }
    // Arithmetic coverage uses finite, exactly representable values only.
    new two as f16 = to_f16(2)
    new three as f16 = to_f16(3)
    if (computed_f16(true, two, three) != to_f16(4)) { failures++; }
    if (computed_f16(false, two, three) != to_f16(6)) { failures++; }
    new computed as f16 = true ? two + two : three + three
    if (computed != to_f16(4)) { failures++; }
    computed = false ? two + two : three + three
    if (computed != to_f16(6)) { failures++; }
    index = 0
    while (index < 2) {
        new choose as bool = index != 0
        new expected as f16 = to_f16(6)
        if (choose) { expected = to_f16(4); }
        reset_evaluation()
        computed = mark_condition(choose) ? mark_left_f16(two) + two : mark_right_f16(three) + three
        if (computed != expected) { failures++; }
        failures = failures + check_evaluation(choose)
        index++
    }
    return failures
}

func identity_f32(value as f32) -> f32 { return value; }
func select_f32(choose as bool, left as f32, right as f32) -> f32 {
    return choose ? left : right;
}
func truth_f32(choose as bool, left as f32, right as f32) -> bool {
    return choose ? left : right;
}
func condition_f32(choose as bool, left as f32, right as f32) -> bool {
    if (choose ? left : right) { return true; }
    return false;
}
func nested_f32(outer as bool, inner as bool, left as f32, right as f32) -> f32 {
    return outer ? (inner ? left : right) : (inner ? right : left);
}
func computed_f32(choose as bool, left as f32, right as f32) -> f32 {
    return choose ? left + left : right + right;
}
func mark_left_f32(value as f32) -> f32 {
    left_count++
    evaluation_order = evaluation_order * 10 + 2
    return value
}
func mark_right_f32(value as f32) -> f32 {
    right_count++
    evaluation_order = evaluation_order * 10 + 3
    return value
}
func effect_nested_f32(outer as bool, inner as bool, left as f32, right as f32) -> f32 {
    return mark_condition(outer) ? (mark_condition(inner) ? mark_left_f32(left) : mark_right_f32(right)) : (mark_condition(inner) ? mark_right_f32(right) : mark_left_f32(left));
}
func effect_return_f32(choose as bool, left as f32, right as f32) -> f32 {
    return mark_condition(choose) ? mark_left_f32(left) : mark_right_f32(right);
}
func effect_truth_f32(choose as bool, left as f32, right as f32) -> bool {
    return mark_condition(choose) ? mark_left_f32(left) : mark_right_f32(right);
}
func effect_condition_f32(choose as bool, left as f32, right as f32) -> bool {
    if (mark_condition(choose) ? mark_left_f32(left) : mark_right_f32(right)) { return true; }
    return false;
}
func repeated_f32() -> i32 {
    // Reuse the condition and computed/call branch temporaries beyond the
    // checked registry's object limit within one function frame.
    new two as f32 = to_f32(2)
    new zero as f32 = to_f32(0)
    new index as i32 = 0
    while ((index < 2048) ? identity_f32(two) + two : zero) {
        index++
    }
    if (index != 2048) { return 1; }
    return 0
}
func check_f32(left as f32, right as f32, left_truth as u8, right_truth as u8) -> i32 {
    new failures as i32 = 0
    // Identity controls isolate ordinary floating ABI parameter/return copies.
    new left_copy as f32 = identity_f32(left)
    new right_copy as f32 = identity_f32(right)
    if (memcmp(left_copy as bytes, left as bytes, 4) != 0) { failures++; }
    if (memcmp(right_copy as bytes, right as bytes, 4) != 0) { failures++; }
    new index as i32 = 0
    while (index < 2) {
        new choose as bool = index != 0
        new expected as f32 = right
        new opposite as f32 = left
        new expected_truth as u8 = right_truth
        if (choose) { expected = left; opposite = right; expected_truth = left_truth; }
        // Direct as uses the validated ternary View length without conversion.
        new direct[4] as bytes = (choose ? left : right) as bytes
        if (memcmp(direct, expected as bytes, 4) != 0) { failures++; }
        direct = (choose ? identity_f32(left) : identity_f32(right)) as bytes
        if (memcmp(direct, expected as bytes, 4) != 0) { failures++; }
        direct = (choose ? (false ? right : left) : (true ? right : left)) as bytes
        if (memcmp(direct, expected as bytes, 4) != 0) { failures++; }
        reset_evaluation()
        direct = (mark_condition(choose) ? mark_left_f32(left) : mark_right_f32(right)) as bytes
        if (memcmp(direct, expected as bytes, 4) != 0) { failures++; }
        failures = failures + check_evaluation(choose)
        new direct_integer as u32 = (choose ? left : right) as u32
        if (memcmp(direct_integer as bytes, expected as bytes, 4) != 0) { failures++; }
        new selected as f32 = choose ? left : right
        if (memcmp(selected as bytes, expected as bytes, 4) != 0) { failures++; }
        selected = choose ? identity_f32(left) : identity_f32(right)
        if (memcmp(selected as bytes, expected as bytes, 4) != 0) { failures++; }
        selected = identity_f32(choose ? left : right)
        if (memcmp(selected as bytes, expected as bytes, 4) != 0) { failures++; }
        selected = select_f32(choose, left, right)
        if (memcmp(selected as bytes, expected as bytes, 4) != 0) { failures++; }
        selected = nested_f32(choose, true, left, right)
        if (memcmp(selected as bytes, expected as bytes, 4) != 0) { failures++; }
        selected = nested_f32(choose, false, left, right)
        if (memcmp(selected as bytes, opposite as bytes, 4) != 0) { failures++; }
        if (bool_byte(truth_f32(choose, left, right)) != expected_truth) { failures++; }
        if (bool_byte(condition_f32(choose, left, right)) != expected_truth) { failures++; }
        new truth as bool = choose ? left : right
        if (bool_byte(truth) != expected_truth) { failures++; }
        // A selected local is an rvalue copy, independent of both source locals.
        left_copy = left
        right_copy = right
        selected = choose ? left_copy : right_copy
        memset(left_copy as bytes, 0, 4)
        memset(right_copy as bytes, 0, 4)
        if (memcmp(selected as bytes, expected as bytes, 4) != 0) { failures++; }
        reset_evaluation()
        selected = mark_condition(choose) ? mark_left_f32(left) : mark_right_f32(right)
        if (memcmp(selected as bytes, expected as bytes, 4) != 0) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        selected = effect_return_f32(choose, left, right)
        if (memcmp(selected as bytes, expected as bytes, 4) != 0) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        if (bool_byte(effect_truth_f32(choose, left, right)) != expected_truth) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        if (bool_byte(effect_condition_f32(choose, left, right)) != expected_truth) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        truth = mark_condition(choose) ? mark_left_f32(left) : mark_right_f32(right)
        if (bool_byte(truth) != expected_truth) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        selected = effect_nested_f32(choose, true, left, right)
        if (memcmp(selected as bytes, expected as bytes, 4) != 0) { failures++; }
        failures = failures + check_nested_evaluation(choose)
        reset_evaluation()
        selected = effect_nested_f32(choose, false, left, right)
        if (memcmp(selected as bytes, opposite as bytes, 4) != 0) { failures++; }
        failures = failures + check_nested_evaluation(!choose)
        index++
    }
    // Arithmetic coverage uses finite, exactly representable values only.
    new two as f32 = to_f32(2)
    new three as f32 = to_f32(3)
    if (computed_f32(true, two, three) != to_f32(4)) { failures++; }
    if (computed_f32(false, two, three) != to_f32(6)) { failures++; }
    new computed as f32 = true ? two + two : three + three
    if (computed != to_f32(4)) { failures++; }
    computed = false ? two + two : three + three
    if (computed != to_f32(6)) { failures++; }
    index = 0
    while (index < 2) {
        new choose as bool = index != 0
        new expected as f32 = to_f32(6)
        if (choose) { expected = to_f32(4); }
        reset_evaluation()
        computed = mark_condition(choose) ? mark_left_f32(two) + two : mark_right_f32(three) + three
        if (computed != expected) { failures++; }
        failures = failures + check_evaluation(choose)
        index++
    }
    return failures
}

func identity_f64(value as f64) -> f64 { return value; }
func select_f64(choose as bool, left as f64, right as f64) -> f64 {
    return choose ? left : right;
}
func truth_f64(choose as bool, left as f64, right as f64) -> bool {
    return choose ? left : right;
}
func condition_f64(choose as bool, left as f64, right as f64) -> bool {
    if (choose ? left : right) { return true; }
    return false;
}
func nested_f64(outer as bool, inner as bool, left as f64, right as f64) -> f64 {
    return outer ? (inner ? left : right) : (inner ? right : left);
}
func computed_f64(choose as bool, left as f64, right as f64) -> f64 {
    return choose ? left + left : right + right;
}
func mark_left_f64(value as f64) -> f64 {
    left_count++
    evaluation_order = evaluation_order * 10 + 2
    return value
}
func mark_right_f64(value as f64) -> f64 {
    right_count++
    evaluation_order = evaluation_order * 10 + 3
    return value
}
func effect_nested_f64(outer as bool, inner as bool, left as f64, right as f64) -> f64 {
    return mark_condition(outer) ? (mark_condition(inner) ? mark_left_f64(left) : mark_right_f64(right)) : (mark_condition(inner) ? mark_right_f64(right) : mark_left_f64(left));
}
func effect_return_f64(choose as bool, left as f64, right as f64) -> f64 {
    return mark_condition(choose) ? mark_left_f64(left) : mark_right_f64(right);
}
func effect_truth_f64(choose as bool, left as f64, right as f64) -> bool {
    return mark_condition(choose) ? mark_left_f64(left) : mark_right_f64(right);
}
func effect_condition_f64(choose as bool, left as f64, right as f64) -> bool {
    if (mark_condition(choose) ? mark_left_f64(left) : mark_right_f64(right)) { return true; }
    return false;
}
func repeated_f64() -> i32 {
    // Reuse the condition and computed/call branch temporaries beyond the
    // checked registry's object limit within one function frame.
    new two as f64 = to_f64(2)
    new zero as f64 = to_f64(0)
    new index as i32 = 0
    while ((index < 2048) ? identity_f64(two) + two : zero) {
        index++
    }
    if (index != 2048) { return 1; }
    return 0
}
func check_f64(left as f64, right as f64, left_truth as u8, right_truth as u8) -> i32 {
    new failures as i32 = 0
    // Identity controls isolate ordinary floating ABI parameter/return copies.
    new left_copy as f64 = identity_f64(left)
    new right_copy as f64 = identity_f64(right)
    if (memcmp(left_copy as bytes, left as bytes, 8) != 0) { failures++; }
    if (memcmp(right_copy as bytes, right as bytes, 8) != 0) { failures++; }
    new index as i32 = 0
    while (index < 2) {
        new choose as bool = index != 0
        new expected as f64 = right
        new opposite as f64 = left
        new expected_truth as u8 = right_truth
        if (choose) { expected = left; opposite = right; expected_truth = left_truth; }
        // Direct as uses the validated ternary View length without conversion.
        new direct[8] as bytes = (choose ? left : right) as bytes
        if (memcmp(direct, expected as bytes, 8) != 0) { failures++; }
        direct = (choose ? identity_f64(left) : identity_f64(right)) as bytes
        if (memcmp(direct, expected as bytes, 8) != 0) { failures++; }
        direct = (choose ? (false ? right : left) : (true ? right : left)) as bytes
        if (memcmp(direct, expected as bytes, 8) != 0) { failures++; }
        reset_evaluation()
        direct = (mark_condition(choose) ? mark_left_f64(left) : mark_right_f64(right)) as bytes
        if (memcmp(direct, expected as bytes, 8) != 0) { failures++; }
        failures = failures + check_evaluation(choose)
        new direct_integer as u64 = (choose ? left : right) as u64
        if (memcmp(direct_integer as bytes, expected as bytes, 8) != 0) { failures++; }
        new selected as f64 = choose ? left : right
        if (memcmp(selected as bytes, expected as bytes, 8) != 0) { failures++; }
        selected = choose ? identity_f64(left) : identity_f64(right)
        if (memcmp(selected as bytes, expected as bytes, 8) != 0) { failures++; }
        selected = identity_f64(choose ? left : right)
        if (memcmp(selected as bytes, expected as bytes, 8) != 0) { failures++; }
        selected = select_f64(choose, left, right)
        if (memcmp(selected as bytes, expected as bytes, 8) != 0) { failures++; }
        selected = nested_f64(choose, true, left, right)
        if (memcmp(selected as bytes, expected as bytes, 8) != 0) { failures++; }
        selected = nested_f64(choose, false, left, right)
        if (memcmp(selected as bytes, opposite as bytes, 8) != 0) { failures++; }
        if (bool_byte(truth_f64(choose, left, right)) != expected_truth) { failures++; }
        if (bool_byte(condition_f64(choose, left, right)) != expected_truth) { failures++; }
        new truth as bool = choose ? left : right
        if (bool_byte(truth) != expected_truth) { failures++; }
        // A selected local is an rvalue copy, independent of both source locals.
        left_copy = left
        right_copy = right
        selected = choose ? left_copy : right_copy
        memset(left_copy as bytes, 0, 8)
        memset(right_copy as bytes, 0, 8)
        if (memcmp(selected as bytes, expected as bytes, 8) != 0) { failures++; }
        reset_evaluation()
        selected = mark_condition(choose) ? mark_left_f64(left) : mark_right_f64(right)
        if (memcmp(selected as bytes, expected as bytes, 8) != 0) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        selected = effect_return_f64(choose, left, right)
        if (memcmp(selected as bytes, expected as bytes, 8) != 0) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        if (bool_byte(effect_truth_f64(choose, left, right)) != expected_truth) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        if (bool_byte(effect_condition_f64(choose, left, right)) != expected_truth) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        truth = mark_condition(choose) ? mark_left_f64(left) : mark_right_f64(right)
        if (bool_byte(truth) != expected_truth) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        selected = effect_nested_f64(choose, true, left, right)
        if (memcmp(selected as bytes, expected as bytes, 8) != 0) { failures++; }
        failures = failures + check_nested_evaluation(choose)
        reset_evaluation()
        selected = effect_nested_f64(choose, false, left, right)
        if (memcmp(selected as bytes, opposite as bytes, 8) != 0) { failures++; }
        failures = failures + check_nested_evaluation(!choose)
        index++
    }
    // Arithmetic coverage uses finite, exactly representable values only.
    new two as f64 = to_f64(2)
    new three as f64 = to_f64(3)
    if (computed_f64(true, two, three) != to_f64(4)) { failures++; }
    if (computed_f64(false, two, three) != to_f64(6)) { failures++; }
    new computed as f64 = true ? two + two : three + three
    if (computed != to_f64(4)) { failures++; }
    computed = false ? two + two : three + three
    if (computed != to_f64(6)) { failures++; }
    index = 0
    while (index < 2) {
        new choose as bool = index != 0
        new expected as f64 = to_f64(6)
        if (choose) { expected = to_f64(4); }
        reset_evaluation()
        computed = mark_condition(choose) ? mark_left_f64(two) + two : mark_right_f64(three) + three
        if (computed != expected) { failures++; }
        failures = failures + check_evaluation(choose)
        index++
    }
    return failures
}

func identity_f128(value as f128) -> f128 { return value; }
func select_f128(choose as bool, left as f128, right as f128) -> f128 {
    return choose ? left : right;
}
func truth_f128(choose as bool, left as f128, right as f128) -> bool {
    return choose ? left : right;
}
func condition_f128(choose as bool, left as f128, right as f128) -> bool {
    if (choose ? left : right) { return true; }
    return false;
}
func nested_f128(outer as bool, inner as bool, left as f128, right as f128) -> f128 {
    return outer ? (inner ? left : right) : (inner ? right : left);
}
func computed_f128(choose as bool, left as f128, right as f128) -> f128 {
    return choose ? left + left : right + right;
}
func mark_left_f128(value as f128) -> f128 {
    left_count++
    evaluation_order = evaluation_order * 10 + 2
    return value
}
func mark_right_f128(value as f128) -> f128 {
    right_count++
    evaluation_order = evaluation_order * 10 + 3
    return value
}
func effect_nested_f128(outer as bool, inner as bool, left as f128, right as f128) -> f128 {
    return mark_condition(outer) ? (mark_condition(inner) ? mark_left_f128(left) : mark_right_f128(right)) : (mark_condition(inner) ? mark_right_f128(right) : mark_left_f128(left));
}
func effect_return_f128(choose as bool, left as f128, right as f128) -> f128 {
    return mark_condition(choose) ? mark_left_f128(left) : mark_right_f128(right);
}
func effect_truth_f128(choose as bool, left as f128, right as f128) -> bool {
    return mark_condition(choose) ? mark_left_f128(left) : mark_right_f128(right);
}
func effect_condition_f128(choose as bool, left as f128, right as f128) -> bool {
    if (mark_condition(choose) ? mark_left_f128(left) : mark_right_f128(right)) { return true; }
    return false;
}
func repeated_f128() -> i32 {
    // Reuse the condition and computed/call branch temporaries beyond the
    // checked registry's object limit within one function frame.
    new two as f128 = to_f128(2)
    new zero as f128 = to_f128(0)
    new index as i32 = 0
    while ((index < 2048) ? identity_f128(two) + two : zero) {
        index++
    }
    if (index != 2048) { return 1; }
    return 0
}
func check_f128(left as f128, right as f128, left_truth as u8, right_truth as u8) -> i32 {
    new failures as i32 = 0
    // Identity controls isolate ordinary floating ABI parameter/return copies.
    new left_copy as f128 = identity_f128(left)
    new right_copy as f128 = identity_f128(right)
    if (memcmp(left_copy as bytes, left as bytes, 16) != 0) { failures++; }
    if (memcmp(right_copy as bytes, right as bytes, 16) != 0) { failures++; }
    new index as i32 = 0
    while (index < 2) {
        new choose as bool = index != 0
        new expected as f128 = right
        new opposite as f128 = left
        new expected_truth as u8 = right_truth
        if (choose) { expected = left; opposite = right; expected_truth = left_truth; }
        // Direct as uses the validated ternary View length without conversion.
        new direct[16] as bytes = (choose ? left : right) as bytes
        if (memcmp(direct, expected as bytes, 16) != 0) { failures++; }
        direct = (choose ? identity_f128(left) : identity_f128(right)) as bytes
        if (memcmp(direct, expected as bytes, 16) != 0) { failures++; }
        direct = (choose ? (false ? right : left) : (true ? right : left)) as bytes
        if (memcmp(direct, expected as bytes, 16) != 0) { failures++; }
        reset_evaluation()
        direct = (mark_condition(choose) ? mark_left_f128(left) : mark_right_f128(right)) as bytes
        if (memcmp(direct, expected as bytes, 16) != 0) { failures++; }
        failures = failures + check_evaluation(choose)
        new selected as f128 = choose ? left : right
        if (memcmp(selected as bytes, expected as bytes, 16) != 0) { failures++; }
        selected = choose ? identity_f128(left) : identity_f128(right)
        if (memcmp(selected as bytes, expected as bytes, 16) != 0) { failures++; }
        selected = identity_f128(choose ? left : right)
        if (memcmp(selected as bytes, expected as bytes, 16) != 0) { failures++; }
        selected = select_f128(choose, left, right)
        if (memcmp(selected as bytes, expected as bytes, 16) != 0) { failures++; }
        selected = nested_f128(choose, true, left, right)
        if (memcmp(selected as bytes, expected as bytes, 16) != 0) { failures++; }
        selected = nested_f128(choose, false, left, right)
        if (memcmp(selected as bytes, opposite as bytes, 16) != 0) { failures++; }
        if (bool_byte(truth_f128(choose, left, right)) != expected_truth) { failures++; }
        if (bool_byte(condition_f128(choose, left, right)) != expected_truth) { failures++; }
        new truth as bool = choose ? left : right
        if (bool_byte(truth) != expected_truth) { failures++; }
        // A selected local is an rvalue copy, independent of both source locals.
        left_copy = left
        right_copy = right
        selected = choose ? left_copy : right_copy
        memset(left_copy as bytes, 0, 16)
        memset(right_copy as bytes, 0, 16)
        if (memcmp(selected as bytes, expected as bytes, 16) != 0) { failures++; }
        reset_evaluation()
        selected = mark_condition(choose) ? mark_left_f128(left) : mark_right_f128(right)
        if (memcmp(selected as bytes, expected as bytes, 16) != 0) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        selected = effect_return_f128(choose, left, right)
        if (memcmp(selected as bytes, expected as bytes, 16) != 0) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        if (bool_byte(effect_truth_f128(choose, left, right)) != expected_truth) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        if (bool_byte(effect_condition_f128(choose, left, right)) != expected_truth) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        truth = mark_condition(choose) ? mark_left_f128(left) : mark_right_f128(right)
        if (bool_byte(truth) != expected_truth) { failures++; }
        failures = failures + check_evaluation(choose)
        reset_evaluation()
        selected = effect_nested_f128(choose, true, left, right)
        if (memcmp(selected as bytes, expected as bytes, 16) != 0) { failures++; }
        failures = failures + check_nested_evaluation(choose)
        reset_evaluation()
        selected = effect_nested_f128(choose, false, left, right)
        if (memcmp(selected as bytes, opposite as bytes, 16) != 0) { failures++; }
        failures = failures + check_nested_evaluation(!choose)
        index++
    }
    // Arithmetic coverage uses finite, exactly representable values only.
    new two as f128 = to_f128(2)
    new three as f128 = to_f128(3)
    if (computed_f128(true, two, three) != to_f128(4)) { failures++; }
    if (computed_f128(false, two, three) != to_f128(6)) { failures++; }
    new computed as f128 = true ? two + two : three + three
    if (computed != to_f128(4)) { failures++; }
    computed = false ? two + two : three + three
    if (computed != to_f128(6)) { failures++; }
    index = 0
    while (index < 2) {
        new choose as bool = index != 0
        new expected as f128 = to_f128(6)
        if (choose) { expected = to_f128(4); }
        reset_evaluation()
        computed = mark_condition(choose) ? mark_left_f128(two) + two : mark_right_f128(three) + three
        if (computed != expected) { failures++; }
        failures = failures + check_evaluation(choose)
        index++
    }
    return failures
}

func integer_choice(choose as bool, left as i64, right as i64) -> i64 {
    return choose ? left : right
}
func raw_choice(choose as bool, left[16] as bytes, right[16] as bytes) -> [16] as bytes {
    return choose ? left : right
}
func main() -> i32 {
    new failures16 as i32 = 0
    new failures32 as i32 = 0
    new failures64 as i32 = 0
    new failures128 as i32 = 0
    new controls as i32 = 0
    controls = controls + repeated_f16()
    controls = controls + repeated_f32()
    controls = controls + repeated_f64()
    controls = controls + repeated_f128()
    new zero16 as u16 = 0
    new negative_zero16 as u16 = 0x8000
    new nan16 as u16 = 0xfe53
    new finite16 as u16 = 0x3e01
    new negative16 as u16 = 0xc123
    failures16 = failures16 + check_f16(zero16 as f16, negative_zero16 as f16, 0, 1)
    failures16 = failures16 + check_f16(nan16 as f16, finite16 as f16, 1, 1)
    failures16 = failures16 + check_f16(finite16 as f16, negative16 as f16, 1, 1)
    new zero32 as u32 = 0
    new negative_zero32 as u32 = 0x80000000
    new nan32 as u32 = 0xffc01234
    new finite32 as u32 = 0x3fc01234
    new negative32 as u32 = 0xc0205678
    failures32 = failures32 + check_f32(zero32 as f32, negative_zero32 as f32, 0, 1)
    failures32 = failures32 + check_f32(nan32 as f32, finite32 as f32, 1, 1)
    failures32 = failures32 + check_f32(finite32 as f32, negative32 as f32, 1, 1)
    new zero64 as u64 = 0
    new negative_zero64 as u64 = 0x8000000000000000
    new nan64 as u64 = 0xfff8000000001234
    new finite64 as u64 = 0x3ff8000000001234
    new negative64 as u64 = 0xc004000000005678
    failures64 = failures64 + check_f64(zero64 as f64, negative_zero64 as f64, 0, 1)
    failures64 = failures64 + check_f64(nan64 as f64, finite64 as f64, 1, 1)
    failures64 = failures64 + check_f64(finite64 as f64, negative64 as f64, 1, 1)
    // These binary128 byte patterns follow the supported little-endian targets.
    new zero128[16] as bytes
    memset(zero128, 0, 16)
    new negative_zero128[16] as bytes = zero128
    negative_zero128[15] = 0x80
    new nan128[16] as bytes = zero128
    nan128[0] = 0x34
    nan128[1] = 0x12
    nan128[13] = 0x80
    nan128[14] = 0xff
    nan128[15] = 0xff
    new finite128[16] as bytes = zero128
    finite128[0] = 0x34
    finite128[1] = 0x12
    finite128[13] = 0x80
    finite128[14] = 0xff
    finite128[15] = 0x3f
    new negative128[16] as bytes = zero128
    negative128[0] = 0x78
    negative128[1] = 0x56
    negative128[13] = 0x40
    negative128[15] = 0xc0
    failures128 = failures128 + check_f128(zero128 as f128, negative_zero128 as f128, 0, 1)
    failures128 = failures128 + check_f128(nan128 as f128, finite128 as f128, 1, 1)
    failures128 = failures128 + check_f128(finite128 as f128, negative128 as f128, 1, 1)
    new integer_left as i64 = -7
    new integer_right as i64 = 19
    if (integer_choice(true, integer_left, integer_right) != -7) { controls++; }
    if (integer_choice(false, integer_left, integer_right) != 19) { controls++; }
    new raw[16] as bytes = raw_choice(true, nan128, finite128)
    if (memcmp(raw as bytes, nan128 as bytes, 16) != 0) { controls++; }
    raw = raw_choice(false, nan128, finite128)
    if (memcmp(raw as bytes, finite128 as bytes, 16) != 0) { controls++; }
    printf("f16 %d\nf32 %d\nf64 %d\nf128 %d\ncontrols %d\n", failures16, failures32, failures64, failures128, controls)
    if (failures16 != 0 || failures32 != 0 || failures64 != 0 || failures128 != 0 || controls != 0) { return 1; }
    return 0
}
