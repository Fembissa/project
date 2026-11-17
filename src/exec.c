// exec.c - исполнитель: обходит дерево команд и запускает процессы.
//
// Внешняя программа: fork создаёт копию процесса, в копии execve заменяет образ на программу,
// родитель ждёт её (waitpid). Встроенные команды (cd, exit ...) выполняются в самом shell:
// иначе изменился бы только дочерний процесс. Главная функция - первая, вспомогательные ниже.

#include "exec.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "lexer.h"

extern char **environ; // окружение: дочерний процесс получает такое же
// коды возврата 
enum { ST_FAIL = 1, ST_CANNOT_EXEC = 126, ST_NOT_FOUND = 127, ST_SIGNAL = 128 };
#define DEFAULT_PATH "/usr/local/bin:/usr/bin:/bin" // если PATH не задан
#define UNUSED [[maybe_unused]]
#define err(...) fprintf(stderr, "mysh: " __VA_ARGS__)
typedef int (*BuiltinFn)(Shell *sh, int argc, char **argv);

// вспомогательные функции определены ниже
static BuiltinFn find_builtin(const char *name);
static int exec_builtin(Shell *sh, const SimpleCommand *s, BuiltinFn fn);
static int exec_job(Shell *sh, const Node *node, bool bg);
// главная функция
int exec_node(Shell *sh, const Node *node){
    const Sequence *s = &node->seq; // для PIPELINE, ANDOR, LIST
    int st;
    switch (node->type) {
    case NODE_SIMPLE: {
        BuiltinFn fn = find_builtin(node->simple.argv[0]);
        return fn ? exec_builtin(sh, &node->simple, fn) : exec_job(sh, node, false);
    }
    case NODE_SUBSHELL: // ( список ) всегда в отдельном процессе
    case NODE_PIPELINE:
        return exec_job(sh, node, false);
    case NODE_ANDOR: // слева направо; следующая команда пропускается, если по коду не нужна
        st = exec_node(sh, s->items[0]);
        for (size_t i = 0; i + 1 < s->count && !sh->exit_requested; i++) {
            sh->last_status = st;
            if (s->ops[i] == OP_AND ? st == 0 : st != 0)
                st = exec_node(sh, s->items[i + 1]);
        }
        return st;
    case NODE_LIST: // команды по очереди; пустой список код не меняет
        st = sh->last_status;
        for (size_t i = 0; i < s->count && !sh->exit_requested; i++) {
            while (waitpid(-1, NULL, WNOHANG) > 0) // подбираем завершившиеся фоновые процессы
                ;
            st = s->ops[i] == OP_BG ? exec_job(sh, s->items[i], true) : exec_node(sh, s->items[i]);
            sh->last_status = st;
        }
        return st;
    }
    return ST_FAIL;
}

// закрывает дескриптор (отрицательный пропускается), об ошибке сообщает
static void close_fd(int fd){
    if (fd >= 0 && close(fd) < 0)
        err("close: %s\n", strerror(errno));
}

// делает дескриптор to копией from и закрывает from; 0 или -1
static int move_fd(int from, int to){
    if (from == to) // уже на месте: закрывать нельзя
        return 0;
    while (dup2(from, to) < 0) {
        if (errno != EINTR)
            return -1;
    }
    close_fd(from);
    return 0;
}

// Применяет перенаправления слева направо; 0 или -1 (сообщение уже напечатано).
static int apply_redirections(const RedirList *redirs){
    for (size_t i = 0; i < redirs->count; i++) {
        const Redir *r = &redirs->items[i];
        bool in = r->type == REDIR_IN; // < читает в stdin; > и >> пишут из stdout
        int flags = in ? O_RDONLY : O_WRONLY | O_CREAT | (r->type == REDIR_OUT ? O_TRUNC : O_APPEND);
        int fd;
        do
            fd = open(r->file, flags, 0666); // права 0666 с учётом umask (ТЗ); EINTR - повтор
        while (fd < 0 && errno == EINTR);
        if (fd < 0 || move_fd(fd, in ? STDIN_FILENO : STDOUT_FILENO) < 0) {
            err("%s: %s\n", r->file, strerror(errno));
            close_fd(fd);
            return -1;
        }
    }
    return 0;
}

