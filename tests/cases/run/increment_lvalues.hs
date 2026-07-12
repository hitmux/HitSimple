template Cell {
    value[1] as i8
}

func main() {
    new cell as Cell
    new bytes[4]
    new target[1]
    new ptr[8]
    ptr &= &target
    cell.value = 10
    bytes[0] = 20
    bytes[1:2] = 30
    target = 40
    cell.value++
    bytes[0]++
    bytes[1:2]--
    [1]*ptr++
    return cell.value - 11 + bytes[0] - 21 + bytes[1] - 29 + target - 41
}
