/* $begin shellmain */
#include "csapp.h"
#include <errno.h>
#include <stdbool.h>
#define MAXARGS   128
#define MAX_LINE_LENGTH 8192
#define MAXJOBS 32
#define HISTORY_FILE ".my_shell_history_phase3"

/* Function prototypes */
void eval(char *cmdline);
int parseline(char *buf, char **argv);
void print_prompt(); // prompt 출력

/* 내부 명령어 관련 함수 */
int handle_builtin_command(char **argv); 
int is_builtin_command(char **argv);
int handle_cd_command(char *argv); 
void handle_exit_command(); 

/* 시그널 핸들링 관련 함수 */
void sigint_handler(int sig);
void sigchld_handler(int signum);
void sigtstp_handler(int sig);

// 외부 명령어 처리 함수 -> Fork()를 사용해야하는 함수들
void handle_external_command(char **argv); 

/* histroy 관련 함수 및 변수 */
int handle_history_command(char **argv); 
void load_history();
void save_history();
void add_history(char *cmdline);
char *replace_command(char **argv);

char **history = NULL;
int history_count = 0;

/* pipe 관련 함수 */
bool check_for_pipe(char *cmdline);
void handle_piped_commands(char *cmdline);

/* phase3 관련 함수 및 변수 */
typedef struct {
    pid_t pid;
    int jid;
    int state;
    char cmdline[MAX_LINE_LENGTH];
} Job;

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

Job jobs[MAXJOBS];
int nextjid = 1; 
pid_t fg_pid = 0; 

void add_job(Job *jobs, pid_t pid, int state, char *cmdline);
void remove_job(Job *jobs, pid_t pid);
int pid2jid(pid_t pid);
pid_t jid2pid(int jid);
void list_jobs(Job *jobs);
void do_bgfg(char **argv);

int main() 
{
    Signal(SIGCHLD, sigchld_handler); // set handler for SIGCHLD	
	Signal(SIGINT, sigint_handler); // set handler for SIGINT
	Signal(SIGTSTP, sigtstp_handler); // set handler for SIGTSTP

    char cmdline[MAXLINE]; /* Command line */
    memset(jobs, 0, sizeof(jobs));
    
    load_history();

    while (1) {
        print_prompt(); 
        /* Read */
        char *tmp = fgets(cmdline, MAXLINE, stdin); // warning 안뜨기 위해 
        if (feof(stdin))
            exit(0);
        /* Evaluate */
        eval(cmdline);
    } 
}
/* $end shellmain */
  
/* $begin eval */
/* eval - Evaluate a command line */
void eval(char *cmdline) 
{
    char *argv[MAXARGS]; /* Argument list execve() */
    char buf[MAXLINE];   /* Holds modified command line */
    int bg;              /* Should the job run in bg or fg? */
    pid_t pid;           /* Process id */
    
    strcpy(buf, cmdline);
    bg = parseline(buf, argv); 
    if (argv[0] == NULL)  
       return;   /* Ignore empty lines */
    if (!strcmp(argv[0], "&"))    /* Ignore singleton & */
	    return;

    char *replaced_command = replace_command(argv); // argv[0]에서만 !! 혹은 !# 가 나오므로 
    if (replaced_command) {
        add_history(replaced_command);
        strcpy(cmdline, replaced_command);
        bg = parseline(cmdline, argv);
        free(replaced_command);
    }
    else add_history(cmdline);

    sigset_t mask, prev;
    Sigemptyset(&mask);
    Sigaddset(&mask, SIGCHLD);

    int is_builtin = is_builtin_command(argv);
    int has_pipe = check_for_pipe(cmdline);

    if (is_builtin) {
        if (bg) {
            // Execute the builtin command in the background
            if ((pid = Fork()) == 0) {
                handle_builtin_command(argv);
                exit(0);
            } else {
                setpgid(pid, pid);
                add_job(jobs, pid, BG, cmdline);
                printf("[%d] %d %s\n", pid2jid(pid), pid, cmdline);
            }
        } else {
            // Execute the builtin command in the foreground
            handle_builtin_command(argv);
        }
    } else {
        if ((pid = Fork()) == 0) { // Child process
            setpgid(0, 0);
            if (has_pipe) {
                handle_piped_commands(cmdline);
            } else {
                handle_external_command(argv); // Handle external command
            }
            exit(0);
        }
    }

    if (!bg) {
        if (!is_builtin) {
            // Parent waits for foreground external job to terminate
            add_job(jobs, pid, FG, cmdline);
            fg_pid = pid;
            setpgid(pid, pid);

            Sigprocmask(SIG_BLOCK, &mask, &prev);
            while (fg_pid != 0) {
                Sigsuspend(&prev);
            }
            Sigprocmask(SIG_SETMASK, &prev, NULL);
        }
    } else {
        if (!is_builtin) {
            // Background external process
            setpgid(pid, pid);
            add_job(jobs, pid, BG, cmdline);
            printf("[%d] %d %s\n", pid2jid(pid), pid, cmdline);
        }
    }
}
/* $end eval */

