#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

static const char ERR_MSG[] = "An error has occurred\n";

static void print_error(void){
    (void)write(STDERR_FILENO, ERR_MSG, sizeof(ERR_MSG)-1);
}

static char *trim_newline(char *s){
    if (!s) return s;
    size_t n = strlen(s);
    if (n && s[n-1]=='\n') s[n-1]='\0';
    return s;
}

static char *normalize_ops(const char *in){
    size_t n = strlen(in);
    size_t cap = 3*n + 1;
    char *out = (char*)malloc(cap);
    if (!out) exit(1);
    size_t j = 0;
    for (size_t i=0;i<n;i++){
        char c = in[i];
        if (c=='>' || c=='&'){
            out[j++] = ' ';
            out[j++] = c;
            out[j++] = ' ';
        } else {
            out[j++] = c;
        }
    }
    out[j] = '\0';
    return out;
}

static int split_ws(char *line, char **argv, int maxv){
    int argc=0;
    char *save=line;
    char *tok;
    while ((tok = strsep(&save, " \t")) != NULL){
        if (*tok=='\0') continue;
        if (argc < maxv-1) argv[argc++] = tok;
    }
    argv[argc] = NULL;
    return argc;
}

typedef struct { char **dirs; size_t len, cap; } PathList;

static void path_init(PathList *p){
    p->cap=4;
    p->len=0;
    p->dirs = (char**)malloc(p->cap*sizeof(char*));
    if(!p->dirs) exit(1);
    p->dirs[p->len++] = strdup("/bin");
}

static void path_clear(PathList *p){
    for(size_t i=0;i<p->len;i++) free(p->dirs[i]);
    p->len=0;
}

static void path_push(PathList *p, const char *dir){
    if(p->len==p->cap){
        p->cap*=2;
        p->dirs=(char**)realloc(p->dirs, p->cap*sizeof(char*));
        if(!p->dirs) exit(1);
    }
    p->dirs[p->len++] = strdup(dir);
}

static char *resolve_cmd(const char *cmd, PathList *pl){
    if (pl->len==0) return NULL;
    for(size_t i=0;i<pl->len;i++){
        char buf[1024];
        snprintf(buf,sizeof(buf),"%s/%s", pl->dirs[i], cmd);
        if (access(buf, X_OK)==0)
            return strdup(buf);
    }
    return NULL;
}

static bool is_builtin(const char *cmd){
    return strcmp(cmd,"exit")==0 || strcmp(cmd,"cd")==0 || strcmp(cmd,"path")==0;
}

static bool do_builtin(char **argv, int argc, PathList *pl){
    if (strcmp(argv[0],"exit")==0){
        if (argc!=1){
            print_error(); 
            return true;
        }
        exit(0);
    } else if (strcmp(argv[0],"cd")==0){
        if (argc!=2){
            print_error(); 
            return true;
        }
        if (chdir(argv[1])!=0)
            print_error();
        return true;
    } else if (strcmp(argv[0],"path")==0){
        path_clear(pl);
        for (int i=1;i<argc;i++)
            path_push(pl, argv[i]);
        return true;
    }
    return false;
}

typedef struct {
    char *argv[256];
    int   argc;
    char *redir;
} Job;

static bool parse_job_tokens(char **tokens, int ntok, Job *job){
    job->argc = 0;
    job->redir = NULL;
    int i=0;
    while (i<ntok){
        if (strcmp(tokens[i], ">")==0){
            if (job->redir != NULL) {
                print_error(); 
                return false;
            }
            if (i+1 >= ntok) {
                print_error(); 
                return false;
            }
            job->redir = tokens[i+1];
            if (i+2 != ntok) {
                print_error(); 
                return false;
            }
            break;
        } else if (strcmp(tokens[i], "&")==0){
            print_error(); 
            return false;
        } else {
            if (job->argc < 255) job->argv[job->argc++] = tokens[i];
            i++;
        }
    }
    job->argv[job->argc] = NULL;
    if (job->argc == 0) {
        print_error(); 
        return false;
    }
    return true;
}

static int split_by_amp(char **tokens, int ntok, int *starts, int *ends, int maxjobs){
    int nj=0;
    int s=0;
    for (int i=0;i<=ntok;i++){
        if (i==ntok || strcmp(tokens[i],"&")==0){
            if (nj < maxjobs){
                starts[nj]=s;
                ends[nj]=i;
                nj++;
            }
            s=i+1;
        }
    }
    return nj;
}

static void run_job(Job *job, PathList *pl, pid_t *out_pid){
    if (is_builtin(job->argv[0])){
        if (job->redir){
            print_error(); 
            return;
        }
        (void)do_builtin(job->argv, job->argc, pl);
        return;
    }

    char *full = resolve_cmd(job->argv[0], pl);
    if (!full){
        print_error(); 
        return;
    }

    pid_t pid = fork();
    if (pid == 0){
        if (job->redir){
            int fd = open(job->redir, O_CREAT|O_TRUNC|O_WRONLY, 0644);
            if (fd < 0){
                print_error(); 
                _exit(1);
            }
            if (dup2(fd, STDOUT_FILENO)<0 || dup2(fd, STDERR_FILENO)<0){
                print_error(); 
                _exit(1);
            }
            close(fd);
        }
        execv(full, job->argv);
        print_error();
        _exit(1);
    } else if (pid > 0){
        if (out_pid) *out_pid = pid;
    } else {
        print_error();
    }
    free(full);
}

static void run_line(char *raw, PathList *pl){
    char *norm = normalize_ops(raw);

    char *tokens[512];
    int ntok = 0;
    char *save = norm, *tok;
    while ((tok = strsep(&save, " \t")) != NULL){
        if (*tok == '\0') continue;
        if (ntok < 512-1) tokens[ntok++] = tok;
    }
    tokens[ntok] = NULL;
    if (ntok == 0){
        free(norm); 
        return; 
    }

    int starts[128], ends[128];
    int njobs = split_by_amp(tokens, ntok, starts, ends, 128);
    if (njobs <= 0){
        free(norm); 
        return; 
    }

    pid_t pids[128];
    int pc=0;

    for (int j=0;j<njobs;j++){
        int s = starts[j], e = ends[j];
        if (e - s <= 0){
            print_error(); 
            continue;
        }
        Job job;
        if (!parse_job_tokens(&tokens[s], e - s, &job))
            continue;

        pid_t pid = -1;
        run_job(&job, pl, &pid);
        if (pid > 0 && pc < 128)
            pids[pc++] = pid;
    }

    for (int i=0;i<pc;i++){
        (void)waitpid(pids[i], NULL, 0);
    }

    free(norm);
}

int main(int argc, char *argv[]){
    PathList pl; 
    path_init(&pl);

    char *line = NULL;
    size_t cap = 0;
    FILE *in = NULL;
    bool interactive = true;

    if (argc == 1){
        in = stdin; 
        interactive = true;
    } else if (argc == 2){
        in = fopen(argv[1], "r");
        if (!in){
            print_error();
            exit(1);
        }
        interactive = false;
    } else {
        print_error(); 
        exit(1);
    }

    while (1){
        if (interactive){
            printf("wish> ");
            fflush(stdout);
        }
        ssize_t nread = getline(&line, &cap, in);
        if (nread == -1) break;
        trim_newline(line);
        run_line(line, &pl);
    }

    free(line);
    for (size_t i=0;i<pl.len;i++)
        free(pl.dirs[i]);
    free(pl.dirs);

    exit(0);
}
