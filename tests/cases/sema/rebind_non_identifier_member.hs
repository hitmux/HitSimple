template Cell { part[8]; }
func main() {
    new x[8]
    new p as addr = &x
    new cell as Cell
    cell.part &= &x
    return 0
}
