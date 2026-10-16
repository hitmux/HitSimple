$include <stdio.hsh>
$include <stdlib.hsh>
new calls as i32 = 0
func pointer_once(p as addr) -> addr { calls++; return p; }
func pointers(p as addr, q as addr) -> (addr, addr) { calls++; return p, q; }
func main() -> i32 {
    new failures as i32 = 0
    new x as i32 = 11
    new next as i32 = 22
    new out as i32 = 0
    (x &=), out = pointer_once(&next), x
    if (x != 22 || out != 11 || calls != 1) { failures++; }

    new first as i32 = 13
    new second as i32 = 17
    new p as addr = &first
    new next_p as addr = &second
    new saved_p as addr
    (p &=), saved_p = &next_p, p
    if (p != &second || saved_p != &first || &p != &next_p) { failures++; }

    new float_name as f32 = 1.5
    new float_next as f32 = 2.5
    new float_out as f32
    (float_name &=), float_out = &float_next, float_name
    new float_expected as f32 = 1.5
    if (float_name != float_next || float_out != float_expected) { failures++; }

    new text[5] as cstr = "old"
    new next_text[5] as cstr = "next"
    new text_out[5] as cstr
    (text &=), text_out = &next_text, text
    if (text != "next" || text_out != "old") { failures++; }

    new large[16]
    new target as i32 = 71
    new address as addr = &target
    large = address
    new narrow as i32 = 0
    narrow &= large
    if (narrow != 71 || &narrow != &target) { failures++; }
    narrow &= true ? large : large
    if (narrow != 71 || &narrow != &target) { failures++; }
    new declaration[4] &= &target
    declaration = 72
    if (target != 72) { failures++; }
    new pair_first[3] = 11
    new pair_second as i32 = 21
    new pair_target_first[3] = 41
    new pair_target_second as i32 = 51
    (pair_first &=), (pair_second &=) = pointers(&pair_target_first, &pair_target_second)
    pair_first = 43
    pair_second = 53
    if (pair_target_first != 43 || pair_target_second != 53 || calls != 2) { failures++; }

    new discarded_source as i32 = 1
    discarded_source &= 0
    (discarded_source &=), _ = &target, discarded_source
    if (discarded_source != 72) { failures++; }
    printf("rebind captures %d\n", failures)
    return failures
}
