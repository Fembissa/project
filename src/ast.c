// ast.c - создание, заполнение, освобождение и печать дерева
#include "ast.h"

#include <stdlib.h>
#include <string.h>

#include "token.h"
#include "util.h"

static void redirs_free(RedirList *redirs);
static void dump_node(const Node *n, int depth, FILE *out);
static void dump_redirs(const RedirList *redirs, int depth, FILE *out);
static void dump_indent(int depth, FILE *out);

Node *node_new(NodeType type){
    Node *n = xmalloc(sizeof *n);
    memset(n, 0, sizeof *n); // обнуляем всё: пустые массивы, count == 0
    n->type = type;
    return n;
}

void node_free(Node *n){
    if (n == NULL)
        return;

    switch (n->type) {
    case NODE_SIMPLE:
        for (size_t i = 0; i < n->simple.argc; i++)
            free(n->simple.argv[i]);
        free(n->simple.argv);
        redirs_free(&n->simple.redirs);
        break;
    case NODE_SUBSHELL:
        node_free(n->subshell.body);
        redirs_free(&n->subshell.redirs);
        break;
    case NODE_PIPELINE:
        
    case NODE_ANDOR:
        
    case NODE_LIST:
        for (size_t i = 0; i < n->seq.count; i++)
            node_free(n->seq.items[i]);
        free(n->seq.items);
        free(n->seq.ops);
        break;
    }
    free(n);
}

// Во всех add-функциях массив растёт на один элемент (realloc).
// Для учебного проекта этого достаточно.

void simple_add_word(Node *n, char *word){
    SimpleCommand *s = &n->simple;
    s->argv = xrealloc(s->argv, (s->argc + 2) * sizeof *s->argv);
    s->argv[s->argc++] = word;
    s->argv[s->argc] = NULL; // завершающий NULL для execvp
}

void redirs_add(RedirList *redirs, RedirType type, char *file){
    redirs->items = xrealloc(redirs->items,
                             (redirs->count + 1) * sizeof *redirs->items);
    redirs->items[redirs->count++] = (Redir){.type = type, .file = file};
}

void seq_add(Node *n, Node *item, Op op){
    Sequence *s = &n->seq;
    s->items = xrealloc(s->items, (s->count + 1) * sizeof *s->items);
    s->ops = xrealloc(s->ops, (s->count + 1) * sizeof *s->ops);
    s->items[s->count] = item;
    s->ops[s->count++] = op;
}

void ast_dump(const Node *root, FILE *out){
    dump_node(root, 0, out);
}

static void redirs_free(RedirList *redirs){
    for (size_t i = 0; i < redirs->count; i++)
        free(redirs->items[i].file);
    free(redirs->items);
}

// отступ: два пробела на каждый уровень вложенности
static void dump_indent(int depth, FILE *out){
    for (int i = 0; i < depth; i++)
        fputs("  ", out);
}

static void dump_redirs(const RedirList *redirs, int depth, FILE *out){
    static const char *const SIGNS[] = {
        [REDIR_IN] = "<", [REDIR_OUT] = ">", [REDIR_APPEND] = ">>",
    };
    for (size_t i = 0; i < redirs->count; i++) {
        dump_indent(depth, out);
        fprintf(out, "REDIR %s ", SIGNS[redirs->items[i].type]);
        print_quoted(redirs->items[i].file, out);
        fputc('\n', out);
    }
}

// печатает узел, затем его детей на уровень глубже
static void dump_node(const Node *n, int depth, FILE *out){
    static const char *const TITLES[] = {
        [NODE_PIPELINE] = "PIPELINE", [NODE_ANDOR] = "AND_OR",
        [NODE_LIST] = "LIST",
    };

    dump_indent(depth, out);
    switch (n->type) {
    case NODE_SIMPLE:
        fputs("SIMPLE", out);
        for (size_t i = 0; i < n->simple.argc; i++) {
            fputc(' ', out);
            print_quoted(n->simple.argv[i], out);
        }
        fputc('\n', out);
        dump_redirs(&n->simple.redirs, depth + 1, out);
        break;
    case NODE_SUBSHELL:
        fputs("SUBSHELL\n", out);
        dump_node(n->subshell.body, depth + 1, out);
        dump_redirs(&n->subshell.redirs, depth + 1, out);
        break;
    case NODE_PIPELINE:
        
    case NODE_ANDOR:
        
    case NODE_LIST:
        fprintf(out, "%s\n", TITLES[n->type]);
        for (size_t i = 0; i < n->seq.count; i++) {
            Op op = n->seq.ops[i];
            if (n->type == NODE_LIST) {
                dump_indent(depth + 1, out);
                fputs(op == OP_BG ? "ITEM bg\n" : "ITEM fg\n", out);
                dump_node(n->seq.items[i], depth + 2, out);
            } else {
                dump_node(n->seq.items[i], depth + 1, out);
                if (op == OP_AND || op == OP_OR) { // оператор между соседями
                    dump_indent(depth + 1, out);
                    fputs(op == OP_AND ? "AND\n" : "OR\n", out);
                }
            }
        }
        break;
    }
}