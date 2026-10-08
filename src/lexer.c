// Идём по строке слева направо индексом i. На каждом шаге пропускаем
// пробелы и комментарий, затем смотрим на текущий символ:
//   конец строки -> TOK_EOF;  '\n' -> TOK_NEWLINE;
//-   | & ; ( ) < > -> оператор;  всё остальное -> слово.
#include "lexer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

// список лексем
//Добавляет лексему. Владение text переходит к списку (при ошибке
// памяти text освобождается здесь).
static bool tl_push(TokenList *list, TokenType type, char *text)
{
    if (list->count == list->capacity) {
        size_t cap = list->capacity ? list->capacity * 2 : 16;
        Token *p = realloc(list->items, cap * sizeof *p);
        if (p == NULL) {
            free(text);
            return false;
        }
        list->items = p;
        list->capacity = cap;
    }
    list->items[list->count++] = (Token){type, text};
    return true;
}

void token_list_free(TokenList *list)
{
    for (size_t i = 0; i < list->count; i++)
        free(list->items[i].text);
    free(list->items);
    *list = (TokenList){0};
}

// символы и пробелы

static bool is_blank(char c)
{
    return c == ' ' || c == '\t';
}

// метасимволы: вне кавычек разделяют слова
static bool is_meta(char c)
{
    return c != '\0' && (is_blank(c) || strchr("\n|&;()<>", c) != NULL);
}

// Пропускает пробелы, табуляции и "\<перевод строки>" (склейка строк)
// *cont_end запоминает, где закончилась последняя склейка
static void skip_blanks(const char *s, size_t *i, size_t *cont_end)
{
    for (;;) {
        if (is_blank(s[*i])) {
            (*i)++;
        } else if (s[*i] == '\\' && s[*i + 1] == '\n') {
            *i += 2;
            *cont_end = *i;
        } else {
            return;
        }
    }
}

// операторы
// операторы вне базы ошибка, а не молчаливый пропуск
static const char *const UNSUPPORTED_OPS[] = {
    "<<", "<>", "<&", ">&", ">|", "&>", "|&", ";;", ";&",
};

// операторы базы. Двухсимвольные стоят первыми - так выполняется
// правило максимального совпадения ("&&" раньше, чем "&")
static const struct {
    const char *text;
    TokenType type;
} OPS[] = {
    {"&&", TOK_AND}, {"||", TOK_OR}, {">>", TOK_DGREAT},
    {"|", TOK_PIPE}, {"&", TOK_AMP}, {";", TOK_SEMI},
    {"(", TOK_LPAREN}, {")", TOK_RPAREN}, {"<", TOK_LESS}, {">", TOK_GREAT},
};

// Читает оператор в начале s: возвращает его длину и вид в *type.
// возвращает 0, если оператор не поддерживается
static size_t read_operator(const char *s, TokenType *type)
{
    for (size_t k = 0; k < COUNT(UNSUPPORTED_OPS); k++) {
        if (strncmp(s, UNSUPPORTED_OPS[k], 2) == 0)
            return 0;
    }

    for (size_t k = 0; k < COUNT(OPS); k++) {
        size_t n = strlen(OPS[k].text);
        if (strncmp(s, OPS[k].text, n) == 0) {
            *type = OPS[k].type;
            return n;
        } 
    }
    return 0;
}

/// слова

// Читает слово с позиции *pos и записывает его текст (без кавычек и
// экранирования) в out. Буфер out должен быть не короче оставшегося
// ввода: слово не бывает длиннее исходного текста

// Переменная q - состояние: 0 (вне кавычек), '\'' или '"'.
//   в '...'  : всё буквально до закрывающей кавычки;
//   в "..."  : буквально, кроме \" и \\ ; "\<перевод строки>" убирается;
//   вне      : '\' экранирует следующий символ; метасимвол кончает слово.