/* If first arg is a builtin command, run it and return true */
int handle_builtin_command(char **argv) 
{
    if (!strcmp(argv[0], "quit")) {/* quit command */
        handle_exit_command();
    }
    if (!strcmp(argv[0], "&"))    /* Ignore singleton & */
	    return 1;
    if (!strcmp(argv[0], "cd")) {
        return handle_cd_command(argv[1]);
    }
    if (!strcmp(argv[0], "history")) {
        return handle_history_command(argv); 
    }
    if (!strcmp(argv[0], "exit")) {
        handle_exit_command(); 
    }
    if (!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg")) {
        do_bgfg(argv);
        return 1;
    }
    if (!strcmp(argv[0], "jobs")) {
        list_jobs(jobs);
        return 1;
    }
    if (!strcmp(argv[0], "kill")){ // command kill %jobid
		if(argv[1][0] == '%'){
			int jid = atoi(&(argv[1][1]));
            pid_t pid; 
			if((pid = jid2pid(jid) ) == 0){
				printf("Error : kill error invalid job id\n");
				return 1;
			}
			Kill(pid, SIGKILL);
			return 1;
		}
	}

    return 0;                     /* Not a builtin command */
}

void handle_exit_command() {
    for(int j = 0 ; j < MAXJOBS ; j++){
        if(jobs[j].pid != 0){
            Kill(jobs[j].pid, SIGKILL);
        }
	}
    save_history();
    free(history);
    exit(0);
}

void handle_external_command(char **argv) {
    if (execvp(argv[0], argv) < 0) {
        printf("%s: Command not found.\n", argv[0]);
        exit(127);
    }
}

/* $begin parseline */
/* parseline - Parse the command line and build the argv array */
int parseline(char *buf, char **argv) 
{
    char *delim;         /* Points to first space delimiter */
    int argc;            /* Number of args */
    int bg;              /* Background job? */

    if (buf[strlen(buf)-1] == '\n')
        buf[strlen(buf)-1] = ' ';  /* Replace trailing '\n' with space */

    while (*buf && (*buf == ' ')) /* Ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    while (*buf) {
        if (*buf == '\'' || *buf == '\"') { // Check for single or double quotes
            char quote = *buf; // 다음 quote가 들어오기 전까지 문자열로 취급함 
            buf++;
            argv[argc++] = buf;
            while (*buf && *buf != quote)
                buf++;
            if (*buf)
                *buf++ = '\0';
        } else {
            argv[argc++] = buf;
            while (*buf && *buf != ' ')
                buf++;
            if (*buf)
                *buf++ = '\0';
        }
        while (*buf && (*buf == ' ')) /* Ignore spaces */
            buf++;
    }

    argv[argc] = NULL;
    
    if (argc == 0)  /* Ignore blank line */
        return 1;
    
    /* Should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0)
        argv[--argc] = NULL;

    
    if (argc > 0 && argv[argc-1][strlen(argv[argc-1])-1] == '&') {
        argv[argc-1][strlen(argv[argc-1])-1] = '\0';
        bg = 1; 
    }
    return bg;
}
/* $end parseline */

