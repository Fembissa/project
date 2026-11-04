// parser.c - парсер методом рекурсивного спуска.
//
// у каждого правила
// грамматики есть своя функция parse_<правило>, а правила вызывают друг друга
// сверху вниз - от самого "слабого" оператора к самому "сильному":
//
// list = and_or { (";" | "&" | перевод строки) [and_or] }
// and_or = pipeline { ("&&" | "||") pipeline }
// pipeline = command { "|" command }
// command = simple | subshell
// subshell = "(" list ")" { redirection }
// simple = { redirection } word { word | redirection }
// redirection = ("<" | ">" | ">>") word
//
// Повторения {...} записаны циклами. Поэтому "a && b || c" разбирается как
// "(a && b) || c" (левая ассоциативность)
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
static Node *parse_list(Parser *p);
static Node *parse_and_or(Parser *p);
static Node *parse_pipeline(Parser *p);
static Node *parse_command(Parser *p);
static Node *parse_subshell(Parser *p);
static Node *parse_simple(Parser *p);
static bool parse_redirection(Parser *p, Redir **redirs, size_t *count);
static bool is_redir_op(TokenType type);
static bool starts_command(TokenType type);
static const Token *peek(const Parser *p);
static bool check(const Parser *p, TokenType type);
static bool match(Parser *p, TokenType type);
static bool expect(Parser *p, TokenType type);
static bool at_end(const Parser *p);
static void advance(Parser *p);
static void skip_newlines(Parser *p);
static void error_unexpected(Parser *p);


// главная функция
bool parse(const TokenList *tokens, Node **root, ParseError *err){
    Parser p = {tokens->items, tokens->count, 0, err};
    err->message[0] = '\0';

    Node *list = parse_list(&p);
    if (list == NULL)
        return false;

    if (!at_end(&p)) { // после списка осталось что-то лишнее: ")", ";;" ...
        error_unexpected(&p);
        node_free(list);
        return false;
    }
    *root = list;
    return true;
}

// правила грамматики

// list = and_or { (";" | "&" | перевод строки) [and_or] }
// Каждая команда списка запоминает, стоял ли после неё "&" (запуск в фоне).
// Разделитель ровно один: "a ; ; b" - ошибка (второй ";" не начинает
// команду, цикл здесь заканчивается и parse() сообщает о лишней лексеме).
// Пустые строки пропускаем. Пустой список - не ошибка (пустая строка ввода).
static Node *parse_list(Parser *p){
    Node *list = node_new(NODE_LIST);

    for (;;) {
        skip_newlines(p);
        if (!starts_command(peek(p)->type))
            break; // список кончился (конец ввода, ")" ...) - решит вызывающий

        Node *item = parse_and_or(p);
        if (item == NULL) {
            node_free(list);
            return NULL;
        }

        TokenType sep = peek(p)->type;
        list_add(list, item, sep == TOK_AMP);
        if (sep != TOK_SEMI && sep != TOK_AMP && sep != TOK_NEWLINE)
            break; // разделителя нет - это была последняя команда
        advance(p);
    }
    return list;
}

// and_or = pipeline { ("&&" | "||") pipeline }
//
// Цикл вместо рекурсии даёт левую ассоциативность: в a && b || c операторы
// выполняются слева направо, как "(a && b) || c". Дерево хранит просто
// последовательность "a, &&, b, ||, c" - исполнитель пройдёт её по порядку.
static Node *parse_and_or(Parser *p){
    Node *first = parse_pipeline(p);
    if (first == NULL)
        return NULL;
    if (!check(p, TOK_AND) && !check(p, TOK_OR))
        return first;

    Node *andor = node_new(NODE_ANDOR);
    andor_add(andor, ANDOR_AND, first); // ops[0] не используется
    while (check(p, TOK_AND) || check(p, TOK_OR)) {
        AndOrOp op = check(p, TOK_AND) ? ANDOR_AND : ANDOR_OR;
        advance(p); // забрали && или ||
        Node *next = parse_pipeline(p);
        if (next == NULL) {
            node_free(andor);
            return NULL;
        }
        andor_add(andor, op, next);
    }
    return andor;
}
// pipeline = command { "|" command }
// Если "|" нет, возвращаем саму команду без обёртки NODE_PIPELINE.
static Node *parse_pipeline(Parser *p){
    Node *first = parse_command(p);
    if (first == NULL)
        return NULL;
    if (!check(p, TOK_PIPE))
        return first;

    Node *pipeline = node_new(NODE_PIPELINE);
    pipeline_add(pipeline, first);
    while (match(p, TOK_PIPE)) { // забрали "|" - после него нужна команда
        Node *cmd = parse_simple(p);
        if (cmd == NULL) {
            node_free(pipeline);
            return NULL;
        }
        pipeline_add(pipeline, cmd);
    }
    return pipeline;
}

