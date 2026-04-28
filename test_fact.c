#include <stdio.h>
extern long long factorial(long long n);
int main() {
    printf("factorial(5) = %lld\n", factorial(5));
    return 0;
}
