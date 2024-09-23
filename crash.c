#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

#define MAXLINE 1024

static int remove_pid(pid_t pid, bool print, char* msg);

typedef struct info {
    int num;
    pid_t pid;
    char* exec; //name of the program being executed
    char* status;
    struct info* next;
} job;

static int* job_num;
static job* jobs;
static job* last_job;
static int* running_jobs;

pid_t foreground;

static void print_process(int num, pid_t pid, const char* exec_name, char* state) {
    char pid_str[20]; // Assuming PID will fit in 20 characters
    snprintf(pid_str, sizeof(pid_str), "%d", pid);
    char integer[10];
    snprintf(integer, sizeof(integer), "%d", num);

    write(STDOUT_FILENO, "[", 1);
    write(STDOUT_FILENO, integer, strlen(integer));
    write(STDOUT_FILENO, "] (", 3);
    write(STDOUT_FILENO, pid_str, strlen(pid_str));
    write(STDOUT_FILENO, ")  ", 3);
    write(STDOUT_FILENO, state, strlen(state));
    write(STDOUT_FILENO, "  ", 2);
    write(STDOUT_FILENO, exec_name, strlen(exec_name));
    write(STDOUT_FILENO, "\n", 1);
}


void eval(const char **toks, bool bg) { // bg is true iff command ended with &
    assert(toks);
    if (*toks == NULL) return;
    if (strcmp(toks[0], "quit") == 0) {
        if (toks[1] != NULL) {
            const char *msg = "ERROR: quit takes no arguments\n";
            write(STDERR_FILENO, msg, strlen(msg));
        } else {
            exit(0);
        }

        return;
    }

    if (strcmp(toks[0], "nuke") == 0) {
        if(toks[1] == NULL) {
            job* temp = jobs;

            while(temp != NULL) {
                kill(temp->pid, SIGKILL);
                temp = temp->next;
            }
        } else {
            for(int k = 1; toks[k] != NULL; k++) {
                if(toks[k][0] == '%') {
                    char substr[sizeof(toks[k])]; // Allocate a buffer to store the substring
                    strcpy(substr, toks[k] + 1);
                    //validate that it is a valid int
                    bool good = true;
                    for (int i = 0; i < strlen(substr); i++) {
                        if(substr[i] > 57 || substr[i] < 47) {
                            //its not a valid int
                            char * msg = "ERROR: bad argument for nuke: ";
                            write(STDERR_FILENO, msg, strlen(msg));
                            write(STDERR_FILENO, toks[k], strlen(toks[k]));
                            write(STDERR_FILENO, "\n", 1);
                            good = false;
                            break;
                        }
                    }

                    if(!good) {
                        continue;
                    }

                    int num = atoi(substr);
                    
                    job* temp = jobs;
                    while(temp != NULL) {
                        if(temp->num == num) {
                            //found it!
                            kill(temp->pid, SIGKILL);
                            break;
                        }
                        temp = temp->next;
                    }

                    if(temp == NULL) {
                        const char *msg = "ERROR: no job ";
                        write(STDERR_FILENO, msg, strlen(msg));
                        write(STDERR_FILENO, substr, strlen(substr));
                        write(STDERR_FILENO, "\n", 1);
                    }
                } else {
                    bool good = true;
                    //validate that toks[k] is a valid int
                    for (int i = 0; i < strlen(toks[k]); i++) {
                        if(toks[k][i] > 57 || toks[k][i] < 47) {
                            //its not a valid int
                            char * msg = "ERROR: bad argument for nuke: ";
                            write(STDERR_FILENO, msg, strlen(msg));
                            write(STDERR_FILENO, toks[k], strlen(toks[k]));
                            write(STDERR_FILENO, "\n", 1);
                            good = false;
                            break;
                        }
                    }
                    if(good) {
                        if (kill(atoi(toks[k]), SIGKILL) == -1) {
                            const char *msg = "ERROR: no PID ";
                            write(STDERR_FILENO, msg, strlen(msg));
                            write(STDERR_FILENO, toks[k], strlen(toks[k]));
                            write(STDERR_FILENO, "\n", 1);
                        }
                    }
                }
            }
        }

        return;
    }

    if (strcmp(toks[0], "jobs") == 0) {
        if (toks[1] != NULL) {
            const char *msg = "ERROR: jobs takes no arguments\n";
            write(STDERR_FILENO, msg, strlen(msg));
        } else {
            job* temp = jobs;
            while(temp != NULL) {
                print_process(temp->num, temp->pid, temp->exec, temp->status);
                temp = temp->next;
            }
        }

        return;
    }

    if (strcmp(toks[0], "fg") == 0) {
        if(toks[2] != NULL || toks[1] == NULL) {
            char * msg = "ERROR: fg needs exactly one argument\n";
            write(STDERR_FILENO, msg, strlen(msg));
            return;
        } else {
            if(toks[1][0] == '%') {
                char substr[sizeof(toks[1])]; // Allocate a buffer to store the substring
                strcpy(substr, toks[1] + 1);
                //validate that it is a valid int
                for (int i = 0; i < strlen(substr); i++) {
                    if(substr[i] > 57 || substr[i] < 47) {
                        //its not a valid int
                        char * msg = "ERROR: bad argument for fg: ";
                        write(STDERR_FILENO, msg, strlen(msg));
                        write(STDERR_FILENO, toks[1], strlen(toks[1]));
                        write(STDERR_FILENO, "\n", 1);
                        return;
                    }
                }

                int num = atoi(substr);
                
                job* temp = jobs;
                while(temp != NULL) {
                    if(temp->num == num) {
                        //found it!

                        foreground = temp->pid;
                        // If the process is not running in the background, wait for it to finish
                        if(strcmp(temp->status, "suspended") == 0) {
                            kill(temp->pid, SIGCONT);
                            print_process(temp->num, temp->pid, temp->exec, "continued");
                            temp->status = "running";
                        }
                        pid_t child_pid;
                        int status;

                        while(child_pid = waitpid(temp->pid, &status, WNOHANG) == 0) {
                            if(foreground == 0) {
                                //new job holds the current job info
                                temp->status = "suspended";
                                print_process(temp->num, temp->pid, temp->exec, temp->status);
                                break;
                            }
                            usleep(1000);
                        }

                        foreground = 0;
                        return;
                    }
                    temp = temp->next;
                }

                if(temp == NULL) {
                    const char *msg = "ERROR: no job ";
                    write(STDERR_FILENO, msg, strlen(msg));
                    write(STDERR_FILENO, substr, strlen(substr));
                    write(STDERR_FILENO, "\n", 1);
                }
            } else {
                //validate that toks[1] is a valid int
                for (int i = 0; i < strlen(toks[1]); i++) {
                    if(toks[1][i] > 57 || toks[1][i] < 47) {
                        //its not a valid int
                        char * msg = "ERROR: bad argument for fg: ";
                        write(STDERR_FILENO, msg, strlen(msg));
                        write(STDERR_FILENO, toks[1], strlen(toks[1]));
                        write(STDERR_FILENO, "\n", 1);
                        return;
                    }
                }

                //search for the pid
                pid_t target_pid = atoi(toks[1]);
                job* temp = jobs;
                bool found = false;
                while(temp != NULL) {
                    if(temp->pid == target_pid) {
                        found = true;
                        foreground = temp->pid;
                        if(strcmp(temp->status, "suspended") == 0) {
                            kill(temp->pid, SIGCONT);
                            print_process(temp->num, temp->pid, temp->exec, "continued");
                            temp->status = "running";
                        }

                        // If the process is not running in the background, wait for it to finish
                        pid_t child_pid;
                        int status;
                        while(child_pid = waitpid(temp->pid, &status, WNOHANG) == 0) {
                            if(foreground == 0) {
                                //new job holds the current job info
                                temp->status = "suspended";
                                print_process(temp->num, temp->pid, temp->exec, temp->status);
                                break;
                            }
                            usleep(1000);
                        }

                        foreground = 0;
                        return;
                    }
                    temp = temp->next;
                }
                
                if(!found) {
                    const char *msg = "ERROR: no PID ";
                    write(STDERR_FILENO, msg, strlen(msg));
                    write(STDERR_FILENO, toks[1], strlen(toks[1]));
                    write(STDERR_FILENO, "\n", 1);
                }
            }
        }

        return;
    }

    if (strcmp(toks[0], "bg") == 0) {
        if(toks[1] == NULL) {
            char * msg = "ERROR: bg needs some arguments\n";
            write(STDERR_FILENO, msg, strlen(msg));
            return;
        } else {
            for(int k = 1; toks[k] != NULL; k++) {
                if(toks[k][0] == '%') {
                    char substr[sizeof(toks[k])]; // Allocate a buffer to store the substring
                    strcpy(substr, toks[k] + 1);
                    //validate that it is a valid int
                    bool good = true;
                    for (int i = 0; i < strlen(substr); i++) {
                        if(substr[i] > 57 || substr[i] < 47) {
                            //its not a valid int
                            char * msg = "ERROR: bad argument for bg: ";
                            write(STDERR_FILENO, msg, strlen(msg));
                            write(STDERR_FILENO, toks[k], strlen(toks[k]));
                            write(STDERR_FILENO, "\n", 1);
                            good = false;
                            break;
                        }
                    }

                    if(!good) {
                        continue;
                    }

                    int num = atoi(substr);
                    
                    job* temp = jobs;
                    while(temp != NULL) {
                        if(temp->num == num) {
                            //found it!
                            if(strcmp(temp->status, "suspended") == 0) {
                                kill(temp->pid, SIGCONT);
                                print_process(temp->num, temp->pid, temp->exec, "continued");
                                temp->status = "running";
                            }
                            break;
                        }
                        temp = temp->next;
                    }

                    if(temp == NULL) {
                        const char *msg = "ERROR: no job ";
                        write(STDERR_FILENO, msg, strlen(msg));
                        write(STDERR_FILENO, substr, strlen(substr));
                        write(STDERR_FILENO, "\n", 1);
                    }
                } else {
                    bool good = true;
                    //validate that toks[k] is a valid int
                    for (int i = 0; i < strlen(toks[k]); i++) {
                        if(toks[k][i] > 57 || toks[k][i] < 47) {
                            //its not a valid int
                            char * msg = "ERROR: bad argument for bg: ";
                            write(STDERR_FILENO, msg, strlen(msg));
                            write(STDERR_FILENO, toks[k], strlen(toks[k]));
                            write(STDERR_FILENO, "\n", 1);
                            good = false;
                            break;
                        }
                    }

                    //search for the pid
                    pid_t target_pid = atoi(toks[k]);
                    job* temp = jobs;
                    bool found = false;
                    while(temp != NULL) {
                        if(temp->pid == target_pid) {
                            found = true;
                            if(strcmp(temp->status, "suspended") == 0) {
                                kill(temp->pid, SIGCONT);
                                print_process(temp->num, temp->pid, temp->exec, "continued");
                                temp->status = "running";
                            }
                            break;
                        }
                        temp = temp->next;
                    }

                    if(!found) {
                        const char *msg = "ERROR: no PID ";
                        write(STDERR_FILENO, msg, strlen(msg));
                        write(STDERR_FILENO, toks[k], strlen(toks[k]));
                        write(STDERR_FILENO, "\n", 1);
                    }
                }
            }
        }

        return;
    }

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD); //change the masked signals
    sigprocmask(SIG_BLOCK, &mask, NULL);

    if(*running_jobs >= 32) {
        write(STDERR_FILENO, "ERROR: too many jobs\n", 21);
        return;
    } else {
        *running_jobs = *running_jobs + 1;
    }

    pid_t p1 = fork();

    if(p1 == 0) {
        //set the process group id
        setpgid(0, getpid());
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
        if(execvp(toks[0], (char *const *) toks) == -1) {
            write(STDERR_FILENO, "ERROR: cannot run ", 18);
            write(STDERR_FILENO, toks[0], strlen(toks[0]));
            write(STDERR_FILENO, "\n", 1);
            exit(2);
        }
    } else {
        job* new_job = malloc(sizeof(job));
        if(new_job == NULL) {
            write(STDERR_FILENO, "No memory remaining\n", 20); 
            exit(2);
        }

        new_job->exec = malloc(strlen(toks[0])+1);
        if(new_job->exec != NULL) {
            strcpy(new_job->exec, toks[0]);
        }

        new_job->num = *job_num + 1;
        new_job->pid = p1;
        new_job->status = "running";
        new_job->next = NULL;

        if(last_job != NULL) {
            last_job->next = new_job;
        } else {
            jobs = new_job;
        }

        last_job = new_job;

        *job_num = *job_num + 1;
        
        //finish adding to list
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
        if(bg) {
            print_process(*job_num, p1, toks[0], "running");
        } else {
            foreground = p1;
            // If the process is not running in the background, wait for it to finish
            pid_t child_pid;
            int status;
            while(child_pid = waitpid(p1, &status, WNOHANG) == 0) {
                if(foreground == 0) {
                    //new job holds the current job info
                    new_job->status = "suspended";
                    print_process(new_job->num, new_job->pid, new_job->exec, new_job->status);
                    break;
                }
                usleep(500);
            }

            foreground = 0;
        }
    }
}