void print_prompt() {
    char hostname[HOST_NAME_MAX + 1];
    char *username;
    char *cwd;
    char *home_directory;
    char path[PATH_MAX + 1];

     // 사용자 이름 가져오기
    username = getenv("USER");
    if (username == NULL) {
        perror("getenv");
        return;
    }

    home_directory = getenv("HOME");
    if (home_directory == NULL) {
        perror("getenv");
        return ;
    }

    // 호스트 이름 가져오기
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        perror("gethostname");
        return ;
    }

    // 현재 작업 디렉토리 정보 가져오기
    cwd = getcwd(path, sizeof(path));
    if (cwd == NULL) {
        perror("getcwd");
        return ;
    }

    // 현재 작업 디렉토리 경로가 홈 디렉토리로 시작하는 경우
    if (strncmp(cwd, home_directory, strlen(home_directory)) == 0) {
        // 홈 디렉토리 경로를 ~로 대체
        memmove(cwd + 1, cwd + strlen(home_directory), strlen(cwd) - strlen(home_directory) + 1);
        cwd[0] = '~';
    }

	printf("%s@%s:%s> ", username, hostname, cwd);   
}

int handle_cd_command(char *argv) {
    if (argv == NULL) {
        argv = getenv("HOME");
    } 
    else if (argv[0] == '~') {
        char *home = getenv("HOME");
        static char expanded_path[PATH_MAX];
        snprintf(expanded_path, PATH_MAX, "%s%s", home, argv + 1);
        argv = expanded_path;
    }
    if (chdir(argv) != 0) {
        fprintf(stderr, "bash: cd: %s: %s\n", argv, strerror(errno));
        return 0; 
    }
    return 1;
}

char *replace_command(char **argv) {
    char *replaced_command = (char *)malloc(sizeof(char) * MAX_LINE_LENGTH);
    if (replaced_command == NULL) {
        fprintf(stderr, "Failed to allocate memory\n");
        exit(1);
    }
    char *out_ptr = replaced_command;
    int i, num, digit_count, index = -1;
    int len = strlen(argv[0]);
    bool found = false;

    for (i = 0; i < len; i++) {
        if (argv[0][i] == '!' && argv[0][i + 1] == '!') {
            // Replace "!!" with str
            index = history_count - 1 ;
            strcpy(out_ptr, history[index]);
            out_ptr += strlen(history[index]);
            i++; // skip the second '!'
            found = true; 
        } else if (argv[0][i] == '!' && isdigit(argv[0][i + 1])) {
            // Replace "!<number>" with str if the number is less than or equal to 10
            found = true; 
            num = argv[0][i + 1] - '0';
            digit_count = 1;
            for (int j = i + 2; isdigit(argv[0][j]); j++) {
                num = num * 10 + (argv[0][j] - '0');
                digit_count++;
            }
            if (num <= history_count) {
                index = num - 1 ;
                strcpy(out_ptr, history[index]);
                out_ptr += strlen(history[index]);
                i += digit_count; // skip the number and '!'
            } else {
                *out_ptr++ = argv[0][i];
            }
        } else {
            *out_ptr++ = argv[0][i];
        }
    }
    *out_ptr = '\0';

    if (!found) {
        free(replaced_command);
        return NULL; 
    }
    
    for (int i = 1; argv[i] != NULL; i++) {
        strcat(replaced_command, " ");
        strcat(replaced_command, argv[i]);
    }
    printf("%s\n", replaced_command); 

    return replaced_command;
}