// command = simple | subshell
// Что перед нами, видно по первой лексеме: "(" начинает подоболочку.
static Node *parse_command(Parser *p){
    if (check(p, TOK_LPAREN))
        return parse_subshell(p);
    return parse_simple(p);
}

// subshell = "(" list ")" { redirection }
// Внутри скобок снова список - поэтому подоболочки вкладываются друг в друга
// (рекурсия: parse_list -> ... -> parse_command -> parse_subshell -> parse_list).
static Node *parse_subshell(Parser *p){
    advance(p); // "("

    Node *body = parse_list(p);
    if (body == NULL)
        return NULL;
    if (body->list.count == 0) { // "()" - пустая подоболочка
        error_unexpected(p);
        node_free(body);
        return NULL;
    }
    if (!expect(p, TOK_RPAREN)) { // не нашли закрывающую скобку
        node_free(body);
        return NULL;
    }

    Node *sub = node_new(NODE_SUBSHELL);
    sub->subshell.body = body;
    while (is_redir_op(peek(p)->type)) { // перенаправления на весь список
        if (!parse_redirection(p, &sub->subshell.redirs,
                               &sub->subshell.redir_count)) {
            node_free(sub);
            return NULL;
        }
    }
    return sub;
}

// simple = { redirection } word { word | redirection }
// Слова и перенаправления могут идти вперемешку: "> out echo hi" - то же,
// что "echo hi > out". Поэтому просто собираем всё подряд, пока попадаются
// слова и знаки перенаправления.
static Node *parse_simple(Parser *p){
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

    if (cmd->simple.argc == 0) { // команда обязана содержать слово
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
static bool parse_redirection(Parser *p, Redir **redirs, size_t *count){
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
static const Token *peek(const Parser *p){
    return &p->tokens[p->pos];
}

// текущая лексема имеет вид type?
static bool check(const Parser *p, TokenType type){
    return peek(p)->type == type;
}

// если текущая лексема имеет вид type - забираем её и возвращаем true
static bool match(Parser *p, TokenType type){
    if (check(p, type)) {
        advance(p);
        return true;
    }
    return false;
}

// забираем лексему type; если её нет - ошибка
static bool expect(Parser *p, TokenType type)
{
    if (match(p, type))
        return true;
    error_unexpected(p);
    return false;
}

static bool at_end(const Parser *p){
    return check(p, TOK_EOF);
}

// переходит к следующей лексеме (за TOK_EOF не уходим)
static void advance(Parser *p){
    if (!at_end(p))
        p->pos++;
}

// перевод строки между командами - то же, что пустые команды
static void skip_newlines(Parser *p){
    while (check(p, TOK_NEWLINE))
        advance(p);
}

static bool is_redir_op(TokenType type){
    return type == TOK_LESS || type == TOK_GREAT || type == TOK_DGREAT;
}

// с этой лексемы может начинаться команда
static bool starts_command(TokenType type){
    return type == TOK_WORD || type == TOK_LPAREN || is_redir_op(type);
}

//ошибки 

// "неожиданная лексема": печатаем, что именно встретилось
static void error_unexpected(Parser *p){
    const Token *t = peek(p);
    if (t->type == TOK_EOF)
        snprintf(p->err->message, sizeof p->err->message,
                 "syntax error: unexpected end of input");
    else
        snprintf(p->err->message, sizeof p->err->message,
                 "syntax error near unexpected token `%s'", token_text(t));
}