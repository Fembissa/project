#ifndef MY_EXEC_H
#define MY_EXEC_H

#include "ast.h"

// Главная функция выполнения дерева команд.
// Возвращает код возврата последней выполненной команды.
int exec_node(const Node *node);

#endif