$include <stdlib.hsh>

func main() {
    new bits as u32 = 0x3f800000
    new value as f32 = bits as f32
    new rounded as i32 = to_i32(value)
    return rounded - 1
}
