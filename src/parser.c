// parser.c - парсер методом рекурсивного спуска.
//
// у каждого правила
// грамматики есть своя функция parse_<правило>, а правила вызывают друг друга
// сверху вниз - от самого "слабого" оператора к самому "сильному":
//
//   list        = and_or { (";" | "&" | перевод строки) [and_or] }
//   and_or      = pipeline { ("&&" | "||") pipeline }
//   pipeline    = command { "|" command }
//   command     = simple | subshell
//   subshell    = "(" list ")" { redirection }
//   simple      = { redirection } word { word | redirection }
//   redirection = ("<" | ">" | ">>") word
//
// Повторения {...} записаны циклами. Поэтому "a && b || c" разбирается как
// "(a && b) || c" (левая ассоциативность, как требует ТЗ).
//
// Каждая функция parse_* возвращает узел дерева или NULL при ошибке; текст
// ошибки пишется в p->err при первом обнаружении, дальше NULL просто
// передаётся вверх.
#include "parser.h"
#include <stdio.h>
#include "util.h"

// Состояние разбора: где мы находимся в массиве лексем.
typedef struct {
    const Token *tokens; // последняя лексема - TOK_EOF
    size_t count;
    size_t pos; // индекс текущей лексемы
    ParseError *err;
} Parser;

// функции разбора и вспомогательные функции определены ниже
static Node *parse_simple(Parser *p);
static bool parse_redirection(Parser *p, Redir **redirs, size_t *count);
static bool is_redir_op(TokenType type);
static const Token *peek(const Parser *p);
static bool check(const Parser *p, TokenType type);
static bool at_end(const Parser *p);
static void advance(Parser *p);
static void skip_newlines(Parser *p);
static void error_unexpected(Parser *p);


// главная функция
bool parse(const TokenList *tokens, Node **root, ParseError *err)
{
    Parser p = {tokens->items, tokens->count, 0, err};
    err->message[0] = '\0';

    // ВРЕМЕННО
    Node *list = node_new(NODE_LIST);
    skip_newlines(&p);
    if (!at_end(&p)) {
        Node *cmd = parse_simple(&p);
        if (cmd == NULL) {
            node_free(list);
            return false;
        }
        list_add(list, cmd, false);
        skip_newlines(&p);
    }

    if (!at_end(&p)) { // после команды осталось что-то лишнее
        error_unexpected(&p);
        node_free(list);
        return false;
    }
    *root = list;
    return true;
}

// правила грамматики

// simple = { redirection } word { word | redirection }
// Слова и перенаправления могут идти вперемешку: "> out echo hi" - то же,
// что "echo hi > out". Поэтому просто собираем всё подряд, пока попадаются
// слова и знаки перенаправления.
static Node *parse_simple(Parser *p)
{
    Node *cmd = node_new(NODE_SIMPLE);

    for (;;) {
        if (check(p, TOK_WORD)) {
            simple_add_word(cmd, xstrdup(peek(p)->text));
            advance(p);
        } else if (is_redir_op(peek(p)->type)) {
            if (!parse_redirection(p, &cmd->simple.redirs,
                                   &cmd->simple.redir_count)) {
                node_free(cmd);
                return NULL;
            }
        } else {
            break;
        }
    }

    if (cmd->simple.argc == 0) { // ТЗ: команда обязана содержать слово
        if (cmd->simple.redir_count > 0)
            snprintf(p->err->message, sizeof p->err->message,
                     "syntax error: redirection without a command");
        else
            error_unexpected(p);
        node_free(cmd);
        
        return NULL;
    }
    return cmd;
}

// redirection = ("<" | ">" | ">>") word
// Добавляет перенаправление в массив redirs; false - ошибка.
static bool parse_redirection(Parser *p, Redir **redirs, size_t *count)
{
    TokenType op = peek(p)->type;
    advance(p); // знак перенаправления

    if (!check(p, TOK_WORD)) { // после знака обязано быть имя файла
        error_unexpected(p);
        return false;
    }

    RedirType type = REDIR_APPEND;
    if (op == TOK_LESS)
        type = REDIR_IN;
    else if (op == TOK_GREAT)
        type = REDIR_OUT;

    redirs_add(redirs, count, type, xstrdup(peek(p)->text));
    advance(p); // имя файла
    return true;
}

//работа с лексемами

// текущая лексема (на неё смотрим, но не забираем)
static const Token *peek(const Parser *p)
{
    return &p->tokens[p->pos];
}

// текущая лексема имеет вид type?
static bool check(const Parser *p, TokenType type)
{
    return peek(p)->type == type;
}

static bool at_end(const Parser *p)
{
    return check(p, TOK_EOF);
}

// переходит к следующей лексеме (за TOK_EOF не уходим)
static void advance(Parser *p)
{
    if (!at_end(p))
        p->pos++;
}

// перевод строки между командами - то же, что пустые команды
static void skip_newlines(Parser *p)
{
    while (check(p, TOK_NEWLINE))
        advance(p);
}

static bool is_redir_op(TokenType type)
{
    return type == TOK_LESS || type == TOK_GREAT || type == TOK_DGREAT;
}

//ошибки 

// "неожиданная лексема": печатаем, что именно встретилось
static void error_unexpected(Parser *p)
{
    const Token *t = peek(p);
    if (t->type == TOK_EOF)
        snprintf(p->err->message, sizeof p->err->message,
                 "syntax error: unexpected end of input");
    else
        snprintf(p->err->message, sizeof p->err->message,
                 "syntax error near unexpected token `%s'", token_text(t));
}