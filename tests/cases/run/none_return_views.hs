$include <stdio.hsh>

// Standard 4.4 and 12.2: spelling `as none` explicitly must not change a
// statically sized return View. Exercise all call-expression construction paths.
func one(value[1] as none) -> [1] as none { return value; }
func two(value[2] as none) -> [2] as none { return value; }
func four(value[4] as none) -> [4] as none { return value; }
func eight(value[8] as none) -> [8] as none { return value; }
func bare(value[8] as none) -> [8] { return value; }
func named(value[8] as none) -> out[8] as none { return value; }
func named_bare(value[8] as none) -> out[8] { return value; }
func typed(value as u64) -> u64 { return value; }
func pair(value[8] as none) -> ([8] as none, out[8] as none) {
    return value, value
}

new evaluations as i32 = 0
func counted(value[8] as none) -> [8] as none {
    evaluations++
    return value
}
func selected(choose as bool, left[8] as none, right[8] as none) -> [8] as bytes {
    return eight(choose ? counted(left) : counted(right)) as bytes
}

template Packet { value[8] as u64; }
impl Packet {
    func bare_read(self as Packet) -> [8] { return self.value; }
    func read(self as Packet) -> [8] as none { return self.value; }
    op + (left as Packet, right as Packet) -> [8] as none {
        return left.value %8d+ right.value
    }
}
func sum(left as Packet, right as Packet) -> [8] as none {
    return left + right
}

func main() -> i32 {
    new failures as i32 = 0
    new b1 as u8 = 0x80
    new b2 as u16 = 0xFEDC
    new b4 as u32 = 0xFEDCBA98
    new b8 as u64 = 0xFEDCBA9876543210
    if ((one(b1 as none) as u8) != b1) { failures++; }
    if ((two(b2 as none) as u16) != b2) { failures++; }
    if ((four(b4 as none) as u32) != b4) { failures++; }
    if ((eight(b8 as none) as u64) != b8) { failures++; }
    if ((eight(eight(b8 as none)) as u64) != b8) { failures++; }
    if ((bare(b8 as none) as u64) != (eight(b8 as none) as u64)) { failures++; }
    if ((named(b8 as none) as u64) != b8) { failures++; }
    if ((named_bare(b8 as none) as u64) != b8) { failures++; }
    if (typed(b8) != b8) { failures++; }
    if ((eight(20) + eight(22)) != 42) { failures++; }
    new first[8]
    new second[8]
    first, second = pair(b8 as none)
    if ((first as u64) != b8) { failures++; }
    if ((second as u64) != b8) { failures++; }
    printf("ordinary %d\n", failures)

    new p as Packet
    new q as Packet
    p.value = 20
    q.value = 22
    if ((p.read() as u64) != p.value) { failures++; }
    if ((p.bare_read() as u64) != p.value) { failures++; }
    if ((sum(p, q) as u64) != 42) { failures++; }
    printf("methods %d\n", failures)

    if ((selected(true, 11, 22) as u64) != 11) { failures++; }
    if (evaluations != 1) { failures++; }
    if ((selected(false, 11, 22) as u64) != 22) { failures++; }
    if (evaluations != 2) { failures++; }
    printf("evaluation %d\n", failures)
    return failures
}
