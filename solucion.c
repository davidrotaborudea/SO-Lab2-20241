#include <stdio.h>
#include <stdlib.h>
#include <string.h>   
#include <unistd.h>   
#include <sys/wait.h> 

static char *trim_newline(char *s) {
    if (!s) return s;
    size_t n = strlen(s);
    if (n && s[n-1] == '\n') s[n-1] = '\0';
    return s;
}

static int split_whitespace(char *line, char **argv, int maxv) {
    int argc = 0;
    char *save = line;
    char *tok;
    while ((tok = strsep(&save, " \t")) != NULL) {
        if (*tok == '\0') continue; 
        if (argc < maxv - 1) argv[argc++] = tok;
    }
    argv[argc] = NULL;
    return argc;
}

int main(void) {
    char *line = NULL;
    size_t cap = 0;

    while (1) {
        printf("wish> ");
        fflush(stdout);

        if (getline(&line, &cap, stdin) == -1) {

            break;
        }
        trim_newline(line);

        
        char *argv[256];
        int argc = split_whitespace(line, argv, 256);
        if (argc == 0) continue;

        
        if (strcmp(argv[0], "exit") == 0) {
            free(line);
            exit(0);
        }

        char pathbuf[1024];
        snprintf(pathbuf, sizeof(pathbuf), "/bin/%s", argv[0]);

        pid_t pid = fork();
        if (pid == 0) {
            execv(pathbuf, argv);
            _exit(1);
        } else if (pid > 0) {
            (void)waitpid(pid, NULL, 0);
        } else {
            
        }
    }

    free(line);
    return 0;
}
