// ast.h - дерево команд (AST): результат работы парсера.
// Узлов пять видов. Простая команда и подоболочка - "листья команд",
// остальные три собирают их в более крупные конструкции:
// NODE_SIMPLE ls -l > out слова и перенаправления
// NODE_SUBSHELL  ( список ) > out список в отдельном процессе
// NODE_PIPELINE  cmd1 | cmd2 | cmd3 конвейер
// NODE_ANDOR p1 && p2 || p3 цепочка по коду возврата
// NODE_LIST a ; b & c  список (корень дерева)
//
// Если конвейер состоит из одной команды, отдельный узел NODE_PIPELINE
// не создаётся (то же с NODE_ANDOR): в дереве лежит сама команда.
#ifndef MY_AST_H
#define MY_AST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct Node Node;

// перенаправление: < файл, > файл, >> файл
typedef enum { REDIR_IN, REDIR_OUT, REDIR_APPEND } RedirType;

typedef struct {
    RedirType type;
    char *file;
} Redir;

// операторы цепочки
typedef enum { ANDOR_AND, ANDOR_OR } AndOrOp;

typedef enum {
    NODE_SIMPLE,
    NODE_SUBSHELL,
    NODE_PIPELINE,
    NODE_ANDOR,
    NODE_LIST
} NodeType;

// ls -l > out
typedef struct {
    char **argv; // слова; argv[argc] == NULL (так удобно для execvp)
    size_t argc;
    Redir *redirs; // перенаправления в порядке записи
    size_t redir_count;
} SimpleCommand;

// ( список ) > out
typedef struct {
    Node *body; // узел NODE_LIST
    Redir *redirs;
    size_t redir_count;
} Subshell;

// cmd1 | cmd2 | cmd3
typedef struct {
    Node **cmds; // NODE_SIMPLE или NODE_SUBSHELL
    size_t count;
} Pipeline;

// p1 && p2 || p3
typedef struct {
    Node **pipes; // NODE_PIPELINE, NODE_SIMPLE или NODE_SUBSHELL
    AndOrOp *ops; // ops[i] стоит перед pipes[i]; ops[0] не используется
    size_t count;
} AndOr;

// a ; b & c
typedef struct {
    Node **items; // что выполнять
    bool *background; // background[i] == true, если после items[i] стоит &
    size_t count;
} CommandList;

// Узел дерева: вид + данные нужного вида (поля union занимают одно место)
struct Node {
    NodeType type;
    union {
        SimpleCommand simple;
        Subshell subshell;
        Pipeline pipeline;
        AndOr andor;
        CommandList list;
    };
};

// создаёт пустой узел заданного вида
Node *node_new(NodeType type);

// освобождает узел и всё, что в нём лежит (рекурсивно)
void node_free(Node *n);

// добавление в конец; владение word, file и вложенными узлами
// переходит к дереву
void simple_add_word(Node *n, char *word);
void redirs_add(Redir **redirs, size_t *count, RedirType type, char *file);
void pipeline_add(Node *n, Node *cmd);
void andor_add(Node *n, AndOrOp op, Node *pipe);
void list_add(Node *n, Node *item, bool background);

// печатает дерево в читаемом виде (режим --dump-ast)
void ast_dump(const Node *root, FILE *out);

#endif