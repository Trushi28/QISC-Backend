#include <stdio.h>
#include <stdint.h>

extern int64_t conditional_value(int64_t cond);

int main(void) {
    int64_t r1 = conditional_value(0);
    int64_t r2 = conditional_value(1);
    int ok = (r1 == 5 && r2 == 10);
    printf("conditional(0)=%lld conditional(1)=%lld %s\n",
           (long long)r1, (long long)r2,
           ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
