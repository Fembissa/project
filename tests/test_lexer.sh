#   ok  'ввод' 'ожидаемые токены через пробел'
#   err 'ввод'  (ожидаем синтаксическую ошибку, код 2)
 
fail=0
 
ok() {
    got=$(./mysh --dump-tokens -c "$1" 2>&1 | tr '\n' ' ')
    got=${got% }
    [ "$got" = "$2" ] || { echo "FAIL: $1"; echo "  want: $2"; echo "  got:  $got"; fail=1; }
}
 
err() {
    ./mysh --dump-tokens -c "$1" >/dev/null 2>&1
    [ $? -eq 2 ] || { echo "FAIL (expected exit code 2): $1"; fail=1; }
}
 
# операторы и слова
ok 'echo hi'               'WORD "echo" WORD "hi" EOF'
ok 'a&&b||c>>d'            'WORD "a" AND WORD "b" OR WORD "c" DGREAT WORD "d" EOF'
ok '| & ; ( ) < >'         'PIPE AMP SEMI LPAREN RPAREN LESS GREAT EOF'
ok $'a\nb'                 'WORD "a" NEWLINE WORD "b" EOF'
ok '>out echo hi'          'GREAT WORD "out" WORD "echo" WORD "hi" EOF'
 
# кавычки и экранирование
ok "'a | b \\n'"           'WORD "a | b \\n" EOF'
ok '"a\tb \" \\ x"'        'WORD "a\\tb \" \\ x" EOF'
ok 'a"b c"d'               'WORD "ab cd" EOF'
ok "echo '' \"\""          'WORD "echo" WORD "" WORD "" EOF'
ok 'a\ b\|c'               'WORD "a b|c" EOF'
 
# комментарии
ok 'echo hi # c | x'       'WORD "echo" WORD "hi" EOF'
ok 'echo a#b'              'WORD "echo" WORD "a#b" EOF'
ok "echo \"#x\" '#y'"      'WORD "echo" WORD "#x" WORD "#y" EOF'
 
# продолжение строки
ok $'ec\\\nho'             'WORD "echo" EOF'
 
# число перед > - это обычное слово, если есть пробел или кавычки
ok 'echo 2 >f'             'WORD "echo" WORD "2" GREAT WORD "f" EOF'
ok "echo '2'>f"            'WORD "echo" WORD "2" GREAT WORD "f" EOF'
 
# ошибки
err "echo 'abc" # незакрытая '
err 'echo "abc' # незакрытая "
err $'echo a\\\n' # ввод оборвался на \ + перевод строки
err 'echo hi 2>f' # номер дескриптора
err 'cat <<EOF' # here-document
err 'echo hi >&2'
err 'echo hi &>f'
err 'a |& b'
err 'a ;; b'
 
[ $fail -eq 0 ] && echo "all lexer tests passed"
exit $fail
