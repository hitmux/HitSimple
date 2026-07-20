$include <stdio.hsh>
$include <stdlib.hsh>

func main() {
    new value as u64 = 4294967296
    new fixed[3] as bytes
    fixed[2] = 1
    new length as u64 = 3
    if (!value) {
        printf("false\n")
    } else {
        printf("true\n")
    }
    if (fixed) {
        printf("fixed\n")
    }
    if (resize_bytes(fixed, length)) {
        printf("dynamic\n")
    }
    return 0
}
