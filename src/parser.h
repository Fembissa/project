// parser.h - синтаксический анализ (вторая фаза): лексемы -> дерево команд.
//
// Парсер не выполняет системных вызовов и не знает, как команды запускаются.
// Он только проверяет, что лексемы складываются в правильную конструкцию,
// и строит дерево (см. ast.h).
#ifndef MY_PARSER_H
#define MY_PARSER_H
#include <stdbool.h>

#include "ast.h"
#include "token.h"

#define PARSE_ERROR_LEN 128

// текст диагностики (без префикса "mysh: ")
typedef struct {
    char message[PARSE_ERROR_LEN];
} ParseError;

// Строит дерево по списку лексем (он оканчивается TOK_EOF).
// Успех: возвращает true, *root - корень (узел NODE_LIST); его нужно
//   освободить через node_free. Пустой ввод даёт пустой список.
// Ошибка: возвращает false, текст ошибки в err->message, дерево не создано.
// Тексты слов парсер копирует, поэтому список лексем можно освобождать сразу.
bool parse(const TokenList *tokens, Node **root, ParseError *err);

#endif