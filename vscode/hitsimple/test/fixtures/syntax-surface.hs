$ define LIMIT 1_000

struct Pair {
    left[4]
    right[4]
}

template Vec2 {
    x[8] as f64
    y[8] as f64
}

impl Vec2 {
    op + (self as Vec2, other as Vec2) -> as Vec2 {
        new result as Vec2
        result.x %4f= self.x %4f+ other.x
        return result
    }

    func scale(mut self as Vec2, factor as f64) -> as Vec2 {
        return self
    }
}

extern errno[4] as i32

func main() {
    new binary[4] = 0b1010_0101
    new octal[4] = 0o755
    new hexadecimal[4] = 0xFF_00
    new decimal[4] = 1_000
    new fraction[8] = .5
    new exponent[8] = 1.5e+2
    decimal %d+= 1
    decimal = decimal %100d+ 1
    decimal = decimal %d<< 1
    decimal = decimal %d& 0xff
    fraction %4f= .5
    fraction = fraction %f** 2.0
    set decimal as u32
    new text[16] %s= "line\n"
    return 0
}
