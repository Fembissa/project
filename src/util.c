// util.c - обёртки над функциями выделения памяти
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Если p == NULL - сообщаем об ошибке и выходим, иначе возвращаем p
static void *or_die(void *p)
{
    if (p == NULL) {
        fputs("mysh: out of memory\n", stderr);
        exit(EXIT_FAILURE);
    }
    return p;
}

void *xmalloc(size_t size)
{
    return or_die(malloc(size));
}

void *xrealloc(void *ptr, size_t size)
{
    return or_die(realloc(ptr, size));
}

char *xstrdup(const char *s)
{
    return or_die(strdup(s));
}