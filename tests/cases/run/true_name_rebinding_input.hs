$include <stdio.hsh>
func main() -> i32 {
    new count as i32 = 0
    new count_target as i32 = 0
    new value as i32 = 0
    new value_target as i32 = 0
    new real as f64 = 0.0
    new real_target as f64 = 0.0
    new text[8] as cstr = ""
    new text_target[8] as cstr = ""
    count &= &count_target
    value &= &value_target
    real &= &real_target
    text &= &text_target
    count, value, real, text = scanf("%d %f %s")
    if (count_target != 3 || value_target != 37 || real_target != 2.5 || text_target != "ok") { return 1; }
    printf("rebind input 0\n")
    return 0
}
