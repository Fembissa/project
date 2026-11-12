#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "util.h"

#define EXIT_SYNTAX 2 // код возврата при синтаксической ошибке
#define ERROR_LEN 160 // место под текст ошибки

typedef enum { MODE_NONE, MODE_DUMP_TOKENS, MODE_DUMP_AST } Mode;

// итог обработки одной команды
typedef enum {
    CMD_OK,
    CMD_INCOMPLETE, // ввод не закончен (кавычка, '\' в конце строки)
    CMD_ERROR // синтаксическая ошибка
} CmdStatus;

// Обрабатывает одну команду: лексер -> (парсер) -> печать.
// При CMD_INCOMPLETE и CMD_ERROR в err лежит текст ошибки.
static CmdStatus process_command(const char *text, Mode mode, char *err)
{
    TokenList tokens;
    LexStatus ls = lex(text, &tokens);
    if (ls != LEX_OK) {
        snprintf(err, ERROR_LEN, "%s", lex_status_message(ls));
        return lex_status_is_incomplete(ls) ? CMD_INCOMPLETE : CMD_ERROR;
    }

    if (mode == MODE_DUMP_TOKENS) {
        token_list_dump(&tokens, stdout);
        token_list_free(&tokens);
        return CMD_OK;
    }
    Node *root = NULL;
    ParseError perr;
    bool ok = parse(&tokens, &root, &perr);
    token_list_free(&tokens);
    if (!ok) {
        snprintf(err, ERROR_LEN, "%s", perr.message);
        return perr.incomplete ? CMD_INCOMPLETE : CMD_ERROR;
    }

    ast_dump(root, stdout);
    node_free(root);
    return CMD_OK;
}

int main(int argc, char **argv)
{
    Mode mode = MODE_NONE;
    const char *command = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-tokens") == 0) {
            mode = MODE_DUMP_TOKENS;
        } else if (strcmp(argv[i], "--dump-ast") == 0) {
            mode = MODE_DUMP_AST;
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            command = argv[++i];
        } else {
            fprintf(stderr, "mysh: %s: invalid option\n", argv[i]);
            return EXIT_SYNTAX;
        }
    }

    if (mode == MODE_NONE) {
        fprintf(stderr, "mysh: only --dump-tokens and --dump-ast "
                        "are implemented so far\n");
        return EXIT_FAILURE;
    }

    char err[ERROR_LEN] = "";
    CmdStatus st = CMD_OK;
    if (command != NULL) {
        st = process_command(command, mode, err); // режим -c: одна команда
    } else {
        // Читаем stdin построчно. Если ввод не закончен (незакрытая кавычка,
        // '\' в конце строки), дописываем следующую строку к той же команде.
        char *line = NULL;
        size_t line_cap = 0;
        char *input = NULL; // накопленный текст команды
        size_t input_len = 0;

        while (getline(&line, &line_cap, stdin) != -1) {
            size_t n = strlen(line);
            input = xrealloc(input, input_len + n + 1);
            memcpy(input + input_len, line, n + 1);
            input_len += n;

            st = process_command(input, mode, err);
            if (st == CMD_INCOMPLETE)
                continue; // команда не закончена: читаем следующую строку
            if (st == CMD_ERROR)
                break;
            input_len = 0; // команда разобрана, начинаем новую
        }
        if (ferror(stdin)) { // внутренняя ошибка чтения
            perror("mysh: stdin");
            exit(EXIT_FAILURE);
        }
        free(line);
        free(input);
    }

    if (st != CMD_OK) { // включая ввод, оборвавшийся посреди команды
        fprintf(stderr, "mysh: %s\n", err);
        return EXIT_SYNTAX;
    }

    return EXIT_SUCCESS;
}