// Ждёт pid; в *code - код возврата (WEXITSTATUS или 128 + сигнал). 0 или -1 (errno, напр. ECHILD).
static int wait_pid(pid_t pid, int *code){
    int ws;
    while (waitpid(pid, &ws, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    *code = WIFSIGNALED(ws) ? ST_SIGNAL + WTERMSIG(ws) : WEXITSTATUS(ws);
    return 0;
}

// Завершает дочерний процесс: возврата в цикл shell быть не должно.
[[noreturn]] static void child_exit(int status){
    fflush(stdout); // _exit сам не сбрасывает буфер stdio
    _exit(status);
}

// Заменяет процесс на программу argv[0]; не вышло - причина и код 126 (найдена, но не
// запускается) или 127 (не найдена). Свой поиск по PATH и execve вместо execvp: тот при
// неверном формате файла запустил бы его через /bin/sh и вернул 0, а ТЗ требует 126.
[[noreturn]] static void exec_external(char **argv){
    const char *name = argv[0], *dir = getenv("PATH");
    bool slash = strchr(name, '/') != NULL; // имя с "/" ищется без PATH
    int fail = ENOENT; // ENOENT - нигде не нашли
    char file[PATH_MAX];

    for (dir = dir ? dir : DEFAULT_PATH; dir != NULL;) {
        const char *colon = slash ? NULL : strchr(dir, ':');
        int len = slash ? 0 : colon ? (int)(colon - dir) : (int)strlen(dir);
        snprintf(file, sizeof file, "%.*s%s%s", len, dir, len ? "/" : "", name); // len 0 - само имя
        execve(file, argv, environ);
        if (errno != ENOENT && (slash || errno != ENOTDIR))
            fail = errno; // файл есть, но не запустился - запоминаем причину
        dir = colon ? colon + 1 : NULL;
    }
    err("%s: %s\n", name, fail == ENOENT && !slash ? "command not found" : strerror(fail));
    child_exit(fail == ENOENT ? ST_NOT_FOUND : ST_CANNOT_EXEC);
}
// cd [dir]: без аргумента - в HOME; аргумент не больше одного
static int builtin_cd(UNUSED Shell *sh, int argc, char **argv){
    const char *dir = argc > 1 ? argv[1] : getenv("HOME");
    if (argc > 2)
        err("cd: too many arguments\n");
    else if (dir == NULL)
        err("cd: HOME not set\n");
    else if (chdir(dir) < 0)
        err("cd: %s: %s\n", dir, strerror(errno));
    else
        return 0;
    return ST_FAIL;
}

// pwd: абсолютный путь (getcwd); опций нет
static int builtin_pwd(UNUSED Shell *sh, int argc, char **argv){
    char cwd[PATH_MAX];
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] != '\0') { err("pwd: %s: invalid option\n", argv[1]); return 2; }
    if (getcwd(cwd, sizeof cwd) == NULL) { err("pwd: %s\n", strerror(errno)); return ST_FAIL; }
    puts(cwd);
    return 0;
}

// echo [-n] [аргументы]: через пробел, в конце перевод строки; \n, \t не обрабатываются
static int builtin_echo(UNUSED Shell *sh, int argc, char **argv){
    int i = 1;
    bool newline = true;
    while (i < argc && argv[i][0] == '-' && argv[i][1] == 'n' && // "-n", "-nn" ... как в bash
           argv[i][1 + strspn(argv[i] + 1, "n")] == '\0') {
        newline = false;
        i++;
    }
    for (; i < argc; i++)
        printf("%s%s", argv[i], i < argc - 1 ? " " : "");
    if (newline)
        putchar('\n');
    return 0;
}

