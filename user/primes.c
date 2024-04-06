#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


static void
sieve_and_print(int initial_left_fds) {
    int left_fds[2] = { initial_left_fds };
    int right_fds[2];
    int curr, prime;
    int hasForked = 0, hasPrintedPrime = 0;

    // Read from the left neighbor
    while (read(left_fds[0], &curr, sizeof(int)) != 0) {

        // The first number read is always a prime
        if (!hasPrintedPrime) {
            hasPrintedPrime = 1;
            prime = curr;

            printf("prime %d\n", curr);
            continue;
        }

        // `curr` has a factor of `prime`
        if (curr % prime == 0) {
            continue;
        }

        // Created a child process and feed numbers into the child process
        if (!hasForked) {
            pipe(right_fds);

            if (fork() > 0) {
                hasForked = 1;
                close(right_fds[0]);
            } else {
                hasForked = 0;
                hasPrintedPrime = 0;

                left_fds[0] = right_fds[0];
                close(right_fds[1]);
                continue;
            }
        }

        write(right_fds[1], &curr, sizeof(int));
    }

    close(left_fds[0]);

    if (hasForked) {
        close(right_fds[1]);
    }
}

int
main(int argc, char *argv[]) {
    int fds[2];
    int i;

    pipe(fds);

    if (fork() == 0) {
        close(fds[1]);
        sieve_and_print(fds[0]);
        close(fds[0]);
    } else {
        close(fds[0]);
        for (i = 2; i <= 35; i ++) {
            write(fds[1], &i, sizeof(int));
        }
        close(fds[1]);
    }

    wait(0);
    exit(0);
}
