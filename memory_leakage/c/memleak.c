#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

int main()
{
    void *p1 = malloc(5);
    void *p2 = malloc(10);
    void *p3 = malloc(15);

    free(p1);
    free(p2);

    return 0;
}