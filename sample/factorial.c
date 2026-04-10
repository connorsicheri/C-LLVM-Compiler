#include "minicio.h"

int factorial(int n) {
    if (n < 2) return 1;
    return n * factorial(n - 1);
}

int main() {
    int f;
    f = factorial(6); /* 720 */
    putint(f);
    putnewline();
    return 0;
}
