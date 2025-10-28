#!/usr/bin/env bash
set -euo pipefail

BOLD="$(printf '\033[1m')"
DIM="$(printf '\033[2m')"
RED="$(printf '\033[31m')"
GRN="$(printf '\033[32m')"
YEL="$(printf '\033[33m')"
RST="$(printf '\033[0m')"

pass() { echo -e "${GRN}✔ PASS${RST} $*"; }
fail() { echo -e "${RED}✘ FAIL${RST} $*"; exit 1; }

# 1) Compilar
echo -e "${BOLD}Compilando solucion.c...${RST}"
if command -v clang >/dev/null 2>&1; then
  CC=clang
else
  CC=gcc
fi

$CC -std=c99 -Wall -Wextra -O2 -o wish solucion.c

[ -x ./wish ] || fail "No se generó ejecutable ./wish"
pass "Compilación"

# Espacio temporal para pruebas
TMPDIR="$(mktemp -d)"
cleanup() {
  rm -rf "$TMPDIR"
}
trap cleanup EXIT

# 2) Test: modo interactivo imprime prompt y acepta exit
echo -e "${BOLD}Prueba: modo interactivo (prompt y exit)${RST}"
OUT="$(printf "exit\n" | ./wish 2>&1)"
PROMPTS="$(printf "%s" "$OUT" | grep -c "wish> " || true)"
[ "$PROMPTS" -eq 1 ] || fail "Se esperaba 1 prompt en interactivo; got $PROMPTS"
pass "Interactivo OK"

# 3) Test: ejecución externa básica en batch (sin prompt)
echo -e "${BOLD}Prueba: batch básico (echo, pwd, cd)${RST}"
BATCH1="$TMPDIR/batch1.txt"
DIRA="$TMPDIR/dirA"
mkdir -p "$DIRA"
cat > "$BATCH1" <<'EOF'
echo hello
pwd
cd /
/bin/pwd
EOF

OUT="$(./wish "$BATCH1" 2>&1)"
echo "$OUT" | grep -q "^hello$" || fail "echo no imprimió 'hello'"
echo "$OUT" | grep -Eq "^/.*$"   || fail "pwd no imprimió un path absoluto"
pass "Batch básico OK"

# 4) Test: path builtin (vaciar PATH rompe ejecución; luego restaurar)
echo -e "${BOLD}Prueba: path builtin (vaciar y restaurar)${RST}"
BATCH2="$TMPDIR/batch2.txt"
cat > "$BATCH2" <<'EOF'
path
ls
path /bin
echo ok
EOF

# Capturar stderr también para ver el mensaje de error de 'ls'
OUT="$(./wish "$BATCH2" 2>&1)"
ERRS="$(printf "%s" "$OUT" | grep -c "An error has occurred" || true)"
[ "$ERRS" -ge 1 ] || fail "Se esperaba al menos 1 error al ejecutar 'ls' sin PATH"
echo "$OUT" | grep -q "^ok$" || fail "Después de 'path /bin', 'echo ok' no se ejecutó"
pass "path builtin OK"

# 5) Test: redirección (> stdout y stderr al mismo archivo)
echo -e "${BOLD}Prueba: redirección de stdout y stderr${RST}"
OUT_STD="$TMPDIR/out_std.txt"
OUT_ERR="$TMPDIR/out_err.txt"
BATCH3="$TMPDIR/batch3.txt"
cat > "$BATCH3" <<EOF
echo hola > $OUT_STD
ls /definitivamente_no_existe > $OUT_ERR
EOF

./wish "$BATCH3" 2>/dev/null

# stdout: debe contener "hola"
[ -s "$OUT_STD" ] || fail "STDOUT redirigido vacío; se esperaba contenido"
grep -q "^hola$" "$OUT_STD" || fail "No se encontró 'hola' en $OUT_STD"

# stderr (de ls fallido) también va al archivo (nuestro shell redirige stdout+stderr)
[ -s "$OUT_ERR" ] || fail "STDERR redirigido vacío; se esperaba mensaje de error"
pass "Redirección OK"

# 6) Test: paralelismo con '&' (debería tardar ~1s, no ~2s)
echo -e "${BOLD}Prueba: comandos en paralelo (&)${RST}"
BATCH4="$TMPDIR/batch4.txt"
cat > "$BATCH4" <<'EOF'
sleep 1 & sleep 1 & echo done
EOF

START="$(date +%s)"
OUT="$(./wish "$BATCH4" 2>&1)"
END="$(date +%s)"
DUR=$(( END - START ))
echo "$OUT" | grep -q "^done$" || fail "No se imprimió 'done'"
# margen: esperamos < 2s; debería ser ~1s
[ "$DUR" -lt 2 ] || fail "Paralelismo no funcionó: duración $DUR s (esperado <2)"
pass "Paralelismo OK (${DUR}s)"

# 7) Test: ampersand final no debe causar error
echo -e "${BOLD}Prueba: '&' al final sin error${RST}"
BATCH5="$TMPDIR/batch5.txt"
cat > "$BATCH5" <<'EOF'
path /bin /usr/bin
true &
EOF
OUT="$(./wish "$BATCH5" 2>&1)"
ERRS="$(printf "%s" "$OUT" | grep -c "An error has occurred" || true)"
[ "$ERRS" -eq 0 ] || fail "No se esperaba error con '&' final"
pass "Ampersand final OK"

# 8) Test: errores de sintaxis redirección (múltiples '>' o archivo faltante)
echo -e "${BOLD}Prueba: errores redirección${RST}"
BATCH6="$TMPDIR/batch6.txt"
cat > "$BATCH6" <<'EOF'
ls >                     # falta archivo
ls > a > b               # múltiples '>'
EOF

OUT="$(./wish "$BATCH6" 2>&1 || true)"
ERRS="$(printf "%s" "$OUT" | grep -c "An error has occurred" || true)"
[ "$ERRS" -ge 2 ] || fail "Se esperaban >=2 errores por redirección inválida"
pass "Errores de redirección OK"

# 9) Test: built-ins con argumentos inválidos
echo -e "${BOLD}Prueba: errores en built-ins (exit con arg, cd sin arg)${RST}"
BATCH7="$TMPDIR/batch7.txt"
cat > "$BATCH7" <<'EOF'
exit 1
cd
echo sigue
EOF

OUT="$(./wish "$BATCH7" 2>&1 || true)"
ERRS="$(printf "%s" "$OUT" | grep -c "An error has occurred" || true)"
echo "$OUT" | grep -q "^sigue$" || fail "El shell no continuó tras errores de built-ins"
[ "$ERRS" -ge 2 ] || fail "Se esperaban >=2 errores (exit con arg, cd sin arg)"
pass "Errores built-ins OK"

# 10) Test: invocar con >1 archivos => exit code 1
echo -e "${BOLD}Prueba: invocación con >1 argumentos (exit 1)${RST}"
touch "$TMPDIR/a.txt" "$TMPDIR/b.txt"
set +e
./wish "$TMPDIR/a.txt" "$TMPDIR/b.txt" >/dev/null 2>&1
RC=$?
set -e
[ "$RC" -eq 1 ] || fail "Se esperaba código de salida 1; got $RC"
pass "Invocación inválida OK"

echo -e "${BOLD}${GRN}Todas las pruebas PASARON ✔${RST}"
