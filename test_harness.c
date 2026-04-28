#include <stdio.h>
#include <stdint.h>
#include <string.h>
extern int64_t compute_value(void);
extern int64_t add_two(int64_t a, int64_t b);
extern int64_t factorial(int64_t n);
extern int64_t sub_test(void);
extern int64_t mul_test(void);
extern int64_t div_test(void);
extern int64_t cmp_test(void);
extern double float_test(void);
extern const char* string_test(void);
int main(void) {
int passed = 0;
if(compute_value() == 5) { passed++; printf("[PASS] compute_value = 5\n"); } else printf("[FAIL] compute_value\n");
if(add_two(3, 4) == 7) { passed++; printf("[PASS] add_two = 7\n"); } else printf("[FAIL] add_two\n");
if(factorial(5) == 120) { passed++; printf("[PASS] factorial = 120\n"); } else printf("[FAIL] factorial\n");
if(sub_test() == 7) { passed++; printf("[PASS] sub_test = 7\n"); } else printf("[FAIL] sub_test\n");
if(mul_test() == 42) { passed++; printf("[PASS] mul_test = 42\n"); } else printf("[FAIL] mul_test\n");
if(div_test() == 25) { passed++; printf("[PASS] div_test = 25\n"); } else printf("[FAIL] div_test\n");
if(cmp_test() == 1) { passed++; printf("[PASS] cmp_test = 1\n"); } else printf("[FAIL] cmp_test\n");
if(float_test() == 5.0) { passed++; printf("[PASS] float_test = 5.0\n"); } else printf("[FAIL] float_test\n");
if(strcmp(string_test(), "Hello QISC") == 0) { passed++; printf("[PASS] string_test\n"); } else printf("[FAIL] string_test\n");
printf("[PASS] %d/9 execution tests\n", passed);
return passed == 9 ? 0 : 1;
}
