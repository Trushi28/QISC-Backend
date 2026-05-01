#include <stdio.h>
#include <stdint.h>
extern int64_t sub_test(void);
extern int64_t mul_test(void);
extern int64_t div_test(void);
extern int64_t cmp_test(void);
int main() {
    printf("sub_test() = %lld\n", sub_test());
    printf("mul_test() = %lld\n", mul_test());
    printf("div_test() = %lld\n", div_test());
    printf("cmp_test() = %lld\n", cmp_test());
    return 0;
}
