$include <stdlib.hsh>
$include <stdio.hsh>
new global_name as i32 = 1
new global_target as i32 = 5
new global_alias[4] &= &global_target
new original_global as addr = &global_name
new target_address as addr = &global_target
func rebind_global() { global_name &= target_address; }
func observe_global() -> i32 { return global_name; }
func static_binding(first as bool) -> i32 {
    static persistent as i32 = 3
    if (first) { persistent &= &global_target; }
    persistent = persistent + 1
    return persistent
}
func parameter_binding(value as i32, target as addr) -> i32 {
    new original as addr = &value
    value &= target
    value = value + 2
    return value + [4]*original
}
func named_result(target as addr) -> (result as i32) {
    result &= target
    result = result + 3
    return
}
func automatic_binding() -> i32 {
    new first as i32 = 13
    new second as i32 = 14
    new count as i32 = 0
    new sum as i32 = 0
    while (count < 3) {
        new local as i32 = 2
        sum = sum + local
        local &= &first
        if (count == 1) { local &= &second; }
        sum = sum + local
        count++
    }
    return sum
}
func initialized_static() -> i32 {
    static alias[4] &= &global_target
    alias = alias + 1
    return alias
}
func main() -> i32 {
    new failures as i32 = 0
    if (global_alias != 5 || &global_alias != &global_target) { failures++; }
    rebind_global()
    if (observe_global() != 5 || &global_name != &global_target) { failures++; }
    global_name = 7
    if (global_target != 7 || [4]*original_global != 1) { failures++; }
    if (static_binding(true) != 8 || static_binding(false) != 9) { failures++; }
    if (parameter_binding(11, &global_target) != 22 || global_target != 11) { failures++; }
    if (named_result(&global_target) != 14 || global_target != 14) { failures++; }
    if (automatic_binding() != 46) { failures++; }
    if (initialized_static() != 15 || initialized_static() != 16) { failures++; }
    try { throw 7; } catch (caught as i32) {
        new saved_error as addr = &caught
        caught &= &global_target
        caught = 17
        if (global_target != 17 || [4]*saved_error != 7) { failures++; }
    }
    new heap as addr = calloc(4, 1)
    if (true) {
        new local as i32 = 1
        local &= heap
        local = 39
    }
    // Scope cleanup releases only local's definition, never the dynamic target.
    if ([4]*heap != 39) { failures++; }
    [4]*heap = 40
    free(heap)
    printf("rebind lifetimes %d\n", failures)
    return failures
}