static LexStatus read_word(const char *s, size_t *pos, char *out,
            size_t *cont_end)
{
    size_t i = *pos;
    char q = 0;

    while (s[i] != '\0') {
        char c = s[i];

        if (q == '\'') {
            if (c == '\'')
                q = 0;
            else
                *out++ = c;
        } else if (c == '\\' && s[i + 1] == '\n') { // склейка строк
            i++;
            
            *cont_end = i + 1;
        } else if (q == '"') {
            if (c == '"')
                q = 0;
            else if (c == '\\' && (s[i + 1] == '"' || s[i + 1] == '\\'))
                *out++ = s[++i];
            else
                *out++ = c;
        } else if (is_meta(c)) {
            break;
        } else if (c == '\'' || c == '"') {
            q = c;
        } else if (c == '\\' && s[i + 1] != '\0') {
            *out++ = s[++i];
            
        } else {
            *out++ = c; // обычный символ (или '\' в самом конце ввода)
        }
        i++;
    }

    *out = '\0';
    *pos = i;
    return q ? LEX_UNCLOSED_QUOTE : LEX_OK;
}
// cостоит ли n символов исходного текста только из цифр? Если за таким
// "словом" сразу идёт < или >, это "2>file" - в базе этого нет
static bool is_fd_number(const char *s, size_t n){
    if (n == 0)
        return false;

    for (size_t k = 0; k < n; k++) {
    if (!isdigit((unsigned char)s[k]))
            return false;
    }
    return true;
}

// главная функция
LexStatus lex(const char *s, TokenList *out)
{

    *out = (TokenList){0};

    char *word = malloc(strlen(s) + 1); /* буфер для текста слова */
    if (word == NULL)
        return LEX_NOMEM;

    size_t i = 0;
    size_t cont_end = (size_t)-1; // "склеек ещё не было"
    LexStatus st = LEX_OK;

    for (;;) {
        skip_blanks(s, &i, &cont_end);
        if (s[i] == '#') { // комментарий; '\n' не трогаем
            while (s[i] != '\0' && s[i] != '\n')
                i++;
        }

        TokenType type = TOK_WORD;
        char *text = NULL;

        if (s[i] == '\0') {
            if (cont_end == i) { // ввод оборвался на "\<перевод строки>"
                st = LEX_CONTINUATION;
                break;
            }
            type = TOK_EOF;
        } else if (s[i] == '\n') {
            type = TOK_NEWLINE;
            i++;

        } else if (is_meta(s[i])) {
            size_t len = read_operator(s + i, &type);
            if (len == 0) {
                st = LEX_UNSUPPORTED;
                break;
            }
            i += len;
        } else {
            size_t start = i;
            st = read_word(s, &i, word, &cont_end);
            if (st == LEX_OK && (s[i] == '<' || s[i] == '>') &&
                is_fd_number(s + start, i - start))
                st = LEX_UNSUPPORTED;
            if (st != LEX_OK)
                break;
            text = strdup(word);
           if (text == NULL) {
                st = LEX_NOMEM;
                break;
            }
        }

        if (!tl_push(out, type, text)) {
            st = LEX_NOMEM;
            break;
        }
        if (type == TOK_EOF)
            break;
    }
    free(word);
    if (st != LEX_OK)
        token_list_free(out);
    return st;
}
bool lex_status_is_incomplete(LexStatus status)
{
    return status == LEX_UNCLOSED_QUOTE || status == LEX_CONTINUATION;
}
const char *lex_status_message(LexStatus status)
{
    switch (status) {
    case LEX_OK:
        return "no error";
    case LEX_UNCLOSED_QUOTE:
        return "syntax error: unexpected EOF while looking for matching quote";
    case LEX_CONTINUATION:
        return "syntax error: unexpected EOF after line continuation";
    case LEX_UNSUPPORTED:
        return "syntax error: unsupported operator or redirection";
    case LEX_NOMEM:
        return "out of memory";
    }
    return "unknown error";
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