$include "included.hsi"
$define ADD(a, b) a + b
$define ENABLE_MAIN 1
$if ENABLE_MAIN
func main() {
    new x[4] = ADD(INCLUDED_VALUE, 2)
    return 0
}
$else
$error "main disabled"
$endif
