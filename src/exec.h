#ifndef MY_EXEC_H
#define MY_EXEC_H

#include <stdbool.h>
#include "ast.h"

// Состояние интерпретатора (глобальных переменных нет - ТЗ). В main: "Shell sh = {0};"
typedef struct {
    int last_status; // код возврата последней выполненной команды
    bool exit_requested; // встроенная exit просит завершить интерпретатор
} Shell;

// Выполняет узел дерева (для корня - NODE_LIST), возвращает код последней команды.
// main после вызова: sh.last_status = exec_node(&sh, root);
// если sh.exit_requested - выйти с кодом sh.last_status.
int exec_node(Shell *sh, const Node *node);

#endif