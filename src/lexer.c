#include "lexer.h"

#include <stdlib.h>
#include <string.h>

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

// растущий буфер, в который собирается текст слова
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

// размер, с которого начинается буфер (потом удваивается)
#define STRBUF_INITIAL_CAP 16

// добавляет символ в конец буфера; false - не хватило памяти
static bool sb_push(StrBuf *sb, char c)
{
    if (sb->len + 1 >= sb->cap) { /* +1: место под завершающий '\0' */
        size_t new_cap = sb->cap ? sb->cap * 2 : STRBUF_INITIAL_CAP;
        char *p = realloc(sb->data, new_cap);
        if (p == NULL)
            return false;
        sb->data = p;
        sb->cap = new_cap;
    }
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
    return true;
}

// отдаёт готовую строку вызывающему
// пустое слово ('' или "") даёт "", а не NULL
static char *sb_finish(StrBuf *sb)
{
    if (sb->data == NULL)
        return calloc(1, 1);
    char *res = sb->data;
    sb->data = NULL;
    return res;
}

// классификация символов

static bool is_blank(char c)
{
    return c == ' ' || c == '\t';
}

// метасимволы: разделяют слова вне кавычек
static bool is_meta(char c)
{
    return c != '\0' && (is_blank(c) || strchr("\n|&;()<>", c) != NULL);
}

// операторы

//Читает оператор в начале s. В *type кладёт его вид, в *len - длину.
// сначала варианты (&&, ||, >>), потом односимвольные.

static void read_operator(const char *s, TokenType *type, size_t *len)
{
    *len = 1;
    switch (s[0]) {
    case '|':
        if (s[1] == '|') { *type = TOK_OR; *len = 2; }
        else             { *type = TOK_PIPE; }
        break;
    case '&':
        if (s[1] == '&') { *type = TOK_AND; *len = 2; }
        else             { *type = TOK_AMP; }
        break;
    case '>':
        if (s[1] == '>') { *type = TOK_DGREAT; *len = 2; }
        else             { *type = TOK_GREAT; }
        break;
    case '<': *type = TOK_LESS;    break;
    case ';': *type = TOK_SEMI;    break;
    case '(': *type = TOK_LPAREN; break;
    case ')': *type = TOK_RPAREN; break;
    default:  *type = TOK_EOF;     break; /* сюда попасть нельзя */
    }
}

/// слова

// Читает одно слово, начиная с s[*pos], и складывает его текст в buf.
// По выходу *pos указывает на первый символ после слова.

static LexStatus read_word(const char *s, size_t *pos, StrBuf *buf)
{
    size_t i = *pos;

    while (s[i] != '\0' && !is_meta(s[i])) {
        char c = s[i];

        if (c == '\'') {
            /* Одинарные кавычки: всё буквально до следующей '. */
            i++;
            while (s[i] != '\'') {
                if (s[i] == '\0')
                    return LEX_UNCLOSED_SINGLE;
                if (!sb_push(buf, s[i]))
                    return LEX_NOMEM;
                i++;
            }
            i++; /* закрывающая кавычка */
        } else {
            if (!sb_push(buf, c))
                return LEX_NOMEM;
            i++;
        }
    }

    *pos = i;
    return LEX_OK;
}

// главная функция
LexStatus lex(const char *s, TokenList *out)
{

    out->items = NULL;
    out->count = 0;
    out->capacity = 0;

    size_t i = 0;
    LexStatus st = LEX_OK;

    for (;;) {
        while (is_blank(s[i]))
            i++;

        if (s[i] == '\0') {
            if (!tl_push(out, TOK_EOF, NULL)) {
                st = LEX_NOMEM;
                goto fail;
            }
            return LEX_OK;
        }

        if (s[i] == '\n') {
            if (!tl_push(out, TOK_NEWLINE, NULL)) {
                st = LEX_NOMEM;
                goto fail;
            }
            i++;
            continue;
        }

        if (is_meta(s[i])) {
            TokenType type;
            size_t len;
            read_operator(s + i, &type, &len);
            if (!tl_push(out, type, NULL)) {
                st = LEX_NOMEM;
                goto fail;
            }
            i += len;
            continue;
        }

        // иначе начинается слово
        StrBuf buf = {0};
        st = read_word(s, &i, &buf);
        if (st != LEX_OK) {
            free(buf.data);
            goto fail;
        }
        char *text = sb_finish(&buf);
        if (text == NULL || !tl_push(out, TOK_WORD, text)) {
            st = LEX_NOMEM;
            goto fail;
        }
    }

fail:
    token_list_free(out);
    return st;
}
bool lex_status_is_incomplete(LexStatus status)
{
    return status == LEX_UNCLOSED_SINGLE;
}
const char *lex_status_message(LexStatus status)
{
    switch (status) {
    case LEX_OK:
        return "no error";
    case LEX_UNCLOSED_SINGLE:
        return "syntax error: unexpected EOF while looking for matching `''";
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