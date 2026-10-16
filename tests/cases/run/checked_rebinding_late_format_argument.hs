$include <stdio.hsh>
new backing[8] as cstr = "%d"
func change() -> i32 {
  backing = "%dxx"
  return 42
}
func main() -> i32 {
  new small[3] as cstr
  small &= &backing
  printf(small, change())
  return 0
}
