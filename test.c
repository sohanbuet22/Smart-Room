#include <stdio.h>
#include <stdlib.h>
int main()
{
    int *x = malloc(10 * sizeof(int));
    printf("%lu\n", sizeof(x));
    free(x);
    return 0;
}