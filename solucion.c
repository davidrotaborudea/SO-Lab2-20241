#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>   
#include <unistd.h>   
#include <sys/wait.h> 
#include <fcntl.h>    

static const char ERR_MSG[] = "An error has occurred\n";

static void print_error(void) {
    (void)write(STDERR_FILENO, ERR_MSG, sizeof(ERR_MSG)-1);
}

static char *trim_newline(char *s) {
    if (!s) return s;
    size_t n = strlen(s);
    if (n && s[n-1] == '\n') s[n-1] = '\0';
    return s;
}

static int split_ws(char *line, char **argv, int maxv) {
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

typedef struct {
    char **dirs;
    size_t len, cap;
} PathList;

static void path_init(PathList *p) {
    p->cap = 4; p->len = 0;
    p->dirs = (char **)malloc(p->cap * sizeof(char *));
    if (!p->dirs) { perror("malloc"); exit(1); }
    p->dirs[p->len++] = strdup("/bin");
}

static void path_clear(PathList *p) {
    for (size_t i = 0; i < p->len; i++) free(p->dirs[i]);
    p->len = 0;
}

static void path_push(PathList *p, const char *dir) {
    if (p->len == p->cap) {
        p->cap *= 2;
        p->dirs = (char **)realloc(p->dirs, p->cap * sizeof(char *));
        if (!p->dirs) { perror("realloc"); exit(1); }
    }
    p->dirs[p->len++] = strdup(dir);
}

static char *resolve_cmd(const char *cmd, PathList *pl) {
    if (pl->len == 0) return NULL; 
    for (size_t i = 0; i < pl->len; i++) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s/%s", pl->dirs[i], cmd);
        if (access(buf, X_OK) == 0) {
            return strdup(buf);
        }
    }
    return NULL;
}

static bool is_builtin(const char *cmd) {
    return strcmp(cmd, "exit") == 0 || strcmp(cmd, "cd") == 0 || strcmp(cmd, "path") == 0;
}

static bool do_builtin(char **argv, int argc, PathList *pl) {
    if (strcmp(argv[0], "exit") == 0) {
        if (argc != 1) { print_error(); return true; }
        exit(0);
    } else if (strcmp(argv[0], "cd") == 0) {
        if (argc != 2) { print_error(); return true; }
        if (chdir(argv[1]) != 0) print_error();
        return true;
    } else if (strcmp(argv[0], "path") == 0) {
        path_clear(pl);
        for (int i = 1; i < argc; i++) path_push(pl, argv[i]);
        return true;
    }
    return false;
}

static void run_line(char *line, PathList *pl) {
    char *argv[256];
    int argc = split_ws(line, argv, 256);
    if (argc == 0) return;

    if (is_builtin(argv[0])) {
        (void)do_builtin(argv, argc, pl);
        return;
    }

    char *full = resolve_cmd(argv[0], pl);
    if (!full) { print_error(); return; }

    pid_t pid = fork();
    if (pid == 0) {
        execv(full, argv);
        _exit(1);
    } else if (pid > 0) {
        (void)waitpid(pid, NULL, 0);
    } else {
        print_error();
    }
    free(full);
}

int main(int argc, char *argv[]) {
    PathList pl; path_init(&pl);
    char *line = NULL;
    size_t cap = 0;
    FILE *in = NULL;
    bool interactive = true;

    if (argc == 1) {
        in = stdin; interactive = true;
    } else if (argc == 2) {
        in = fopen(argv[1], "r");
        if (!in) { print_error(); exit(1); }
        interactive = false;
    } else {
        print_error();
        exit(1);
    }

    while (1) {
        if (interactive) { printf("wish> "); fflush(stdout); }
        ssize_t nread = getline(&line, &cap, in);
        if (nread == -1) break;
        trim_newline(line);
        run_line(line, &pl);
    }

    free(line);
    for (size_t i = 0; i < pl.len; i++) free(pl.dirs[i]);
    free(pl.dirs);
    exit(0);
}
