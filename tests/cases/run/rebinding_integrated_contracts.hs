$include <stdio.hsh>
$include <stdlib.hsh>
$include <string.hsh>
$include <ctype.hsh>

new calls as i32 = 0
new returned[8] as cstr = "ret\0tail"
func address_once(value as addr) -> addr { calls++; return value; }
func named() -> out[4] as cstr {
    new local[8] as cstr = "old"
    return (local &= address_once(&returned))
}
func anonymous() -> [4] as cstr {
    new local[8] as cstr = "old"
    return (local &= address_once(&returned))
}
func rebound_return() -> out[8] as cstr { out &= &returned; return; }

func main() -> i32 {
    new failures as i32 = 0
    new name[8] as cstr = "old"
    new target[8] as cstr = "new\0tail"
    new saved as addr = &name
    new copied[5] as cstr = (name &= address_once(&target))
    target = "later"
    if (copied != "new" || name != "later" || calls != 1) { failures++; }
    if (copied[3] != 0 || copied[4] != 0 || [1]*saved != 'o') { failures++; }
    new frozen[8] as cstr
    (name &=), frozen = &returned, name
    if (frozen != "later" || name != "ret" || &name != &returned) { failures++; }
    copied = named()
    if (copied != "ret" || calls != 2) { failures++; }
    copied = anonymous()
    if (copied != "ret" || calls != 3) { failures++; }
    copied = rebound_return()
    if (copied != "ret") { failures++; }

    new raw[8] as bytes = 'A1234567'
    new wide as f64 = 0.0
    wide &= &raw
    if (!is_upper(wide) || !is_xdigit(wide) || !is_print(wide) || !is_graph(wide)) { failures++; }
    if (is_lower(wide) || is_blank(wide) || is_control(wide) || is_punct(wide)) { failures++; }
    new punctuation[8] as bytes = '!1234567'
    new snapshot as f64 = (wide &= &punctuation)
    wide &= &raw
    if (!is_punct(snapshot) || !is_upper(wide)) { failures++; }
    new tiny[1] as bytes = 'a'
    wide &= &tiny
    if (!is_lower(wide) || is_upper(wide) || !is_xdigit(wide)) { failures++; }
    if (!is_print(wide) || !is_graph(wide) || is_punct(wide)) { failures++; }
    if (is_blank(wide) || is_control(wide)) { failures++; }
    new blank[1] as bytes = '\t'
    wide &= &blank
    if (!is_blank(wide) || !is_control(wide)) { failures++; }
    printf("integrated rebinding %d\n", failures)
    return failures
}
