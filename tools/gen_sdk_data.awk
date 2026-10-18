# Emit one guest SDK file as a C string literal, one source line per
# string element. Fed to awk as `awk -f gen_sdk_data.awk <file>`; the
# caller prints the declaration and the trailing semicolon.
#
# Escaping: backslashes and double quotes only — the SDK files are plain
# ASCII C/ld/asm text, and every line becomes its own literal so the
# output stays readable and diffable.
{
    gsub(/\\/, "\\\\")
    gsub(/"/, "\\\"")
    printf "    \"%s\\n\"\n", $0
}
