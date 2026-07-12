$include <stdio.hsh>

func main() {
    new sum[4]
    sum = 0
    for (new i[4] = 0; i < 4; i++) {
        if (i == 2) {
            continue
        }
        sum = sum + i
    }
    try {
        throw 7
    } catch (err[4]) {
        sum = sum + err
    }
    goto done
    sum = 99
    done: printf("%d\n", sum)
    return 0
}
