
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

char buf[512];
char exec_args[512];
int main(int argc, char *argv[]) {

    int n, cur_word_len = 0, argc_tmp = argc - 1;
    char *w;
    char *arguments[MAXARG];
    if (argc < 2) {
        fprintf(2, "arguments length error\n");
        exit(1);
    }
    // printf("argc: %d\n",argc);
    // for(int i = 0; i < argc; i++){
    //     printf("argv[%d]: %s\n", i, argv[i]);
    // }
    for (int i = 1; i < argc; i++) { // skip xargs
        arguments[i - 1] = argv[i];
    }
    while ((n = read(0, buf, sizeof(buf))) > 0) {
        // printf("buf: %s, %d\n", buf, n);
        for (int i = 0; i < n; i++) {
            // printf("buf[%d]: %c , %d, %d\n",i, buf[i], buf[i] == ' ', buf[i]
            // == '\n');
            if (buf[i] == ' ' || buf[i] == '\n') {
                if (cur_word_len == 0)
                    continue;
                w = malloc(cur_word_len * sizeof(char));
                memcpy(w, exec_args, cur_word_len * sizeof(char));
                // printf("word: %s, %d\n", w, cur_word_len);
                arguments[argc_tmp++] = w;
                cur_word_len = 0;
                if (buf[i] == '\n') {
                    if (fork() == 0) { // child
                        // printf("exec: %s ", argv[1]);
                        // for (int j = 0; j < argc_tmp; j++)
                        //     printf("%s ", arguments[j]);
                        // printf("\n");
                        exec(argv[1], arguments);
                        exit(0);
                    } else { // parent
                        wait(0);
                        for (int k = argc; k < argc_tmp; k++)
                            free(arguments[k]);
                        argc_tmp = argc - 1;
                        cur_word_len = 0;
                    }
                }
            } else {
                exec_args[cur_word_len++] = buf[i];
            }
        }
    }
    exit(0);
}