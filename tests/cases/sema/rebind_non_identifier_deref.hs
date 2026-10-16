template Cell { part[8]; }
func main() {
    new x[8]
    new p as addr = &x
    new cell as Cell
    [8]*p &= &x
    return 0
}
