#include "exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

// Прототипы встроенных команд
static int is_builtin(const char *cmd);
static int exec_builtin(int argc, char **argv);

// Применение перенаправлений для простой команды
static int apply_redirections(const RedirList *redirs) {
    for (size_t i = 0; i < redirs->count; i++) {
        const Redir *r = &redirs->items[i];
        int fd = -1;

        if (r->type == REDIR_IN) {
            fd = open(r->file, O_RDONLY);
        } else if (r->type == REDIR_OUT) {
            fd = open(r->file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        } else if (r->type == REDIR_APPEND) {
            fd = open(r->file, O_WRONLY | O_CREAT | O_APPEND, 0666);
        }

        if (fd < 0) {
            fprintf(stderr, "mysh: %s: %s\n", r->file, strerror(errno));
            return -1;
        }

        int target_fd = (r->type == REDIR_IN) ? STDIN_FILENO : STDOUT_FILENO;
        if (dup2(fd, target_fd) < 0) {
            perror("mysh: dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

// Преобразование статуса waitpid в код возврата
static int status_to_exit_code(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

// Выполнение простой команды (NODE_SIMPLE)
static int exec_simple(const SimpleCommand *s) {
    if (s->argc == 0)
        return 0;

    // Проверяем, является ли команда встроенной
    if (is_builtin(s->argv[0])) {
        // Для встроенных команд без конвейера перенаправления можно применить локально
        // (в базовом варианте для простоты пока сделаем через fork или сохранение дескрипторов,
        // но давай для начала обработаем через запуск или прямым вызовом)
        int saved_stdin = dup(STDIN_FILENO);
        int saved_stdout = dup(STDOUT_FILENO);

        if (apply_redirections(&s->redirs) < 0) {
            close(saved_stdin);
            close(saved_stdout);
            return 1;
        }

        int status = exec_builtin(s->argc, s->argv);

        // Восстанавливаем стандартные потоки
        dup2(saved_stdin, STDIN_FILENO);
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdin);
        close(saved_stdout);

        return status;
    }

    // Внешняя программа
    pid_t pid = fork();
    if (pid < 0) {
        perror("mysh: fork");
        return 1;
    }

    if (pid == 0) {
        // Дочерний процесс
        if (apply_redirections(&s->redirs) < 0) {
            _exit(1);
        }

        execvp(s->argv[0], s->argv);
        
        if (errno == ENOENT) {
            fprintf(stderr, "mysh: %s: command not found\n", s->argv[0]);
            _exit(127);
        } else {
            fprintf(stderr, "mysh: %s: %s\n", s->argv[0], strerror(errno));
            _exit(126);
        }
    } else {
        int status;
        waitpid(pid, &status, 0);
        return status_to_exit_code(status);
    }
}

// Проверка встроенных команд
static int is_builtin(const char *cmd) {
    return strcmp(cmd, "cd") == 0 ||
           strcmp(cmd, "pwd") == 0 ||
           strcmp(cmd, "echo") == 0 ||
           strcmp(cmd, "exit") == 0 ||
           strcmp(cmd, "help") == 0;
}

// Выполнение встроенных команд
static int exec_builtin(int argc, char **argv) {
    if (strcmp(argv[0], "pwd") == 0) {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
            return 0;
        }
        perror("mysh: pwd");
        return 1;
    } else if (strcmp(argv[0], "echo") == 0) {
        bool newline = true;
        int start = 1;
        if (argc > 1 && strcmp(argv[1], "-n") == 0) {
            newline = false;
            start = 2;
        }
        for (int i = start; i < argc; i++) {
            printf("%s", argv[i]);
            if (i < argc - 1)
                printf(" ");
        }
        if (newline)
            printf("\n");
        return 0;
    } else if (strcmp(argv[0], "exit") == 0) {
        int code = 0;
        if (argc > 1) {
            code = atoi(argv[1]);
        }
        exit(code);
    } else if (strcmp(argv[0], "cd") == 0) {
        const char *path = (argc > 1) ? argv[1] : getenv("HOME");
        if (!path) {
            fprintf(stderr, "mysh: cd: HOME not set\n");
            return 1;
        }
        if (chdir(path) < 0) {
            fprintf(stderr, "mysh: cd: %s: %s\n", path, strerror(errno));
            return 1;
        }
        return 0;
    } else if (strcmp(argv[0], "help") == 0) {
        printf("mysh - educational UNIX shell\nBuiltins: cd, pwd, echo, exit, help\n");
        return 0;
    }
    return 1;
}

// Выполнение конвейера (NODE_PIPELINE)
static int exec_pipeline(const Sequence *seq) {
    size_t count = seq->count;
    if (count == 0) return 0;
    if (count == 1) return exec_node(seq->items[0]);

    int pipes[2 * (count - 1)];
    for (size_t i = 0; i < count - 1; i++) {
        if (pipe(pipes + i * 2) < 0) {
            perror("mysh: pipe");
            return 1;
        }
    }

    pid_t pids[count];
    int last_status = 0;

    for (size_t i = 0; i < count; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("mysh: fork");
            return 1;
        }

        if (pids[i] == 0) {
            // Дочерний процесс в конвейере
            if (i > 0) {
                dup2(pipes[(i - 1) * 2], STDIN_FILENO);
            }
            if (i < count - 1) {
                dup2(pipes[i * 2 + 1], STDOUT_FILENO);
            }

            // Закрываем все файловые дескрипторы каналов в дочернем процессе
            for (size_t j = 0; j < 2 * (count - 1); j++) {
                close(pipes[j]);
            }

                exit(exec_node(seq->items[i]));
            }
        }
        for (size_t i = 0; i < 2 * (count - 1); i++) {
        close(pipes[i]);
    }

    for (size_t i = 0; i < count; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == count - 1) {
            last_status = status_to_exit_code(status);
        }
    }

    return last_status;
}

// Главная функция обхода узлов AST
int exec_node(const Node *node) {
    if (node == NULL)
        return 0;

    switch (node->type) {
    case NODE_SIMPLE:
        return exec_simple(&node->simple);

    case NODE_SUBSHELL:
        // Подоболочка выполняется в дочернем процессе
        {
            pid_t pid = fork();
            if (pid < 0) {
                perror("mysh: fork");
                return 1;
            }
            if (pid == 0) {
                // Применяем перенаправления для подоболочки, если есть
                if (apply_redirections(&node->subshell.redirs) < 0) {
                    _exit(1);
                }
                _exit(exec_node(node->subshell.body));
            } else {
                int status;
                waitpid(pid, &status, 0);
                return status_to_exit_code(status);
            }
        }

    case NODE_PIPELINE:
        return exec_pipeline(&node->seq);

    case NODE_ANDOR:{
            int status = exec_node(node->seq.items[0]);
            for (size_t i = 0; i < node->seq.count; i++) {
                Op op = node->seq.ops[i];
                if (op == OP_NONE) break;

                Node *next_item = node->seq.items[i + 1];
                if (op == OP_AND) {
                    if (status == 0) {
                        status = exec_node(next_item);
                    }
                } else if (op == OP_OR) {
                    if (status != 0) {
                        status = exec_node(next_item);
                    }
                }
            }
            return status;
        }

    case NODE_LIST:{
            int last_status = 0;
            for (size_t i = 0; i < node->seq.count; i++) {
                Op op = node->seq.ops[i];
                if (op == OP_BG) {
                    // Фоновый запуск (&)
                    pid_t pid = fork();
                    if (pid == 0) {
                        _exit(exec_node(node->seq.items[i]));
                    }
                    // В фоновом режиме выводим сообщение вида [1] pid (упрощенно)
                    printf("[%d] PID %d started in background\n", (int)(i + 1), (int)pid);
                } else {
                    last_status = exec_node(node->seq.items[i]);
                }
            }
            return last_status;
        }
    }
    return 0;
}