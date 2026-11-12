#include "exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

// Базовая заглушка для выполнения узлов AST
int exec_node(const Node *node) {
    if (node == NULL)
        return 0;

    switch (node->type) {
    case NODE_SIMPLE:
        // TODO: Реализовать запуск простых команд (встроенные + fork/exec)
        fprintf(stderr, "mysh: execution of simple commands not implemented yet\n");
        return 1;

    case NODE_SUBSHELL:
        // TODO: Подоболочки (...)
        return 0;

    case NODE_PIPELINE:
        // TODO: Конвейеры (|)
        return 0;

    case NODE_ANDOR:
        // TODO: Логические цепочки (&&, ||)
        return 0;

    case NODE_LIST:
        // Последовательный запуск элементов списка (; / & / newline)
        {
            int last_status = 0;
            for (size_t i = 0; i < node->seq.count; i++) {
                last_status = exec_node(node->seq.items[i]);
                // TODO: Учитывать Op (OP_BG, OP_AND, OP_OR и т.д.)
            }
            return last_status;
        }
    }
    return 0;
}