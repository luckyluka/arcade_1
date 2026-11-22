#!/usr/bin/env python3
"""M4.1c conformance: the C expression pipeline vs the oracle.

`arcforge/c/expr.c` (accepted through tests/expr_harness.c) must reproduce
`tools/arcforge_ref.py`'s ev / to_int / truthiness BYTE-FOR-BYTE for a
deterministic corpus (arcforge AGENTS rule 8: Python stays the oracle
until M4.3 flips the gate).

Each case is one of
    ["ev",     expr, env]    -> repr(ev(expr, env))  | ERR str(e) | CRASH
    ["to_int", value]        -> str(to_int(value))   | ERR str(e) | CRASH
    ["truthy", value]        -> str(bool(value))
where ERR carries the ExprError text and CRASH stands for an uncaught
Python exception -- the oracle's empty stdout + exit 1 (expr.h).

Corpus (deterministic, 7-bit ASCII, no randomness):
  * literal returns (int/float/bool, in-range and the range edges);
  * arithmetic over int/float operands incl. all three zero-divisions;
  * string +, *, membership and ordering;
  * comparisons across int/float/str/bool/None (TypeError -> message);
  * unary -, +, not, ~ (unsupported);
  * BoolOp + the oracle's $var / && / || / ! prep shorthands;
  * ternaries / lambdas / comprehensions: accepted, then "unsupported";
  * every FUNCS entry, wrong arity, unknown function, keyword args;
  * variables ($known, $missing, bare identifier, true/false/None);
  * syntax errors (empty, unbalanced, stray operators, unterminated str);
  * int-range violations at both ends;
  * nested parens inside the evaluation depth;
  * non-string expressions (null/list/dict -> the oracle's TypeError);
  * to_int over every runtime type incl. the crash classes;
  * truthiness over every runtime type.

Known, deliberate divergences (spec'd, exercised as `known` cases and
reported as `--- KNOWN`, never as a failure):
  * hex-float literals (0x1p3): Python's tokenizer accepts them, the C
    lexer reports a syntax error;
  * \\N{...} Unicode name escapes;
  * integer literals beyond int64 (Python parses, then range-checks the
    exact value; the C lexer cannot represent it);
  * the RecursionError band (~1500 nested parens): Python raises an
    uncaught RecursionError where C stops at AF_PARSE_DEPTH;
  * `str * N` beyond AF_STR_REPEAT_CAP (64 MB), non-ASCII non-printables
    inside repr(), lone-surrogate escapes, exotic Unicode whitespace and
    digits, non-ASCII identifiers -- not corpus-reachable here.

Both binaries (plain + -fsanitize=undefined,address) must agree with the
oracle, twice in a row (determinism). Exit code 0 = all checks hold.
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import arcforge_ref as A          # noqa: E402

fails = 0


def check(name, ok, detail=""):
    global fails
    print(f"{'PASS' if ok else 'FAIL'}  {name} {detail}")
    fails += 0 if ok else 1


# --- 1. build both harnesses (plain + sanitizers) --------------------------
for extra, binname in ((["harness"], "arcforge_expr_harness"),
                       (["SAN=1", "harness"], "arcforge_expr_harness_san")):
    p = subprocess.run(["make", "-C", str(ROOT / "c")] + extra,
                       capture_output=True, text=True)
    check(f"build {binname} clean (-Wall -Wextra -Werror)",
          p.returncode == 0, (p.stdout + p.stderr)[-400:])

HARNESS = ROOT / "build" / "arcforge_expr_harness"
HARNESS_SAN = ROOT / "build" / "arcforge_expr_harness_san"

# --- 2. the deterministic corpus -------------------------------------------
strict = []      # (kind, payload tuple) -- must match byte for byte
known = []       # (label, payload)     -- spec'd divergences


def ev(expr, env=None):
    strict.append(["ev", expr, env if env is not None else {}])


def to_int(v):
    strict.append(["to_int", v])


def truthy(v):
    strict.append(["truthy", v])


# literal returns + range edges
for lit in (0, 1, -1, 2, 2147483647, -2147483648, 2.0, -0.5, 0.1, True, False):
    ev(lit)
ev(2147483648)          # literal returns are NOT range-checked ...
ev(-2147483649)
ev("2147483648")        # ... but the same digits as source are
ev("-2147483649")

# arithmetic over int/float operands (incl. all three zero-divisions)
ints = [0, 1, 2, 3, -1, -7, 2147483647]
floats = [0.5, -2.5, 2.5, -0.25]
for a in ints:
    for b in ints:
        for op in ("+", "-", "*", "/", "//", "%"):
            ev(f"{a} {op} {b}")
for a in ints[:4]:
    for b in floats:
        for op in ("+", "-", "*", "/", "//", "%"):
            ev(f"{a} {op} {b}")
            ev(f"{b} {op} {a}")
for a in floats:
    for b in floats:
        for op in ("+", "-", "*", "/", "//", "%"):
            ev(f"{a} {op} {b}")
# unsupported binops (the oracle has no **, <<, &, ^)
for expr in ("2 ** 3", "1 << 3", "6 & 3", "6 | 3", "6 ^ 3", "5 % 0.0"):
    ev(expr)

# strings: concat, repeat, membership, ordering
strs = ["''", "'a'", "'ab'", "'hello world'"]
for a in strs:
    for b in strs:
        for op in ("+", "==", "!=", "<", "<=", "in", "not in"):
            ev(f"{a} {op} {b}")
for s in strs:
    for n in (0, 1, 3, -1):
        ev(f"{s} * {n}")
        ev(f"{n} * {s}")
for expr in ("'a' * 2.5", "3 * 'ab'", "'x' * True", "len('abc')"):
    ev(expr)

# comparisons across runtime types
cmp_vals = ["1", "2", "1.5", "True", "False", "None", "'a'", "'b'"]
for a in cmp_vals:
    for b in cmp_vals:
        for op in ("==", "!=", "<", "<=", ">", ">="):
            ev(f"{a} {op} {b}")
ev("1 < 2 < 3")
ev("3 > 2 > 1")
ev("1 < 'a'")

# unary
for a in ("1", "-2", "2.5", "True", "None", "'x'", "-2147483648"):
    for op in ("-", "+", "not ", "~"):
        ev(f"{op}{a}")

# BoolOp + the oracle's && / || / ! prep
for expr in ("1 and 2", "0 and 2", "1 or 2", "0 or 2", "'' or 'x'",
             "'a' and '' and 'b'", "1 && 0", "0 || 5", "!0", "!1",
             "!''", "!None", "$h > 5 && $h < 10", "0 || 0 and 1"):
    ev(expr, {"h": 40})

# accepted-but-unsupported grammar (ternary, lambda, comprehension, slice)
for expr in ("1 if 2 < 3 else 4", "lambda x: x", "[x for x in [1, 2]]",
             "[1, 2][0]", "'ab'[0]", "(yield 1)", "1 if True else 2",
             "{k: 1 for k in ['a']}", "None"):
    ev(expr)

# functions: every FUNCS entry, wrong arity, unknown, keyword args
for v in ("-3.7", "2.5", "-2", "3", "0.5", "7"):
    for fn in ("floor", "ceil", "round", "abs", "odd", "even", "half"):
        ev(f"{fn}({v})")
for expr in ("min(3, 1, 2)", "max(3, 1, 2)", "clamp(5, 1, 3)",
             "clamp(-5, 1, 3)", "min()", "abs(1, 2)", "min(1, 'a')",
             "max(1)", "round(1.5, ndigits=0)", "sqrt(4)", "floor('x')",
             "floor(None)", "floor(True)", "abs('a')", "clamp(1, 2)"):
    ev(expr)

# variables: known, missing, bare identifier, constants
env = {"h": 40, "name": "'knight'", "flag": True, "f": 0.5, "none": None,
       "lst": [1, 2]}
for expr in ("$h", "$h + 1", "$h / 4", "$missing", "$", "$1h", "h", "flag",
             "true", "false", "$flag and true", "$f * 2", "$none",
             "$lst", "$name + '!'", "$name * 3"):
    ev(expr, env)

# syntax errors
for expr in ("", "   ", "1 +", "(1", "1)", "1 2", "@", "1 if", "'unterminated",
             "0x", "+", ")", "'a' 'b'", "1 +\n2", "\\"):
    ev(expr)

# nested parens inside the evaluation depth
for depth in (1, 10, 100):
    ev("(" * depth + "1" + ")" * depth)

# non-string expressions: the oracle's _prep raises TypeError -> CRASH
ev(None)
ev([1, 2])
ev({"a": 1})

# to_int: every runtime type, floors, crash classes
for v in (0, 1, -1, 2147483647, -2147483648, 2.5, -2.5, 0.9999, -0.0001,
          1e17, -1e17, True, False, 0.0, -0.0, 1.5, -1.5, 123456789.25,
          1000000.75):
    to_int(v)
for v in (None, "s", "", [1], [], {"a": 1}, {}):
    to_int(v)

# truthiness: every runtime type
for v in (0, 1, -1, 0.0, 0.5, True, False, None, "", "x", [], [1], {},
          {"a": 1}):
    truthy(v)

# --- known, spec'd divergences (never a failure) ---------------------------
known.append(("hex-float 0x1p3", ["ev", "0x1p3", {}]))
known.append(("\\N{...} name escape",
              ["ev", "'\\N{GREEK SMALL LETTER ALPHA}'", {}]))
known.append(("int literal beyond int64",
              ["ev", "99999999999999999999", {}]))
known.append(("RecursionError band (1500 parens)",
              ["ev", "(" * 1500 + "1" + ")" * 1500, {}]))

strict_count = len(strict)
all_cases = strict + [c for _, c in known]


def expected_line(c):
    kind = c[0]
    try:
        if kind == "ev":
            return "OK " + repr(A.ev(c[1], c[2]))
        if kind == "to_int":
            return "OK " + str(A.to_int(c[1]))
        return "OK " + str(bool(c[1]))
    except A.ExprError as e:
        return "ERR " + str(e)
    except Exception:
        return "CRASH"


# --- 3. run both harnesses, twice ------------------------------------------
tmp = Path("/tmp/opencode")
tmp.mkdir(parents=True, exist_ok=True)
case_file = tmp / "expr_cases.json"
case_file.write_text(json.dumps(all_cases, ensure_ascii=True))

want = [expected_line(c) for c in all_cases]

outputs = {}
for binname, path in (("plain", HARNESS), ("san", HARNESS_SAN)):
    runs = []
    for _ in range(2):
        p = subprocess.run([str(path), str(case_file)],
                           capture_output=True,
                           env={"ASAN_OPTIONS": "detect_leaks=0",
                                "PATH": "/usr/bin:/bin"} if binname == "san"
                           else None)
        runs.append(p)
    check(f"{binname} run rc=0", runs[0].returncode == 0,
          runs[0].stderr.decode(errors="replace")[-300:])
    check(f"{binname} deterministic (two runs byte-identical)",
          runs[0].stdout == runs[1].stdout)
    check(f"{binname} output is 7-bit ASCII", runs[0].stdout.isascii())
    outputs[binname] = runs[0].stdout

lines = {k: v.decode("utf-8", "replace").splitlines()
         for k, v in outputs.items()}

for binname in ("plain", "san"):
    got = lines[binname]
    check(f"{binname} one line per case",
          len(got) == len(want), f"{len(got)} vs {len(want)}")
    mismatch = 0
    known_div = 0
    for i, (g, w) in enumerate(zip(got, want)):
        if i < strict_count:
            if g != w:
                mismatch += 1
                if mismatch <= 5:
                    print(f"    [{i}] {all_cases[i][0]} {all_cases[i][1]!r}\n"
                          f"        py: {w!r}\n"
                          f"        c : {g!r}")
        else:
            if g != w:
                known_div += 1
                print(f"    --- KNOWN {known[i - strict_count][0]}: "
                      f"py {w!r} vs c {g!r}")
            else:
                print(f"    --- KNOWN {known[i - strict_count][0]}: "
                      f"agrees (not diverging)")
    check(f"{binname} matches the oracle on all {strict_count} strict cases",
          mismatch == 0, f"{mismatch} mismatches")
    check(f"{binname} known divergences reported, not failed",
          known_div <= len(known), f"{known_div}/{len(known)} diverge")

check("plain and sanitizers agree", lines["plain"] == lines["san"])

# --- 4. coverage: the corpus must exercise what it claims ------------------
kinds = [c[0] for c in strict]
check("corpus size", len(strict) >= 800, f"{len(strict)} strict cases")
check("ev cases dominate", kinds.count("ev") >= 600, kinds.count("ev"))
check("to_int cases", kinds.count("to_int") >= 25, kinds.count("to_int"))
check("truthy cases", kinds.count("truthy") >= 14, kinds.count("truthy"))

flat = "\n".join(lines["plain"][:strict_count])
for needle in ("ERR division by zero", "ERR floor division by zero",
               "ERR modulo by zero", "ERR unknown variable $missing",
               "ERR unknown identifier 'h'", "ERR unsupported expression",
               "ERR unsupported comparison",
               "ERR wrong number or type of arguments to min()",
               "ERR integer 2147483648 is outside the valid range "
               "-2147483648..2147483647",
               "ERR boolean used as a number",
               "ERR syntax error in expression '1 +'",
               'ERR type error in expression "\'a\' * 2.5"',
               "CRASH"):
    check(f"coverage: {needle[:60]!r}", needle in flat)

ok_count = sum(1 for l in lines["plain"][:strict_count] if l.startswith("OK "))
check("OK results present", ok_count >= 400, ok_count)

print(f"\n{'ALL GREEN' if not fails else str(fails) + ' FAILED'}")
sys.exit(1 if fails else 0)
