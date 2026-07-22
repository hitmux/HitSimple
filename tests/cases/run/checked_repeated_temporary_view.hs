$include <stdlib.hsh>

func main() -> i32 {
    new ready[1] as bytes = 'A'
    new calls[8] as u64 = 0

    while (calls < 2048 && byte_swap(ready)) {
        calls++
    }

    return 0
}
