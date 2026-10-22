// token.h - лексемы (токены): типы, список лексем и его печать.
#ifndef MY_TOKEN_H
#define MY_TOKEN_H

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

// добавляет лексему в конец списка. Владение text переходит к списку
void token_list_push(TokenList *list, TokenType type, char *text);

// освобождает список лексем 
void token_list_free(TokenList *list);

//печатает лексемы по одной в строке 
void token_list_dump(const TokenList *list, FILE *out);

#endif