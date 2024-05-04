#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *fmtname(char *path) {
    char *p;
    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;
    return p;
}
void find(char *path, char *name) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "cannot open\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "cannot stat %s\n", path);
        return;
    }

    switch (st.type) {
    case T_FILE:
        if (!strcmp(fmtname(path), name))
            printf("%s\n", path);
        break;
    case T_DIR:
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
            printf("path too long\n");
            break;
        }
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        while (read(fd, &de, sizeof(de)) == sizeof(de)) {
            if (de.inum == 0)
                continue;
            memmove(p, de.name, sizeof(de.name));
            p[sizeof(de.name)] = 0;
            if (stat(buf, &st) < 0) {
                printf("cannot stat %s\n", buf);
                continue;
            }
            if (st.type == T_DIR && (strcmp(fmtname(buf), ".") != 0) &&
                (strcmp(fmtname(buf), "..") != 0))
                find(buf, name);
            else if (st.type == T_FILE && !strcmp(fmtname(buf), name))
                printf("%s\n", buf);
        }
        break;
    }
    close(fd);
}
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(2, "Missing Parameters\n");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}