#include "lexer.h"

#include <stdlib.h>

// работа со списком лексем

#define TOKENLIST_INITIAL_CAP 16

// добавляет лексему в список. Владение text переходит к списку
static bool tl_push(TokenList *list, TokenType type, char *text)
{
    if (list->count == list->capacity) {
        size_t new_cap = list->capacity ? list->capacity * 2
                                        : TOKENLIST_INITIAL_CAP;
        Token *p = realloc(list->items, new_cap * sizeof(Token));
        if (p == NULL) {
            free(text);
            return false;
        }
        list->items = p;
        list->capacity = new_cap;
    }
    list->items[list->count].type = type;
    list->items[list->count].text = text;
    list->count++;
    return true;
}

void token_list_free(TokenList *list)
{
    for (size_t i = 0; i < list->count; i++)
        free(list->items[i].text);
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

// главная функция
LexStatus lex(const char *s, TokenList *out)
{
    (void)s; /* текст пока не читаем */

    out->items = NULL;
    out->count = 0;
    out->capacity = 0;

    if (!tl_push(out, TOK_EOF, NULL))
        return LEX_NOMEM;
    return LEX_OK;
}

const char *lex_status_message(LexStatus status)
{
    switch (status) {
    case LEX_OK:
        return "no error";
    case LEX_NOMEM:
        return "out of memory";
    }
    return "unknown error";
}

static const char *token_type_name(TokenType t)
{
    switch (t) {
    case TOK_WORD:    return "WORD";
    case TOK_PIPE:    return "PIPE";
    case TOK_AMP:     return "AMP";
    case TOK_SEMI:    return "SEMI";
    case TOK_AND:     return "AND";
    case TOK_OR:      return "OR";
    case TOK_LPAREN:  return "LPAREN";
    case TOK_RPAREN:  return "RPAREN";
    case TOK_LESS:    return "LESS";
    case TOK_GREAT:   return "GREAT";
    case TOK_DGREAT:  return "DGREAT";
    case TOK_NEWLINE: return "NEWLINE";
    case TOK_EOF:     return "EOF";
    }
    return "?";
}

// печатает текст слова в кавычках, спецсимволы - в виде \n, \t
static void dump_text(const char *text, FILE *out)
{
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == '"' || *p == '\\')
            fprintf(out, "\\%c", *p);
        else if (*p == '\n')
            fputs("\\n", out);
        else if (*p == '\t')
            fputs("\\t", out);
        else if (*p < 0x20 || *p == 0x7f)
            fprintf(out, "\\x%02x", *p);
        else
            fputc(*p, out);
    }
    fputc('"', out);
}

void token_list_dump(const TokenList *list, FILE *out)
{
    for (size_t i = 0; i < list->count; i++) {
        const Token *t = &list->items[i];
        fputs(token_type_name(t->type), out);
        if (t->type == TOK_WORD) {
            fputc(' ', out);
            dump_text(t->text, out);
        }
        fputc('\n', out);
    }
}