#include "minicio.h"

int fib(int n) {
    int a;
    int b;
    int i;
    int temp;
    
    a = 0;
    b = 1;
    
    if (n == 0) return a;
    if (n == 1) return b;
    
    i = 2;
    for (i = 2; i <= n; i = i + 1) {
        temp = a + b;
        a = b;
        b = temp;
    }
    return b;
}

int main() {
    int result;
    result = fib(10); /* 55 */
    putint(result);
    putnewline();
    return 0;
}
