#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"

#define EXIT_SYNTAX 2 // код возврата при синтаксической ошибке
#define READ_CHUNK 4096

// читает весь стандартный ввод в одну строку 
static char *read_all_stdin(void)
{
    size_t len = 0, cap = READ_CHUNK;
    char *buf = malloc(cap);
    if (buf == NULL)
        return NULL;

    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, stdin)) > 0) {
        len += n;
        if (cap - len < 2) {
            char *p = realloc(buf, cap * 2);
            if (p == NULL) {
                free(buf);
                return NULL;
            }
            buf = p;
            cap *= 2;
        }
    }
    if (ferror(stdin)) {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    return buf;
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

    char *input = NULL;
    if (command == NULL) {
        input = read_all_stdin();
        if (input == NULL) {
            perror("mysh: stdin");
            return EXIT_FAILURE;
        }
        command = input;
    }

    TokenList tokens;
    LexStatus st = lex(command, &tokens);
    free(input);
    if (st != LEX_OK) {
        fprintf(stderr, "mysh: %s\n", lex_status_message(st));
        return EXIT_SYNTAX;
    }

    token_list_dump(&tokens, stdout);
    token_list_free(&tokens);
    return EXIT_SUCCESS;
}