static int builtin_help(UNUSED Shell *sh, UNUSED int argc, UNUSED char **argv){
    puts("built-in commands:\n  cd [dir]       change directory (HOME by default)\n"
         "  pwd            print the current directory\n  echo [-n] ...  print arguments (-n: no newline)\n"
         "  help           show this list\n  exit [code]    leave the shell (default: last status)\n"
         "  wait [pid]     wait for background processes");
    return 0;
}

// exit [code]: сама exit() не вызывает, а просит об этом (exit_requested): main выйдет сам и
// освободит память. Без аргумента - код последней команды (ТЗ); код приводится к 0..255.
static int builtin_exit(Shell *sh, int argc, char **argv)
{
    char *end;
    errno = 0;
    long v = argc > 1 ? strtol(argv[1], &end, 10) : sh->last_status;
    if (argc > 1 && (*argv[1] == '\0' || *end != '\0' || errno == ERANGE)) {
        err("exit: %s: numeric argument required\n", argv[1]);
        v = 2; // как в bash: выходим всё равно
    } else if (argc > 2) {
        err("exit: too many arguments\n");
        return ST_FAIL; // не выходим
    }
    sh->exit_requested = true;
    return (int)(((v % 256) + 256) % 256);
}

// wait [pid]: без аргумента ждёт все фоновые процессы (код 0); с pid - этот процесс
// и возвращает его код. Задания (%n) появятся вместе с таблицей заданий.
static int builtin_wait(UNUSED Shell *sh, int argc, char **argv){
    int status = 0;
    char *end;
    long pid = argc > 1 ? strtol(argv[1], &end, 10) : 0;
    if (argc == 1) {
        while (waitpid(-1, NULL, 0) > 0 || errno == EINTR)
            ;
    } else if (*argv[1] == '\0' || *end != '\0' || pid <= 0) {
        err("wait: `%s': not a pid or valid job spec\n", argv[1]);
        status = ST_FAIL;
    } else if (wait_pid((pid_t)pid, &status) < 0) {
        err("wait: pid %ld is not a child of this shell\n", pid);
        status = ST_NOT_FOUND;
    }
    return status;
}

static BuiltinFn find_builtin(const char *name){
    static const struct { const char *name; BuiltinFn fn; } TABLE[] = {
        {"cd", builtin_cd}, {"pwd", builtin_pwd}, {"echo", builtin_echo},
        {"help", builtin_help}, {"exit", builtin_exit}, {"wait", builtin_wait},
    };
    for (size_t i = 0; i < sizeof TABLE / sizeof *TABLE; i++) {
        if (strcmp(name, TABLE[i].name) == 0)
            return TABLE[i].fn;
    }
    return NULL;
}

// Встроенная команда выполняется в самом shell (без fork). Перенаправления применяются
// здесь же: stdin и stdout сохраняются (dup) и после команды ВСЕГДА возвращаются.
static int exec_builtin(Shell *sh, const SimpleCommand *s, BuiltinFn fn){
    bool redirected = s->redirs.count > 0;
    int saved[2] = {redirected ? dup(0) : -1, redirected ? dup(1) : -1};
    int status = apply_redirections(&s->redirs) < 0 ? ST_FAIL : fn(sh, (int)s->argc, s->argv); // ТЗ: ошибка файла - код 1
    if (fflush(stdout) != 0 || ferror(stdout)) { // пока stdout ещё перенаправлен
        err("%s: write error: %s\n", s->argv[0], strerror(errno));
        clearerr(stdout);
        status = status ? status : ST_FAIL;
    }
    for (int i = 0; i < 2; i++) {
        if (saved[i] >= 0 && move_fd(saved[i], i) < 0)
            err("restoring descriptor %d: %s\n", i, strerror(errno));
    }
    return status;
}

