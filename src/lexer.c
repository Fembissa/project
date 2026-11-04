// Идём по строке слева направо индексом i. На каждом шаге пропускаем
// пробелы и комментарий, затем смотрим на текущий символ:
//   конец строки -> TOK_EOF;  '\n' -> TOK_NEWLINE;
//-   | & ; ( ) < > -> оператор;  всё остальное -> слово.
#include "lexer.h"

#include <stdlib.h>
#include <string.h>
#include "util.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static bool is_meta(char c);
static void skip_blanks(const char *s, size_t *i, size_t *cont_end);
static size_t read_operator(const char *s, TokenType *type);
static LexStatus read_word(const char *s, size_t *pos, char *out,
            size_t *cont_end, bool *quoted);

// главная функция
LexStatus lex(const char *s, TokenList *out)
{
    
    *out = (TokenList){0};

    char *word = xmalloc(strlen(s) + 1); // буфер для текста слова

    size_t i = 0;
    size_t cont_end = (size_t)-1; // "склеек ещё не было"
    size_t prev_end = 0; // где закончилась предыдущая лексема
    LexStatus st = LEX_OK;

    for (;;) {
        skip_blanks(s, &i, &cont_end);
        if (out->count > 0) // ничего не пропущено - значит, лексемы вплотную
            out->items[out->count - 1].glued = (i == prev_end);
        if (s[i] == '#') { // комментарий; '\n' не трогаем
            while (s[i] != '\0' && s[i] != '\n')
                i++;
        }

        TokenType type = TOK_WORD;
        char *text = NULL;
        bool quoted = false;

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
            i += read_operator(s + i, &type);
        } else {
            st = read_word(s, &i, word, &cont_end, &quoted);
            if (st != LEX_OK)
                break;
            text = xstrdup(word);
        }
        token_list_push(out, type, text);
        out->items[out->count - 1].quoted = quoted;
        prev_end = i;
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
    }
    return "unknown error";
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
static size_t read_operator(const char *s, TokenType *type)
{
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
// Состояния при чтении слова

typedef enum {
    WORD_PLAIN, // вне кавычек
    WORD_SINGLE, // внутри '...'
    WORD_DOUBLE // внутри "..."
} WordState;

// Читает слово с позиции *pos и записывает его текст (без кавычек и
// экранирования) в out. Буфер out должен быть не короче оставшегося
// ввода: слово не бывает длиннее исходного текста
// *quoted станет true, если в слове были кавычки или '\'
// В зависимости от состояния символы читаются по-разному:
// WORD_SINGLE : всё буквально до закрывающей кавычки;
// WORD_DOUBLE : буквально, кроме \" и \\ ; "\<перевод строки>" убирается;
// WORD_PLAIN  : '\' экранирует следующий символ; метасимвол кончает слово.

static LexStatus read_word(const char *s, size_t *pos, char *out,
            size_t *cont_end, bool *quoted)
{
    size_t i = *pos;
    WordState state = WORD_PLAIN;

    while (s[i] != '\0') {
        char c = s[i];

        if (state == WORD_SINGLE) {
            if (c == '\'')
                state = WORD_PLAIN;
            else
                *out++ = c;
        } else if (c == '\\' && s[i + 1] == '\n') { // склейка строк
            i++;
            
            *cont_end = i + 1;
        } else if (state == WORD_DOUBLE) {
            if (c == '"')
                state = WORD_PLAIN;
            else if (c == '\\' && (s[i + 1] == '"' || s[i + 1] == '\\'))
                *out++ = s[++i];
            else
                *out++ = c;
        } else if (is_meta(c)) {
            break;
        } else if (c == '\'') {
            state = WORD_SINGLE;
            *quoted = true;
        } else if (c == '"') {
            state = WORD_DOUBLE;
            *quoted = true;
        } else if (c == '\\' && s[i + 1] != '\0') {
            *out++ = s[++i];
            *quoted = true;
        } else {
            *out++ = c; // обычный символ (или '\' в самом конце ввода)
        }
        i++;
    }

    *out = '\0';
    *pos = i;
    
    return state == WORD_PLAIN ? LEX_OK : LEX_UNCLOSED_QUOTE;
}