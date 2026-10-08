//lexer.h - лексический анализ: превращает строку в массив лексем.

//Лексер не выполняет системных вызовов и не знает грамматики. Он режет
// текст на слова и операторы, снимает кавычки и убирает комментарии.
#ifndef MY_LEXER_H
#define MY_LEXER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// виды лексем.
typedef enum {
    TOK_WORD, // слово (команда, аргумент, имя файла)
    TOK_PIPE, // | 
    TOK_AMP, // & 
    TOK_SEMI, // ;  
    TOK_AND, // && 
    TOK_OR, // || 
    TOK_LPAREN, // ( 
    TOK_RPAREN, // ) 
    TOK_LESS, // < 
    TOK_GREAT, // > 
    TOK_DGREAT, // >>
    TOK_NEWLINE, // перевод строки
    TOK_EOF // конец ввода
} TokenType;

//text заполнен только у TOK_WORD (кавычки уже сняты), иначе NULL
typedef struct {
    TokenType type;
    char *text;
} Token;

//динамический массив лексем
typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

// результат работы лексера
typedef enum {
    LEX_OK,
    LEX_UNCLOSED_QUOTE, // не закрыта ' или "                   
    LEX_CONTINUATION, // ввод кончился на '\' + перевод строки 
    LEX_UNSUPPORTED, // вне базы: <<, >&, &>, 2>f и т. п. 
    LEX_NOMEM // не хватило памяти              
} LexStatus;

//Разбирает строку input. При LEX_OK в out лежит список, оканчивающийся
// TOK_EOF; его нужно освободить через token_list_free. При ошибке out пуст
LexStatus lex(const char *input, TokenList *out);

// освобождает список лексем 
void token_list_free(TokenList *list);
bool lex_status_is_incomplete(LexStatus status);

const char *lex_status_message(LexStatus status);

//печатает лексемы по одной в строке 
void token_list_dump(const TokenList *list, FILE *out);

#endif 