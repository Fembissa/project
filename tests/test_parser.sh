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

# списки: ; & и перевод строки (после & команда идёт в фон)
ast 'a; b'             'LIST|  ITEM fg|    SIMPLE "a"|  ITEM fg|    SIMPLE "b"'
ast 'a & b'            'LIST|  ITEM bg|    SIMPLE "a"|  ITEM fg|    SIMPLE "b"'
ast 'sleep 1 &'        'LIST|  ITEM bg|    SIMPLE "sleep" "1"'
ast 'a; b &'           'LIST|  ITEM fg|    SIMPLE "a"|  ITEM bg|    SIMPLE "b"'
ast 'a;'               'LIST|  ITEM fg|    SIMPLE "a"'
ast $'a\n\n b\n'       'LIST|  ITEM fg|    SIMPLE "a"|  ITEM fg|    SIMPLE "b"'
ast 'a && b &'         'LIST|  ITEM bg|    AND_OR|      SIMPLE "a"|      AND|      SIMPLE "b"'
ast 'a && b | c ; d &' 'LIST|  ITEM fg|    AND_OR|      SIMPLE "a"|      AND|      PIPELINE|        SIMPLE "b"|        SIMPLE "c"|  ITEM bg|    SIMPLE "d"'
ast $'\n'              'LIST'

# подоболочки
ast '(a; b)'           'LIST|  ITEM fg|    SUBSHELL|      LIST|        ITEM fg|          SIMPLE "a"|        ITEM fg|          SIMPLE "b"'
ast '(a) > out | b'    'LIST|  ITEM fg|    PIPELINE|      SUBSHELL|        LIST|          ITEM fg|            SIMPLE "a"|        REDIR > "out"|      SIMPLE "b"'
ast '((a))'            'LIST|  ITEM fg|    SUBSHELL|      LIST|        ITEM fg|          SUBSHELL|            LIST|              ITEM fg|                SIMPLE "a"'
ast '(a) && (b) &'     'LIST|  ITEM bg|    AND_OR|      SUBSHELL|        LIST|          ITEM fg|            SIMPLE "a"|      AND|      SUBSHELL|        LIST|          ITEM fg|            SIMPLE "b"'
ast $'(a\nb)'          'LIST|  ITEM fg|    SUBSHELL|      LIST|        ITEM fg|          SIMPLE "a"|        ITEM fg|          SIMPLE "b"'

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
bad '; a'
bad 'a ; ; b'
bad 'a & ; b'
bad '&'
bad '()'
bad '( ; a)'
bad '(a'
bad 'a)'
bad ')'
bad '(a) b'
bad 'a (b)'
bad '(a) >'

[ $fail -eq 0 ] && echo "all parser tests passed"
exit $fail