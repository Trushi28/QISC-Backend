#include <stdio.h>
extern double float_test(void);
int main() {
    printf("float_test returned %f\n", float_test());
    return 0;
}
