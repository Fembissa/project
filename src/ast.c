// ast.c - создание, заполнение и освобождение дерева команд (см. ast.h).
#include "ast.h"

#include <stdlib.h>
#include <string.h>

#include "token.h"
#include "util.h"

static void redirs_free(Redir *redirs, size_t count);
static void dump_indent(int depth, FILE *out);
static void dump_redirs(const Redir *redirs, size_t count, int depth,
                        FILE *out);
static void dump_node(const Node *n, int depth, FILE *out);

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
        redirs_free(n->simple.redirs, n->simple.redir_count);
        break;
    case NODE_SUBSHELL:
        node_free(n->subshell.body);
        redirs_free(n->subshell.redirs, n->subshell.redir_count);
        break;
    case NODE_PIPELINE:
        for (size_t i = 0; i < n->pipeline.count; i++)
            node_free(n->pipeline.cmds[i]);
        free(n->pipeline.cmds);
        break;
    case NODE_ANDOR:
        for (size_t i = 0; i < n->andor.count; i++)
            node_free(n->andor.pipes[i]);
        free(n->andor.pipes);
        free(n->andor.ops);
        break;
    case NODE_LIST:
        for (size_t i = 0; i < n->list.count; i++)
            node_free(n->list.items[i]);
        free(n->list.items);
        free(n->list.background);
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

void redirs_add(Redir **redirs, size_t *count, RedirType type, char *file){
    *redirs = xrealloc(*redirs, (*count + 1) * sizeof **redirs);
    (*redirs)[(*count)++] = (Redir){type, file};
}

void pipeline_add(Node *n, Node *cmd){
    Pipeline *p = &n->pipeline;
    p->cmds = xrealloc(p->cmds, (p->count + 1) * sizeof *p->cmds);
    p->cmds[p->count++] = cmd;
}

void andor_add(Node *n, AndOrOp op, Node *pipe){
    AndOr *a = &n->andor;
    a->pipes = xrealloc(a->pipes, (a->count + 1) * sizeof *a->pipes);
    a->ops = xrealloc(a->ops, (a->count + 1) * sizeof *a->ops);
    a->pipes[a->count] = pipe;
    a->ops[a->count] = op;
    a->count++;
}

void list_add(Node *n, Node *item, bool background){
    CommandList *l = &n->list;
    l->items = xrealloc(l->items, (l->count + 1) * sizeof *l->items);
    l->background = xrealloc(l->background,
                             (l->count + 1) * sizeof *l->background);
    l->items[l->count] = item;
    l->background[l->count] = background;
    l->count++;
}

void ast_dump(const Node *root, FILE *out)
{
    dump_node(root, 0, out);
}

static void redirs_free(Redir *redirs, size_t count)
{
    for (size_t i = 0; i < count; i++)
        free(redirs[i].file);
    free(redirs);
}

// отступ: два пробела на каждый уровень вложенности
static void dump_indent(int depth, FILE *out)
{
    for (int i = 0; i < depth; i++)
        fputs("  ", out);
}

static void dump_redirs(const Redir *redirs, size_t count, int depth,
                        FILE *out)
{
    static const char *const SIGNS[] = {
        [REDIR_IN] = "<", [REDIR_OUT] = ">", [REDIR_APPEND] = ">>",
    };
    for (size_t i = 0; i < count; i++) {
        dump_indent(depth, out);
        fprintf(out, "REDIR %s ", SIGNS[redirs[i].type]);
        print_quoted(redirs[i].file, out);
        fputc('\n', out);
    }
}

// печатает узел, затем его детей на уровень глубже
static void dump_node(const Node *n, int depth, FILE *out)
{
    dump_indent(depth, out);
    switch (n->type) {
    case NODE_SIMPLE:
        fputs("SIMPLE", out);
        for (size_t i = 0; i < n->simple.argc; i++) {
            fputc(' ', out);
            print_quoted(n->simple.argv[i], out);
        }
        fputc('\n', out);
        dump_redirs(n->simple.redirs, n->simple.redir_count, depth + 1, out);
        break;
    case NODE_SUBSHELL:
        fputs("SUBSHELL\n", out);
        dump_node(n->subshell.body, depth + 1, out);
        dump_redirs(n->subshell.redirs, n->subshell.redir_count, depth + 1,
                    out);
        break;
    case NODE_PIPELINE:
        fputs("PIPELINE\n", out);
        for (size_t i = 0; i < n->pipeline.count; i++)
            dump_node(n->pipeline.cmds[i], depth + 1, out);
        break;
    case NODE_ANDOR:
        fputs("AND_OR\n", out);
        for (size_t i = 0; i < n->andor.count; i++) {
            if (i > 0) { // оператор печатаем между соседними командами
                dump_indent(depth + 1, out);
                fputs(n->andor.ops[i] == ANDOR_AND ? "AND\n" : "OR\n", out);
            }
            dump_node(n->andor.pipes[i], depth + 1, out);
        }
        break;
    case NODE_LIST:
        fputs("LIST\n", out);
        for (size_t i = 0; i < n->list.count; i++) {
            dump_indent(depth + 1, out);
            fputs(n->list.background[i] ? "ITEM bg\n" : "ITEM fg\n", out);
            dump_node(n->list.items[i], depth + 2, out);
        }
        break;
    }
}