//returns 1 if successful, returns 0 if not found
static int remove_pid(pid_t pid, bool print, char* message) {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD); //change the masked signals
    sigprocmask(SIG_BLOCK, &mask, NULL);

    *running_jobs = *running_jobs - 1;
    job* temp = jobs;
    job* prev = NULL;
    while(temp != NULL) {
        if(temp->pid == pid) {
            //remove them from the list

            if(prev != NULL) {
                prev->next = temp->next;
            } else {
                //they are the first ones in the list
                jobs = temp->next;
            }

            if(last_job == temp) {
                //they are the last job
                last_job = prev;
            }

            if(print) {
                print_process(temp->num, temp->pid, temp->exec, message);
            }
            free(temp->exec);
            free(temp);
            break;
        }

        prev = temp;
        temp = temp->next;
    }

    if(temp == NULL) {
        return 0;
    }
    return 1;
}

void parse_and_eval(char *s) {
    assert(s);
    const char *toks[MAXLINE+1];
    
    while (*s != '\0') {
        bool end = false;
        bool bg = false;
        int t = 0;

        while (*s != '\0' && !end) {
            while (*s == '\n' || *s == '\t' || *s == ' ') ++s;
            if (*s != ';' && *s != '&' && *s != '\0') toks[t++] = s;
            while (strchr("&;\n\t ", *s) == NULL) ++s;
            switch (*s) {
            case '&':
                bg = true;
                end = true;
                break;
            case ';':
                end = true;
                break;
            }
            if (*s) *s++ = '\0';
        }
        toks[t] = NULL;
        eval(toks, bg);
    }
}

