// token.c - работа со списком лексем 
#include "token.h"

#include <stdlib.h>

#include "util.h"

void token_list_push(TokenList *list, TokenType type, char *text)
{
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 16;
        list->items = xrealloc(list->items,
                               list->capacity * sizeof *list->items);
    }
    list->items[list->count++] = (Token){.type = type, .text = text};
}

void token_list_free(TokenList *list)
{
    for (size_t i = 0; i < list->count; i++)
        free(list->items[i].text);
    free(list->items);
    *list = (TokenList){0};
}

static const char *const TOKEN_NAMES[] = {
    [TOK_WORD] = "WORD",       [TOK_PIPE] = "PIPE",     [TOK_AMP] = "AMP",
    [TOK_SEMI] = "SEMI",       [TOK_AND] = "AND",       [TOK_OR] = "OR",
    [TOK_LPAREN] = "LPAREN",   [TOK_RPAREN] = "RPAREN", [TOK_LESS] = "LESS",
    [TOK_GREAT] = "GREAT",     [TOK_DGREAT] = "DGREAT",
    [TOK_NEWLINE] = "NEWLINE", [TOK_EOF] = "EOF",
};

void print_quoted(const char *text, FILE *out)
{
    fputc('"', out);
    for (const char *p = text; *p; p++) {
        if (*p == '"' || *p == '\\')
            fputc('\\', out);
        if (*p == '\n')
            fputs("\\n", out);
        else
            fputc(*p, out);
    }
    fputc('"', out);
}

void token_list_dump(const TokenList *list, FILE *out)
{
    for (size_t i = 0; i < list->count; i++) {
        const Token *t = &list->items[i];
        fputs(TOKEN_NAMES[t->type], out);
        if (t->type == TOK_WORD) {
            fputc(' ', out);
            print_quoted(t->text, out);
        }
        fputc('\n', out);
    }
}

static const char *const TOKEN_SYMBOLS[] = {
    [TOK_PIPE] = "|",      [TOK_AMP] = "&",       [TOK_SEMI] = ";",
    [TOK_AND] = "&&",      [TOK_OR] = "||",       [TOK_LPAREN] = "(",
    [TOK_RPAREN] = ")",    [TOK_LESS] = "<",      [TOK_GREAT] = ">",
    [TOK_DGREAT] = ">>",   [TOK_NEWLINE] = "newline",
    [TOK_EOF] = "end of input",
};

const char *token_text(const Token *t)
{
    return t->type == TOK_WORD ? t->text : TOKEN_SYMBOLS[t->type];
}