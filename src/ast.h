// ast.h - дерево команд (AST): результат работы парсера.
// Узлов пять видов:
// NODE_SIMPLE ls -l > out слова и перенаправления
// NODE_SUBSHELL  ( список ) > out список в отдельном процессе
// NODE_PIPELINE  cmd1 | cmd2 | cmd3 конвейер
// NODE_ANDOR p1 && p2 || p3 цепочка по коду возврата
// NODE_LIST a ; b & c  список (корень дерева)
//
// Три последних устроены одинаково: это последовательность элементов
// с операторами между ними (структура Sequence). Если в конвейере (или цепочке)
// всего один элемент, отдельный узел не создаётся: в дереве лежит сам элемент.
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

typedef struct {
    Redir *items; // в порядке записи
    size_t count;
} RedirList;

// Оператор, который стоит ПОСЛЕ элемента последовательности.
typedef enum {
    OP_NONE, // после элемента ничего нет (последний элемент)
    OP_PIPE, // |
    OP_AND, // &&
    OP_OR, // ||
    OP_SEMI, // ; или перевод строки
    OP_BG // & (элемент запускается в фоне)
} Op;

// ls -l > out
typedef struct {
    char **argv; // слова; argv[argc] == NULL (так удобно для execvp)
    size_t argc;
    RedirList redirs;
} SimpleCommand;

// ( список ) > out
typedef struct {
    Node *body; // узел NODE_LIST
    RedirList redirs; // относятся ко всему списку
} Subshell;

// Элементы items[i] и оператор ops[i] после каждого из них:
// NODE_PIPELINE ops[i] - OP_PIPE (у последнего OP_NONE)
// NODE_ANDOR ops[i] - OP_AND или OP_OR между items[i] и items[i+1]
// NODE_LIST ops[i] - OP_SEMI, OP_BG или OP_NONE (нет разделителя)
typedef struct {
    Node **items;
    Op *ops;
    size_t count;
} Sequence;

typedef enum {
    NODE_SIMPLE,
    NODE_SUBSHELL,
    NODE_PIPELINE,
    NODE_ANDOR,
    NODE_LIST
} NodeType;

// Узел дерева: вид + данные нужного вида (поля union занимают одно место)
struct Node {
    NodeType type;
    union {
        SimpleCommand simple;
        Subshell subshell;
        Sequence seq; // NODE_PIPELINE, NODE_ANDOR, NODE_LIST
    };
};

// создаёт пустой узел заданного вида
Node *node_new(NodeType type);

// освобождает узел и всё, что в нём лежит (рекурсивно)
void node_free(Node *n);

// добавление в конец; владение word, file и item переходит к дереву
void simple_add_word(Node *n, char *word);
void redirs_add(RedirList *redirs, RedirType type, char *file);
void seq_add(Node *n, Node *item, Op op);

// печатает дерево в читаемом виде (режим --dump-ast)
void ast_dump(const Node *root, FILE *out);

#endif