// Код дочернего процесса. Перенаправления первыми (они сильнее каналов). Подоболочка
// выполняет в этом процессе весь свой список, поэтому её cd и exit родителя не затрагивают.
[[noreturn]] static void child_run(Shell *sh, const Node *node){
    if (node->type == NODE_SUBSHELL) {
        if (apply_redirections(&node->subshell.redirs) < 0)
            child_exit(ST_FAIL);
        child_exit(exec_node(sh, node->subshell.body));
    }
    if (node->type != NODE_SIMPLE) // например "a && b &": цепочка целиком
        child_exit(exec_node(sh, node));

    const SimpleCommand *s = &node->simple;
    if (apply_redirections(&s->redirs) < 0)
        child_exit(ST_FAIL);
    BuiltinFn fn = find_builtin(s->argv[0]);
    if (fn != NULL) // встроенная в конвейере или в фоне работает в ребёнке
        child_exit(fn(sh, (int)s->argc, s->argv));
    exec_external(s->argv);
}

// Запускает node в дочернем процессе. in_fd / out_fd станут его stdin / stdout (-1: не
// трогать), unused - ненужный ему конец канала, bg - команда в фоне. Возвращает pid или -1.
static pid_t spawn(Shell *sh, const Node *node, int in_fd, int out_fd, int unused, bool bg){
    fflush(NULL); // иначе несброшенный буфер stdio попадёт и в копию процесса
    pid_t pid = fork();
    if (pid != 0) {
        if (pid < 0)
            err("fork: %s\n", strerror(errno));
        return pid;
    }

    // дальше мы в дочернем процессе
    close_fd(unused);
    if (bg) { // фоновая команда не слушает Ctrl-C и не читает ввод shell
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        if (in_fd < 0 && (in_fd = open("/dev/null", O_RDONLY)) < 0)
            child_exit(ST_FAIL);
    }
    if ((in_fd >= 0 && move_fd(in_fd, STDIN_FILENO) < 0) ||
        (out_fd >= 0 && move_fd(out_fd, STDOUT_FILENO) < 0)) {
        err("dup2: %s\n", strerror(errno));
        child_exit(ST_FAIL);
    }
    child_run(sh, node);
}

// Запускает узел как задание: конвейер cmd1 | cmd2 | ... или одну команду. Процессы стартуют
// сразу, stdout каждого соединён через pipe со stdin следующего. Каждый процесс держит
// открытыми только свои концы каналов: иначе читающий не получит конец файла и конвейер
// зависнет. Код - у последней команды. bg: не ждать (код 0).
static int exec_job(Shell *sh, const Node *node, bool bg){
    bool is_pipeline = node->type == NODE_PIPELINE;
    size_t n = is_pipeline ? node->seq.count : 1, started = 0;
    pid_t *pids = xmalloc(n * sizeof *pids);
    int prev_read = -1; // читающий конец прошлого канала: stdin очередной команды
    for (size_t i = 0; i < n; i++) {
        int fds[2] = {-1, -1}; // канал к следующей команде: [0] чтение, [1] запись
        if (i + 1 < n && pipe(fds) < 0) {
            err("pipe: %s\n", strerror(errno));
            break;
        }
        pid_t pid = spawn(sh, is_pipeline ? node->seq.items[i] : node, prev_read, fds[1], fds[0], bg);
        close_fd(prev_read); // родителю эти концы больше не нужны
        close_fd(fds[1]);
        prev_read = fds[0]; // его получит следующая команда
        if (pid < 0)
            break;
        pids[started++] = pid;
    }
    close_fd(prev_read); // остался открытым, если вышли по ошибке
    int status = (bg && started == n) ? 0 : ST_FAIL;
    for (size_t i = 0; i < started && !bg; i++) { // ждём всех, иначе останутся зомби
        int st = ST_FAIL;
        if (wait_pid(pids[i], &st) < 0)
            err("waitpid: %s\n", strerror(errno));
        if (i + 1 == n)
            status = st;
    }
    free(pids);
    return status;
}
