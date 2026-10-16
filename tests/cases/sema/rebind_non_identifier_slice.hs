template Cell { part[8]; }
func main() {
    new x[8]
    new p as addr = &x
    new cell as Cell
    x[0:+4] &= &x
    return 0
}
