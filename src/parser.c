// parser.c - парсер методом рекурсивного спуска.
//
// у каждого правила грамматики есть своя функция parse_<правило>, а правила вызывают друг друга
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
// Ошибки. Первая найденная ошибка запоминает сообщение и перематывает парсер
// на конец ввода (fail). Все циклы видят TOK_EOF и заканчиваются сами, поэтому
// результат каждой функции проверять не нужно: узлы сразу цепляются к
// родителю, и недостроенное дерево освобождается одним вызовом в parse().
#include "parser.h"

#include <stdio.h>
#include <string.h>
#include "util.h"

#define DIGITS "0123456789"
#define IDENT_CHARS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_" DIGITS
#define STARTS_WITH(s, prefix) (strncmp((s), (prefix), sizeof(prefix) - 1) == 0)

// Состояние разбора: где мы находимся в массиве лексем.
typedef struct {
    const Token *tokens; // последняя лексема - TOK_EOF
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
static void parse_redirection(Parser *p, RedirList *redirs);
static const Token *peek(const Parser *p);
static const Token *peek_next(const Parser *p);
static bool check(const Parser *p, TokenType type);
static bool match(Parser *p, TokenType type);
static void advance(Parser *p);
static bool is_redir_op(TokenType type);
static bool reject_fd_redirect(Parser *p);
static void fail(Parser *p, const char *message);
static void fail_unexpected(Parser *p);

// главная функция
bool parse(const TokenList *tokens, Node **root, ParseError *err){
    Parser p = {tokens->items, 0, err};
    err->message[0] = '\0';

    Node *list = parse_list(&p);
    if (!check(&p, TOK_EOF)) // после списка осталось лишнее: ")", ";;" ...
        fail_unexpected(&p);

    if (err->message[0] != '\0') { // где-то была ошибка
        node_free(list);
        return false;
    }
    *root = list;
    return true;
}

// правила грамматики

// list = and_or { (";" | "&" | перевод строки) [and_or] }
// После каждой команды запоминаем разделитель (в Op): "&" значит "в фон".
// Разделитель ровно один: "a ; ; b" - ошибка (второй ";" не начинает
// команду, цикл заканчивается, и parse() сообщает о лишней лексеме).
// Пустой список - не ошибка (пустая строка ввода).
static Node *parse_list(Parser *p){
    Node *list = node_new(NODE_LIST);

    for (;;) {
        while (match(p, TOK_NEWLINE)); // пустые строки пропускаем
        TokenType t = peek(p)->type;
        if (t != TOK_WORD && t != TOK_LPAREN && !is_redir_op(t))
            break; // с этой лексемы команда не начинается: список кончился

        Node *item = parse_and_or(p);
        Op op = OP_NONE;
        if (check(p, TOK_AMP))
            op = OP_BG;
        else if (check(p, TOK_SEMI) || check(p, TOK_NEWLINE))
            op = OP_SEMI;

        if (op == OP_BG && peek(p)->glued) { // "&>file" и "&>>file"
            TokenType next = peek_next(p)->type;
            if (next == TOK_GREAT || next == TOK_DGREAT)
                fail(p, "syntax error: `&>' is not supported");
        }
        seq_add(list, item, op);
        if (op == OP_NONE)
            break; // разделителя нет - это была последняя команда
        advance(p);
    }
    return list;
}

// and_or = pipeline { ("&&" | "||") pipeline }
// Дерево хранит просто последовательность "a, &&, b, ||, c" - исполнитель
// пройдёт её слева направо. Если операторов нет, возвращаем сам конвейер.
static Node *parse_and_or(Parser *p){
    Node *cur = parse_pipeline(p);
    if (!check(p, TOK_AND) && !check(p, TOK_OR))
        return cur;

    Node *andor = node_new(NODE_ANDOR);
    while (check(p, TOK_AND) || check(p, TOK_OR)) {
        Op op = check(p, TOK_AND) ? OP_AND : OP_OR;
        advance(p); // забрали && или ||
        seq_add(andor, cur, op);
        cur = parse_pipeline(p);
    }
    seq_add(andor, cur, OP_NONE);
    return andor;
}
// pipeline = command { "|" command }
// Если "|" нет, возвращаем саму команду без обёртки NODE_PIPELINE.
static Node *parse_pipeline(Parser *p){
    Node *cur = parse_command(p);
    if (!check(p, TOK_PIPE))
        return cur;

    Node *pipeline = node_new(NODE_PIPELINE);
    while (match(p, TOK_PIPE)) { // забрали "|" - после него нужна команда
        seq_add(pipeline, cur, OP_PIPE);
        cur = parse_command(p);
    }
    seq_add(pipeline, cur, OP_NONE);
    return pipeline;
}

// command = simple | subshell
// Что перед нами, видно по первой лексеме: "(" начинает подоболочку.
static Node *parse_command(Parser *p){
   return check(p, TOK_LPAREN) ? parse_subshell(p) : parse_simple(p);
}

// subshell = "(" list ")" { redirection }
// Внутри скобок снова список - поэтому подоболочки вкладываются друг в друга.
static Node *parse_subshell(Parser *p){
    advance(p); // "("

    Node *sub = node_new(NODE_SUBSHELL);
    sub->subshell.body = parse_list(p);
    if (sub->subshell.body->seq.count == 0 || !match(p, TOK_RPAREN)){
        fail_unexpected(p); // "()" или нет закрывающей скобки
    }

    while (is_redir_op(peek(p)->type)){ // перенаправления на весь список
        parse_redirection(p, &sub->subshell.redirs);
    return sub;
    }
}

// simple = { redirection } word { word | redirection }
// Слова и перенаправления могут идти вперемешку: "> out echo hi" - то же,
// что "echo hi > out". Поэтому просто собираем всё подряд.
static Node *parse_simple(Parser *p){
    Node *cmd = node_new(NODE_SIMPLE);

    for (;;) {
        if (check(p, TOK_WORD)) {
            if (!reject_fd_redirect(p)) { // "2>f" - вне базы
                simple_add_word(cmd, xstrdup(peek(p)->text));
                advance(p);
            }
        } else if (is_redir_op(peek(p)->type)) {
            parse_redirection(p, &cmd->simple.redirs);
        } else {
            break;
        }
    }

    if (cmd->simple.argc == 0) { // команда обязана содержать слово
        if (cmd->simple.redirs.count > 0){
            fail(p, "syntax error: redirection without a command");
        }else {
            fail_unexpected(p);
        }
    }
    return cmd;
}

// redirection = ("<" | ">" | ">>") word
static void parse_redirection(Parser *p, RedirList *redirs){
    TokenType op = peek(p)->type;
    advance(p); // знак перенаправления

    if (!check(p, TOK_WORD)) { // после знака обязано быть имя файла
        fail_unexpected(p);
        return;
    }
    if (reject_fd_redirect(p)) // ">2>f": после знака стоит номер дескриптора
        return;

    const char *file = peek(p)->text;
    if (STARTS_WITH(file, "/dev/tcp/") || STARTS_WITH(file, "/dev/udp/")) {
        fail(p, "syntax error: redirection to /dev/tcp or /dev/udp "
                "is not supported");
        return;
    }

    RedirType type = REDIR_APPEND;
    if (op == TOK_LESS){
        type = REDIR_IN;
    }else if (op == TOK_GREAT){
        type = REDIR_OUT;

    redirs_add(redirs, type, xstrdup(file));
    advance(p); // имя файла
    }
}

//работа с лексемами

// текущая лексема (на неё смотрим, но не забираем)
static const Token *peek(const Parser *p){
    return &p->tokens[p->pos];
}

// следующая за текущей (вызывать, только если текущая - не TOK_EOF)
static const Token *peek_next(const Parser *p){
    return &p->tokens[p->pos + 1];
}

// текущая лексема имеет вид type?
static bool check(const Parser *p, TokenType type){
    return peek(p)->type == type;
}

// переходит к следующей лексеме (за TOK_EOF не уходим)
static void advance(Parser *p){
    if (!check(p, TOK_EOF))
        p->pos++;
}

// если текущая лексема имеет вид type - забираем её и возвращаем true
static bool match(Parser *p, TokenType type){
    if (!check(p, type))
        return false;
    advance(p);
    return true;
}

static bool is_redir_op(TokenType type){
    return type == TOK_LESS || type == TOK_GREAT || type == TOK_DGREAT;
}

// Номер дескриптора в "2>file" - слово из цифр (или "{имя}" в "{fd}>file"),
// написанное без кавычек и вплотную к < или >. С пробелом ("2 >file") или
// в кавычках ("'2'>file") это обычный аргумент. В базе таких перенаправлений
// нет: если текущее слово - номер дескриптора, сообщаем об ошибке (true).
static bool reject_fd_redirect(Parser *p){
    const Token *t = peek(p);
    if (t->quoted || !t->glued || !is_redir_op(peek_next(p)->type))
        return false;
    size_t n = strlen(t->text); // слово без кавычек не бывает пустым
    bool is_fd = strspn(t->text, DIGITS) == n; // "2"
    if (!is_fd && n > 2 && t->text[0] == '{' && t->text[n - 1] == '}')
        is_fd = strchr(DIGITS, t->text[1]) == NULL &&
                strspn(t->text + 1, IDENT_CHARS) == n - 2; // "{имя}"

    if (is_fd)
        fail(p, "syntax error: redirection with file descriptor "
                "is not supported");
    return is_fd;
}
//ошибки 

// Запоминает ошибку и перематывает парсер на конец ввода. Сообщение пишется
// только для первой ошибки: дальнейшие "ошибки" - следствие первой.
static void fail(Parser *p, const char *message){
    if (p->err->message[0] != '\0')
        return;
    snprintf(p->err->message, sizeof p->err->message, "%s", message);
    while (!check(p, TOK_EOF))
        advance(p);
}

// "неожиданная лексема": сообщаем, что именно встретилось
static void fail_unexpected(Parser *p){
    const Token *t = peek(p);
    char message[PARSE_ERROR_LEN];

    if (t->type == TOK_EOF)
        snprintf(message, sizeof message, "syntax error: unexpected end of input");
    else
        snprintf(message, sizeof message,
                 "syntax error near unexpected token `%s'", token_text(t));
    fail(p, message);
}