// util.h - обёртки над malloc/realloc/strdup для ВНУТРЕННИХ ошибок
// Если памяти не хватило, это не ошибка пользователя, а сбой самой программы:
// обёртка печатает сообщение и завершает программу (exit), поэтому
// результат проверять не нужно - он никогда не NULL
#ifndef MY_UTIL_H
#define MY_UTIL_H

#include <stddef.h>

void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);

#endif