/*뒤에 숫자오는 경우 수정해야 됨*/
/* 수정 완료 */ 
int handle_history_command(char **argv) {
    if (argv[1] == NULL) {
        for (int i = 0; i < history_count; i++) {
            printf("%d %s\n", i + 1, history[i]);
        }
    } 
    else {
        if (isdigit(argv[1][0])) {
            int num = atoi(argv[1]);
            for (int i = history_count - num; i < history_count; i++) {
                printf("%d %s\n", i + 1, history[i]);
            }

        }
    }
}

void add_history(char *cmdline) {
    size_t len = strlen(cmdline);
    if (cmdline[len - 1] == '\n') {
        cmdline[len - 1] = '\0';
    }
    if (history_count > 0 && strcmp(history[history_count - 1], cmdline) == 0) {
        return; // Skip duplicate command
    }
    history = realloc(history, sizeof(char *) * (history_count + 1));
    if (!history) {
        fprintf(stderr, "Failed to allocate memory for history\n");
        exit(1);
    }
    history[history_count++] = strdup(cmdline);
}

// save와 load에서 지정된 위치에서 로그를 읽고 저장해야함! 
void load_history() {
    char history_file_path[PATH_MAX];
    snprintf(history_file_path, sizeof(history_file_path), "%s/%s", getenv("HOME"), HISTORY_FILE);
    FILE *fp = fopen(history_file_path, "r");
    if (!fp) {
        return;
    }
    char line[MAX_LINE_LENGTH];
    while (fgets(line, MAX_LINE_LENGTH, fp)) {
        line[strlen(line) - 1] = '\0';
        add_history(line);
    }
    fclose(fp);
}

void save_history() {
    char history_file_path[PATH_MAX];
    snprintf(history_file_path, sizeof(history_file_path), "%s/%s", getenv("HOME"), HISTORY_FILE);
    int fd = Open(history_file_path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        return;
    }
    for (int i = 0; i < history_count; i++) {
        Write(fd, history[i], strlen(history[i]));
        Write(fd, "\n", 1);
    }
    Close(fd);
}

bool check_for_pipe(char *cmdline) {
    return strchr(cmdline, '|') != NULL;
}

void handle_piped_commands(char *cmdline) {
    char *commands[MAXARGS];
    int num_commands = 0;
    char *token = strtok(cmdline, "|");

    while (token) {
        commands[num_commands++] = token;
        token = strtok(NULL, "|");
    }
    commands[num_commands] = NULL;

    int pipe_fds[2];
    int in_fd = dup(STDIN_FILENO);
    int out_fd;

    for (int i = 0; i < num_commands; i++) {
        if (i < num_commands - 1) {
            if (pipe(pipe_fds) < 0 ) {
                perror("pipe error");
		        exit(1);
            }
            out_fd = pipe_fds[1];
        } else {
            out_fd = dup(STDOUT_FILENO);
        }

        char buf[MAX_LINE_LENGTH];
        strcpy(buf, commands[i]);
        char *argv[MAXARGS];
        parseline(buf, argv);

        pid_t pid = Fork();
        if (pid == 0) {
            dup2(in_fd, STDIN_FILENO);
            dup2(out_fd, STDOUT_FILENO);
            handle_external_command(argv);
        } else {
            wait(NULL);
        }

        if (in_fd != STDIN_FILENO) close(in_fd);
        if (out_fd != STDOUT_FILENO) close(out_fd);

        in_fd = pipe_fds[0];
    }
}

void add_job(Job *jobs, pid_t pid, int state, char *cmdline) {
    if (pid < 1)
        return;
    
    for (int i=nextjid-1; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid; 
            jobs[i].jid = i+1; 
            nextjid++;
            if (nextjid > MAXJOBS) nextjid = 1; 
            jobs[i].state = state; 
            strcpy(jobs[i].cmdline, cmdline); 

            return; 
        } 
    }
    return;
}

void remove_job(Job *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            if (jobs[i].jid == nextjid - 1) nextjid--;
            jobs[i].pid = 0;
            jobs[i].jid = 0;
            jobs[i].state = 0;
            strcpy(jobs[i].cmdline, "");
            return;
        }
    }
    printf("No such job\n");
}

