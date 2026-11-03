#!/bin/bash
# Тесты парсера. Запуск из корня репозитория:  make && bash tests/test_parser.sh
#
# ast 'ввод' 'дерево'   дерево - строки --dump-ast, склеенные через |
# bad 'ввод' ожидаем синтаксическую ошибку: код 2, stdout пуст

fail=0

ast() {
    got=$(./mysh --dump-ast -c "$1" 2>&1 | tr '\n' '|')
    got=${got%|}
    [ "$got" = "$2" ] || { echo "FAIL: $1"; echo "  want: $2"; echo "  got:  $got"; fail=1; }
}

bad() {
    out=$(./mysh --dump-ast -c "$1" 2>/dev/null)
    code=$?
    { [ $code -eq 2 ] && [ -z "$out" ]; } || { echo "FAIL (expected syntax error): $1"; fail=1; }
}

# простые команды
ast ''                  'LIST'
ast 'ls'                'LIST|  ITEM fg|    SIMPLE "ls"'
ast 'ls -l /tmp'        'LIST|  ITEM fg|    SIMPLE "ls" "-l" "/tmp"'
ast 'echo "a b" '"'c'"  'LIST|  ITEM fg|    SIMPLE "echo" "a b" "c"'

# перенаправления (слова и перенаправления можно перемешивать)
ast 'echo hi > out'     'LIST|  ITEM fg|    SIMPLE "echo" "hi"|      REDIR > "out"'
ast '> out echo hi'     'LIST|  ITEM fg|    SIMPLE "echo" "hi"|      REDIR > "out"'
ast 'cat < in >> log'   'LIST|  ITEM fg|    SIMPLE "cat"|      REDIR < "in"|      REDIR >> "log"'
ast 'wc <in x'          'LIST|  ITEM fg|    SIMPLE "wc" "x"|      REDIR < "in"'

# конвейеры
ast 'ls | wc -l'       'LIST|  ITEM fg|    PIPELINE|      SIMPLE "ls"|      SIMPLE "wc" "-l"'
ast 'a | b | c'        'LIST|  ITEM fg|    PIPELINE|      SIMPLE "a"|      SIMPLE "b"|      SIMPLE "c"'
ast 'cat <in | sort >out' 'LIST|  ITEM fg|    PIPELINE|      SIMPLE "cat"|        REDIR < "in"|      SIMPLE "sort"|        REDIR > "out"'

# && и || (операторы идут слева направо, | связывает сильнее)
ast 'a && b'           'LIST|  ITEM fg|    AND_OR|      SIMPLE "a"|      AND|      SIMPLE "b"'
ast 'a && b || c'      'LIST|  ITEM fg|    AND_OR|      SIMPLE "a"|      AND|      SIMPLE "b"|      OR|      SIMPLE "c"'
ast 'a || b | c && d'  'LIST|  ITEM fg|    AND_OR|      SIMPLE "a"|      OR|      PIPELINE|        SIMPLE "b"|        SIMPLE "c"|      AND|      SIMPLE "d"'

# ошибки
bad '> out' # команды нет, только перенаправление
bad 'echo >' # нет имени файла
bad 'echo > > f'
bad 'echo < < f'
bad '| a'
bad 'a |'
bad 'a | | b'
bad 'a &&'
bad '&& a'
bad 'a && || b'

[ $fail -eq 0 ] && echo "all parser tests passed"
exit $fail