#include <stdio.h>
#include <stdlib.h>
#include <time.h>   /* clock */

#include "code_inspector.h"

int main(int argc, char *argv[])
{
	clock_t start = clock();

    if (argc < 2) {
        printf("missing target code...\n");
    }
    code_inspector_input(argv[1]);

    clock_t end = clock();
	printf("\n %lfs\n", (double)(end - start) / CLOCKS_PER_SEC);

    return 0;
}
