#include <stdio.h>
#include <stdint.h>
extern int64_t compute_value(void);
extern int64_t add_two(int64_t a, int64_t b);
extern int64_t factorial(int64_t n);
int main(void) {
int64_t r1 = compute_value();
int64_t r2 = add_two(3, 4);
int64_t r3 = factorial(5);
printf("compute_value = %lld (expected 5)\n", (long long)r1);
printf("add_two(3,4)  = %lld (expected 7)\n", (long long)r2);
printf("factorial(5)  = %lld (expected 120)\n", (long long)r3);
if(r1==5 && r2==7 && r3==120) { printf("[PASS] compute_value = 5\n[PASS] add_two(3,4) = 7\n[PASS] factorial(5) = 120\nALL TESTS PASSED\n"); return 0; } else { printf("FAIL\n"); return 1; }
}
