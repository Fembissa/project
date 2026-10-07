#ifndef MY_LEXER_H
#define MY_LEXER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// виды лексем.
typedef enum {
    TOK_WORD, // слово
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
    LEX_UNCLOSED_SINGLE, // не закрыта ' 
    LEX_UNCLOSED_DOUBLE, // не закрыта " 
    LEX_CONTINUATION, // ввод кончился на '\' + перевод строки
    LEX_UNSUPPORTED_OP, // оператор вне базы: <<, >&, &>, ...
    LEX_UNSUPPORTED_FD_REDIR, // перенаправление с номером fd: 2>f
    LEX_NOMEM // не хватило памяти                
} LexStatus;

LexStatus lex(const char *input, TokenList *out);

// освобождает список лексем 
void token_list_free(TokenList *list);
bool lex_status_is_incomplete(LexStatus status);

const char *lex_status_message(LexStatus status);

//печатает лексемы по одной в строке 
void token_list_dump(const TokenList *list, FILE *out);

#endif 