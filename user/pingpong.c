#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char const *argv[]) {
    int fds[2];
    char buf;

    pipe(fds);


    if (fork() == 0) {
        // Child receives 'ping'
        read(fds[0], &buf, 1);
        printf("%d: received ping\n", getpid());

        // Child sends 'pong'
        write(fds[1], &buf, 1);
        exit(0);
    } else {
        // Parent sends 'ping'
        write(fds[1], &buf, 1);

        // Parent receives 'pong'
        read(fds[0], &buf, 1);
        printf("%d: received pong\n", getpid());
        exit(0);
    }

    return 0;
}
