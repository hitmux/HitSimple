$include <stdio.hsh>

func main() {
    new condition as bool = true
    new left as u64 = 1
    new right as u64 = 2
    new result as u64 = condition ? left : right
    printf("%d\n", result)
    return 0
}
