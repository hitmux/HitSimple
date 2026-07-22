func main() {
    new x[1]
    x %d= 0
start:
    while (x < 4) {
        x %d+= 1
        if (x == 2) {
            continue
        } else {
            x %d+= 1
        }
        if (x > 3) {
            break
        }
    }
    for (x %d= 0; x < 2; x++) {
        x %d+= 1
    }
    try {
        throw x
    } catch (error[1]) {
        return error - 4
    }
    goto start
}
