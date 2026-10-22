#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "util.h"

#define EXIT_SYNTAX 2 // код возврата при синтаксической ошибке

// разбирает одну команду и печатает её лексемы; возвращает статус лексера
static LexStatus dump_command(const char *text)
{
    TokenList tokens;
    LexStatus st = lex(text, &tokens);
    if (st == LEX_OK) {
        token_list_dump(&tokens, stdout);
        token_list_free(&tokens);
    }
    return st;
}

int main(int argc, char **argv)
{
    bool dump_tokens = false;
    const char *command = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-tokens") == 0) {
            dump_tokens = true;
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            command = argv[++i];
        } else {
            fprintf(stderr, "mysh: %s: invalid option\n", argv[i]);
            return EXIT_SYNTAX;
        }
    }

    if (!dump_tokens) {
        fprintf(stderr, "mysh: only --dump-tokens is implemented so far\n");
        return EXIT_FAILURE;
    }

    LexStatus st = LEX_OK;
    if (command != NULL) {
        st = dump_command(command); // режим -c: одна команда
    } else {
        // Читаем stdin построчно. Если лексер сообщает, что ввод не закончен
        // (незакрытая кавычка, '\' в конце строки), дописываем следующую
        // строку к той же команде.
        char *line = NULL;
        size_t line_cap = 0;
        char *input = NULL; // накопленный текст команды
        size_t input_len = 0;

        while (getline(&line, &line_cap, stdin) != -1) {
            size_t n = strlen(line);
            input = xrealloc(input, input_len + n + 1);
            memcpy(input + input_len, line, n + 1);
            input_len += n;

            st = dump_command(input);
            if (lex_status_is_incomplete(st))
                continue; // команда не закончена: читаем следующую строку
            if (st != LEX_OK)
                break; // синтаксическая ошибка
            input_len = 0; // команда разобрана, начинаем новую
        }
        if (ferror(stdin)) { // внутренняя ошибка чтения
            perror("mysh: stdin");
            exit(EXIT_FAILURE);
        }
        free(line);
        free(input);
    }

    if (st != LEX_OK) { // включая ввод, оборвавшийся посреди команды
        fprintf(stderr, "mysh: %s\n", lex_status_message(st));
        return EXIT_SYNTAX;
    }

    return EXIT_SUCCESS;
}