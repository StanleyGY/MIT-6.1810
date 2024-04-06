#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"

static char*
fmtname(char *path)
{
    // Assume that `path` has a slash
    static char buf[MAXPATH + 1];
    char *p;

    // Find first character after last slash
    for(p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    strcpy(buf, p);
    return buf;
}

static void
find(char *path, const char *target) {
    int fd;
    struct stat st;
    struct dirent de;

    char buf[MAXPATH + 1];
    char *p;

    fd = open(path, O_RDONLY);
    fstat(fd, &st);

    switch (st.type) {
    case T_DEVICE:
    case T_FILE:
        if (strcmp(fmtname(path), target) == 0) {
            printf("%s\n", path);
        }
        break;
    case T_DIR:
        while(read(fd, &de, sizeof(de)) == sizeof(de)) {
            // Read each entry from the directory
            if (de.inum == 0) {
                continue;
            }
            if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {
                continue;
            }

            // Copy the full path for each entry
            strcpy(buf, path);
            p = buf + strlen(buf);
            *(p++) = '/';
            strcpy(p++, de.name);

            // Recursively search
            find(buf, target);
        }
        break;
    }
    close(fd);
}

int
main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(2, "Usage: find dir file");
        exit(1);
    }

    find(argv[1], argv[2]);
    exit(0);
}