int pid2jid(pid_t pid) {
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
            return jobs[i].jid;
    return 0;
}

pid_t jid2pid(int jid) {
    int i;

    if (jid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid)
            return jobs[i].pid;
    return 0;
}

void list_jobs(Job *jobs) {
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("[%d] ", jobs[i].jid);

            switch (jobs[i].state) {
                case ST:
                    printf("suspended ");
                    break;
                case BG:
                    printf("running ");
                    break;
                case FG:
                    printf("foreground ");
                    break;
                default:
                    printf("unknown ");
                    break;
            }

            printf("%s\n", jobs[i].cmdline);
        }
    }
}


void do_bgfg(char **argv) {
    int jid;
    pid_t pid;
    char *id = argv[1];
    Job *job;

    if (id == NULL) {
        printf("%s command requires PID or %%jobid argument\n", argv[0]);
        return;
    }

    if (id[0] == '%') { // jid is given
        jid = atoi(&id[1]);
        if (!(pid = jid2pid(jid))) {
            printf("No such job\n");
            return;
        }
    } else if (isdigit(id[0])) { // pid is given
        pid = atoi(id);
        if (!(jid = pid2jid(pid))) {
            printf("(%d): No such process\n", pid);
            return;
        }
    } else {
        printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        return;
    }

    job = &jobs[jid - 1];
    
    // Send the process group a SIGCONT signal to continue
    if (kill(-pid, SIGCONT) < 0) {
        if (errno != ESRCH) {
            unix_error("kill error\n");
        }
        return;
    }

    if (!strcmp(argv[0], "bg")) {
        printf("[%d] (%d) %s", jid, pid, job->cmdline);
        job->state = BG; // Set the job state to background
    } else if (!strcmp(argv[0], "fg")) {
        job->state = FG; // Set the job state to foreground
        fg_pid = pid;
        // Wait for the foreground job to complete
        int status;
        Waitpid(pid, &status, WUNTRACED);

        // Remove the completed job from the job list
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            fg_pid = 0; 
            remove_job(jobs, pid);
        } else if (WIFSTOPPED(status)) {
            job->state = ST; // Set the job state to stopped
        }
    }
}

void sigint_handler(int sig) {
	if (fg_pid != 0) {
	    Kill(-fg_pid, SIGINT); // send SIGINT signal to pid (process group)
    }

	return;
}

void sigchld_handler(int signum) {
    int status;
    pid_t child_pid;

    while ((child_pid = waitpid(-1, &status,  WNOHANG)) > 0) {
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            remove_job(jobs, child_pid);
            if (child_pid == fg_pid) {
                fg_pid = 0;
            }
        } else if (WIFSTOPPED(status)) {
            int jid = pid2jid(child_pid);
            jobs[jid-1].state = ST;
        } else if (WIFCONTINUED(status)) {
            int jid = pid2jid(child_pid);
            jobs[jid-1].state = FG;
        } 
    }
}

void sigtstp_handler(int sig){ // for SIGTSTP signal
    if (fg_pid != 0) {
        int jid = pid2jid(fg_pid);
        if (jid) {
            jobs[jid-1].state = ST;
            kill(-fg_pid, SIGTSTP);
        }
        printf("\n[%d] suspended %s\n", jid, jobs[jid-1].cmdline);
        fg_pid = 0; 
    }
  
    return;
}

int is_builtin_command(char **argv) {
    const char *builtin_commands[] = {"quit", "&", "cd", "history", "exit", "bg", "fg", "jobs", "kill"};
    int num_builtin_commands = sizeof(builtin_commands) / sizeof(builtin_commands[0]);

    for (int i = 0; i < num_builtin_commands; i++) {
        if (!strcmp(argv[0], builtin_commands[i])) {
            if (i == 1) return 2; // &단일 
            return 1;
        }
    }
    return 0;
}