void prompt() {
    const char *prompt = "crash> ";
    ssize_t nbytes = write(STDOUT_FILENO, prompt, strlen(prompt));
}

int repl() {
    char *buf = NULL;
    size_t len = 0;
    while (prompt(), getline(&buf, &len, stdin) != -1) {
        parse_and_eval(buf);
    }

    if (buf != NULL) free(buf);
    if (feof(stdin)) {
        exit(0);
    }
    if (ferror(stdin)) {
        perror("ERROR");
        return 1;
    }
    return 0;
}

static void child_signal_handler(int sig) {
    int err = errno;
    pid_t child_pid;
    int status;
    while ((child_pid = waitpid(-1, &status, WNOHANG)) >= 1) {
        int signal = WTERMSIG(status);
        if(signal == SIGSEGV || signal == SIGQUIT) {
            remove_pid(child_pid, true, "killed (core dumped)");
        } else if(signal == SIGINT || signal == SIGTERM || signal == SIGKILL) {
            remove_pid(child_pid, true, "killed");
        } else {
            //remove the child
            remove_pid(child_pid, true, "finished");
        }
    }
    errno = err;
}

//Ctrl-C
static void int_signal_handler(int sig) {
    int err = errno;
    //send a signal to child
    if(foreground != 0) {
        kill(foreground, SIGINT);
    }
    errno = err;
}

