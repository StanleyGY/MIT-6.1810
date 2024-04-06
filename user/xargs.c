#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define MAXARGLEN 32

int
main(int argc, char *argv[]) {
    int i, j;
    char *p;
    char buf[MAXARGLEN];
    char *new_args[MAXARG];

    while (1) {
        gets(buf, MAXARGLEN);

        // No more lines from standard input
        if (buf[0] == '\0') {
            break;
        }

        // Copy the command line args
        i = 0;
        for (j = 1; j < argc; j ++, i ++) {
            new_args[i] = argv[j];
        }

        // Parse and copy the args from standard input
        p = buf;
        while (1) {
            new_args[i] = p;
            i ++;

            while (*p != ' ' && *p != '\n') {
                p ++;
            }
            if (*p == '\n') {
                *p = '\0';
                break;
            } else {
                *p = '\0';
                p ++;
            }
        }

        if (fork() == 0) {
            exec(argv[1], new_args);
        } else {
            wait(0);
        }
    }

    return 0;
}
