//lexer.h - лексический анализ: превращает строку в массив лексем.

//Лексер не выполняет системных вызовов и не знает грамматики. Он режет
// текст на слова и операторы, снимает кавычки и убирает комментарии
#ifndef MY_LEXER_H
#define MY_LEXER_H

#include <stdbool.h>
#include "token.h"

// результат работы лексера. Здесь только логические ошибки (ошибки
// ввода пользователя). Внутренние ошибки (нехватка памяти) сюда не
// попадают: они завершают программу через exit
typedef enum {
    LEX_OK,
    LEX_UNCLOSED_QUOTE, // не закрыта ' или "                   
    LEX_CONTINUATION // ввод кончился на '\' + перевод строки             
} LexStatus;

//Разбирает строку input. При LEX_OK в out лежит список, оканчивающийся
// TOK_EOF; его нужно освободить через token_list_free. При ошибке out пуст
LexStatus lex(const char *input, TokenList *out);
bool lex_status_is_incomplete(LexStatus status);
const char *lex_status_message(LexStatus status);

void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);
#endif 