//Ctrl-backslash
static void quit_signal_handler(int sig) {
    int err = errno;
    if(foreground == 0) {
        exit(0);
    } else {
        kill(foreground, SIGQUIT);
    }
    errno = err;
}

//Ctrl-Z
static void stop_signal_handler(int sig) {
    int err = errno;
    if(foreground != 0) {
        kill(foreground, SIGSTOP);
        foreground = 0;
    }
    errno = err;
}

static void signals_setup() {
    struct sigaction act;
    act.sa_handler = child_signal_handler;
    act.sa_flags = SA_RESTART;
    sigemptyset(&act.sa_mask);
    sigaction(SIGCHLD, &act, NULL);

    struct sigaction act1;
    act1.sa_handler = int_signal_handler;
    act1.sa_flags = SA_RESTART;
    sigemptyset(&act1.sa_mask);
    sigaction(SIGINT, &act1, NULL);

    struct sigaction act2;
    act2.sa_handler = quit_signal_handler;
    act2.sa_flags = SA_RESTART;
    sigemptyset(&act2.sa_mask);
    sigaction(SIGQUIT, &act2, NULL);

    struct sigaction act3;
    act3.sa_handler = stop_signal_handler;
    act3.sa_flags = SA_RESTART;
    sigemptyset(&act3.sa_mask);
    sigaction(SIGSTOP, &act3, NULL);
    sigaction(SIGTSTP, &act3, NULL);
}

int main(int argc, char **argv) {
    signals_setup();

    last_job = NULL;
    jobs = NULL;
    job_num = malloc(sizeof(int));
    running_jobs = malloc(sizeof(int));
    *running_jobs = 0;
    foreground = 0;
    return repl();
}
