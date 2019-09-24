#include <stdio.h>
#include <stdlib.h>
#include <time.h>   /* clock */
#include <getopt.h> /* getopt_long */

#include "code_inspector.h"

void show_help(void)
{
    printf("Options:\n"
           "-f <file>   Specify analysis code file\n"
           "--help      Display this information\n");
}

int main(int argc, char *argv[])
{
	clock_t start = clock();
    const char *code_path = NULL;
    int opt;

    static struct option ops[] = {
        { "file", required_argument, NULL, 'f' },
        { "help", no_argument, NULL, 'h' }
    };
    while ((opt = getopt_long(argc, argv, "f:h", ops, NULL)) != -1)
    {
        switch(opt)
        {
        case 'f':
            code_path = optarg;
            break;
        case 'h':
            show_help();
            return 0;
        default:
            fprintf(stderr, "Try: %s --help\n", argv[0]);
            return -1;
        }
    }
    if (NULL == code_path)
    {
        printf("Missing target code. Try: %s --help\n", argv[0]);
        return -1;
    }
    code_inspector_input(code_path);

    clock_t end = clock();
	printf("\n %lfs\n", (double)(end - start) / CLOCKS_PER_SEC);

    return 0;
}
