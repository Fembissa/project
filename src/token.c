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
    list->items[list->count++] = (Token){type, text};
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

void token_list_dump(const TokenList *list, FILE *out)
{
    for (size_t i = 0; i < list->count; i++) {
        const Token *t = &list->items[i];
        fputs(TOKEN_NAMES[t->type], out);
        if (t->type == TOK_WORD) { // текст в кавычках, \ " \n экранируем
            fputs(" \"", out);
            for (const char *p = t->text; *p; p++) {
                if (*p == '"' || *p == '\\')
                    fputc('\\', out);
                if (*p == '\n')
                    fputs("\\n", out);
                else
                    fputc(*p, out);
            }
            fputc('"', out);
        }
        fputc('\n', out);
    }
}