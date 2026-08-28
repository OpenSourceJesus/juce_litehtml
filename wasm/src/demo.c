/* A small module bundled with the browser, to exercise the AOT path.
 *
 * Compiled to WebAssembly by Crust at build time, then translated back to C
 * and linked in -- so this file is the input to the whole round trip that
 * tools/wasm_aot.py automates.
 *
 * Deliberately plain: no imports, no memory beyond what locals need, and
 * exports whose signatures the registry can call directly.
 */

int add(int a, int b) { return a + b; }

int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

/* Something with a loop and a comparison, so the module is not entirely
   arithmetic that a peephole could fold away. */
int sum_to(int n) {
    int s = 0;
    int i;
    for (i = 1; i <= n; i++) s += i;
    return s;
}

int gcd(int a, int b) {
    while (b != 0) {
        int t = a % b;
        a = b;
        b = t;
    }
    return a < 0 ? -a : a;
}

int main(void) { return 0; }
