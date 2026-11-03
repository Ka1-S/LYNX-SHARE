#!/usr/bin/env python3
"""Splits tests/tests.cpp (with every #include "*.inc" it pulls in) into N translation units at build time.

tests/tests.cpp stays the single source of truth and keeps compiling on its own (the fallback). Each generated chunk is
the whole test text with the .inc files inlined (#line keeps __FILE__ / __LINE__ as in the single-TU build), but only
the bodies of the TESTs it owns: every other TEST body is emptied (its newlines kept). So helpers stay visible to every
chunk while the heavy part (the ~1400 test bodies) is compiled once, spread over N parallel compilers.

What is shared between the chunks (it lives in lynx_split_main.cpp, compiled once):
  - the check / failure counters, the FL data folder (realFlpDir) and the test registry;
  - the registry is sorted by the TEST's position in the text, so tests run in exactly the single-TU order.
main() is kept in chunk 0 only; namespace-scope definitions outside anonymous namespaces get `inline` (one copy).

usage: split-tests.py <tests.cpp> <out dir> <N>      exit status != 0: use the single-TU build
"""
import os
import re
import sys


def fail(msg):
    sys.stderr.write("split-tests: " + msg + "\n")
    sys.exit(1)


INC = re.compile(r'^\s*#\s*include\s+"([^"]+\.inc)"\s*(//.*)?$')
QUOTE = re.compile(r'^(\s*#\s*include\s+)"([^"]+)"(.*)$')
ANY_INC = re.compile(r'^\s*#\s*include\s*[<"][^>"]+\.inc[>"]', re.M)


def expand(path, display, seen, out_dir):
    """The text of `path` with its .inc includes inlined; #line directives keep __FILE__/__LINE__ unchanged."""
    if path in seen:
        fail("recursive include of " + path)
    seen = seen | {path}
    out = ['#line 1 "%s"\n' % display]
    with open(path, encoding="utf-8") as f:
        lines = f.readlines()
    if lines and not lines[-1].endswith("\n"):
        lines[-1] += "\n"
    for i, line in enumerate(lines, 1):
        m = INC.match(line)
        if m:
            inc = os.path.join(os.path.dirname(path), m.group(1))
            if not os.path.isfile(inc):
                fail("missing " + inc)
            out.append(expand(inc, os.path.join(os.path.dirname(display), m.group(1)), seen, out_dir))
            out.append('#line %d "%s"\n' % (i + 1, display))
        else:
            q = QUOTE.match(line)
            if q and os.path.isfile(os.path.join(os.path.dirname(path), q.group(2))):
                # a header next to the tests (tests/DvsSignal.h): the chunks live elsewhere, point at it from there
                rel = os.path.relpath(os.path.join(os.path.dirname(path), q.group(2)), out_dir)
                line = '%s"%s"%s\n' % (q.group(1), rel, q.group(3))
            out.append(line)
    return "".join(out)


def scan(text):
    """Lexes the C++ text: TEST bodies (start, end, name, head span) and namespace-scope definitions that need `inline`.

    Skips comments, string / char / raw string literals and preprocessor lines. Tracks the scope stack: 'A' anonymous
    namespace, 'N' named namespace, 'O' anything else (class, function, initializer...)."""
    n = len(text)
    i = 0
    stack = []
    tests = []
    pending_kind = None  # what the next '{' opens: 'A', 'N' or None
    stmt_start = None  # start of the current namespace-scope statement (outside anonymous namespaces)
    stmt_tokens = []
    inline_at = []  # offsets where `inline ` must be inserted
    only_first = []  # [start, end) of definitions kept in chunk 0 only (blanked elsewhere, newlines kept)
    alloc_depth = None
    test_head = None  # (name, head start, head end) waiting for its body
    line_start = True
    ident = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")
    while i < n:
        c = text[i]
        if c == "\n":
            line_start = True
            i += 1
            continue
        if c in " \t\r\f\v":
            i += 1
            continue
        if line_start and c == "#":
            # preprocessor line (with continuations)
            while i < n:
                j = text.find("\n", i)
                if j < 0:
                    i = n
                    break
                if text[j - 1] == "\\" or (text[j - 1] == "\r" and text[j - 2] == "\\"):
                    i = j + 1
                    continue
                i = j
                break
            continue
        line_start = False
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            if j < 0:
                fail("unterminated comment")
            i = j + 2
            continue
        m = re.compile(r'(?:u8|u|U|L)?R"([^ ()\\\t\n]{0,16})\(').match(text, i)
        if m and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            end = text.find(")" + m.group(1) + '"', m.end())
            if end < 0:
                fail("unterminated raw string")
            i = end + len(m.group(1)) + 2
            stmt_tokens.append("lit")
            continue
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                if text[j] == "\\":
                    j += 1
                elif text[j] == "\n":
                    fail("newline in a literal at offset %d" % i)
                j += 1
            i = j + 1
            stmt_tokens.append("lit")
            continue
        if c.isdigit() or (c == "." and i + 1 < n and text[i + 1].isdigit()):
            # pp-number (digit separators ' included)
            j = i + 1
            while j < n and (text[j].isalnum() or text[j] in "._" or (text[j] == "'" and j + 1 < n and text[j + 1].isalnum())
                             or (text[j] in "+-" and text[j - 1] in "eEpP")):
                j += 1
            i = j
            stmt_tokens.append("num")
            continue
        m = ident.match(text, i)
        if m:
            word = m.group(0)
            at_ns_scope = all(s != "O" for s in stack)
            if at_ns_scope and not stmt_tokens:
                stmt_start = i
            if at_ns_scope and word == "TEST" and not stmt_tokens:
                m2 = re.compile(r"TEST\s*\(\s*([A-Za-z_][A-Za-z_0-9]*)\s*\)").match(text, i)
                if not m2:
                    fail("unexpected TEST at offset %d" % i)
                test_head = (m2.group(1), i, m2.end())
                i = m2.end()
                stmt_tokens.append("TEST")
                continue
            if word == "namespace" and at_ns_scope and not stmt_tokens:
                k = i + len(word)
                rest = re.compile(r"\s*([A-Za-z_][A-Za-z_0-9:]*)?\s*\{").match(text, k)
                if rest:
                    pending_kind = "N" if rest.group(1) else "A"
            stmt_tokens.append(word)
            i = m.end()
            continue
        # punctuation
        if c == "{":
            if test_head is not None:
                tests.append({"name": test_head[0], "head": (test_head[1], test_head[2]), "open": i})
                test_head = None
                stack.append("T")
                i += 1
                continue
            if pending_kind:
                stack.append(pending_kind)
                pending_kind = None
                stmt_tokens = []
                i += 1
                continue
            if all(s != "O" and s != "T" for s in stack) and stmt_tokens:
                if "operator" in stmt_tokens and ("new" in stmt_tokens or "delete" in stmt_tokens) and "A" not in stack:
                    # a replacement operator new / delete: one definition in the program (chunk 0), never inline
                    only_first.append([stmt_start, None])
                    alloc_depth = len(stack)
                else:
                    note_definition(stack, stmt_tokens, stmt_start, inline_at, "{")
            stack.append("O")
            i += 1
            continue
        if c == "}":
            if not stack:
                fail("unbalanced } at offset %d" % i)
            kind = stack.pop()
            if alloc_depth is not None and len(stack) == alloc_depth:
                only_first[-1][1] = i + 1
                alloc_depth = None
            if kind == "T":
                tests[-1]["close"] = i
                stmt_tokens = []
            elif kind in "AN":
                stmt_tokens = []
            elif all(s != "O" and s != "T" for s in stack):
                # end of a class / function / initializer body at namespace scope
                if stmt_tokens and stmt_tokens[0] not in ("struct", "class", "union", "enum") and not top_level_eq(stmt_tokens):
                    stmt_tokens = []  # a function definition ends without ';' (an '=' inside its parameters is a default)
            i += 1
            continue
        if c == ";":
            if all(s != "O" and s != "T" for s in stack):
                if stmt_tokens:
                    note_definition(stack, stmt_tokens, stmt_start, inline_at, ";")
                stmt_tokens = []
            i += 1
            continue
        if all(s != "O" and s != "T" for s in stack):
            if not stmt_tokens and c not in "()[]<>,":
                stmt_start = i
            stmt_tokens.append(c)
        i += 1
    if stack:
        fail("unbalanced braces at the end (%s)" % "".join(stack))
    for t in tests:
        if "close" not in t:
            fail("TEST %s without a body" % t["name"])
    return tests, inline_at, only_first


def top_level_eq(tokens):
    """An '=' outside parentheses: a variable with an initializer (it ends with ';'), not a function definition."""
    depth = 0
    for t in tokens:
        if t == "(":
            depth += 1
        elif t == ")":
            depth -= 1
        elif t == "=" and depth == 0:
            return True
    return False


NON_DEFS = {"struct", "class", "union", "enum", "using", "typedef", "template", "static", "inline", "extern",
            "constexpr", "consteval", "constinit", "static_assert", "namespace", "friend", "TEST", "thread_local",
            "LYNX_SPLIT_MAIN"}


def note_definition(stack, tokens, start, inline_at, end):
    """A namespace-scope declaration outside an anonymous namespace that defines a function or a variable gets `inline`
    (the same definition is compiled in every chunk)."""
    if "A" in stack or start is None:
        return
    first = tokens[0]
    if first in NON_DEFS or not (first[0].isalpha() or first[0] == "_" or first == ":"):
        return
    if end == ";" and ("(" in tokens and "=" not in tokens and tokens.index("(") < len(tokens)):
        # a function declaration (or a variable with a constructor call: rare in tests, also fine with inline)
        if "=" not in tokens and ")" in tokens and tokens[-1] == ")":
            return
    inline_at.append(start)


def main():
    if len(sys.argv) != 4:
        fail("usage: split-tests.py <tests.cpp> <out dir> <N>")
    src, out, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
    if n < 2:
        fail("N must be >= 2")
    text = expand(src, src, frozenset(), out)

    # the state shared by the chunks: exact lines of tests/tests.cpp (if one changes, the split is refused -> single TU)
    replace = [
        ("int failures = 0;", "int& failures = lynxSplitFailures;"),
        ("int checks = 0;", "int& checks = lynxSplitChecks;"),
        ("    static std::vector<std::pair<const char*, std::function<void()>>> r;", "    auto& r = lynxSplitRegistry();"),
        ("std::string realFlpDir;", "std::string& realFlpDir = lynxSplitFlpDir;"),
        ("int main (int argc, char** argv)", "LYNX_SPLIT_MAIN (int argc, char** argv)"),
    ]
    lines = text.split("\n")
    for old, new in replace:
        hits = [k for k, l in enumerate(lines) if l == old]
        if len(hits) != 1:
            fail("expected exactly one line %r in %s, found %d" % (old, src, len(hits)))
        lines[hits[0]] = new
    text = "\n".join(lines)
    if ANY_INC.search(text):
        fail("an .inc include was not inlined: " + ANY_INC.search(text).group(0))

    tests, inline_at, only_first = scan(text)
    if len(tests) < 2:
        fail("no TESTs found")
    names = [t["name"] for t in tests]
    if len(set(names)) != len(names):
        fail("duplicate TEST names")

    # balance the chunks by body size (greedy, biggest first); the order inside the registry is the text order
    owner = [0] * len(tests)
    load = [0] * n
    for k in sorted(range(len(tests)), key=lambda k: tests[k]["open"] - tests[k]["close"]):
        c = min(range(n), key=lambda c: load[c])
        owner[k] = c
        load[c] += tests[k]["close"] - tests[k]["open"]

    os.makedirs(out, exist_ok=True)

    def write(path, content):
        # only rewrite a file whose content changed (keeps the compiler caches warm)
        try:
            with open(path, encoding="utf-8") as f:
                if f.read() == content:
                    return
        except OSError:
            pass
        with open(path + ".tmp", "w", encoding="utf-8") as f:
            f.write(content)
        os.replace(path + ".tmp", path)

    edits_inline = sorted(set(inline_at))
    for c in range(n):
        parts = []
        pos = 0
        events = [(t["head"][0], "head", k) for k, t in enumerate(tests)] + \
                 [(t["open"] + 1, "body", k) for k, t in enumerate(tests) if owner[k] != c] + \
                 [(p, "inline", -1) for p in edits_inline] + \
                 ([(a, "blank", e) for a, e in only_first] if c != 0 else [])
        for at, kind, k in sorted(events):
            parts.append(text[pos:at])
            if kind == "blank":
                parts.append("\n" * text[at:k].count("\n"))
                pos = k
            elif kind == "inline":
                parts.append("inline ")
                pos = at
            elif kind == "head":
                t = tests[k]
                head = ("LYNX_TEST_OWN (%s, %d)" % (t["name"], k)) if owner[k] == c else ("LYNX_TEST_SKIP (%s)" % t["name"])
                parts.append(head)
                pos = t["head"][1]
            else:
                body = text[at:tests[k]["close"]]
                parts.append("\n" * body.count("\n"))
                pos = tests[k]["close"]
        parts.append(text[pos:])
        prelude = ("// generated by scripts/split-tests.py from %s: do not edit\n"
                   "#define LYNX_SPLIT_CHUNK %d\n#include \"lynx_split.h\"\n" % (src, c))
        # helpers used only by the tests of other chunks are unused here: those warnings are the split's, not the code's
        prelude += ("#pragma GCC diagnostic ignored \"-Wunused-function\"\n"
                    "#pragma GCC diagnostic ignored \"-Wunused-variable\"\n"
                    "#pragma GCC diagnostic ignored \"-Wunused-const-variable\"\n"
                    "#ifdef __clang__\n#pragma clang diagnostic ignored \"-Wunneeded-internal-declaration\"\n"
                    "#pragma clang diagnostic ignored \"-Wunused-template\"\n"
                    "#pragma clang diagnostic ignored \"-Wunused-member-function\"\n#endif\n")
        write(os.path.join(out, "chunk%02d.cpp" % c), prelude + "".join(parts))

    write(os.path.join(out, "lynx_split.h"), r'''// generated by scripts/split-tests.py: do not edit
#pragma once
#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>
extern int lynxSplitFailures;
extern int lynxSplitChecks;
extern std::string lynxSplitFlpDir;
std::vector<std::pair<const char*, std::function<void()>>>& lynxSplitRegistry();
void lynxSplitAdd (int index, const char* name, void (*fn)());
struct LynxSplitReg
{
    LynxSplitReg (int index, const char* name, void (*fn)()) { lynxSplitAdd (index, name, fn); }
};
#define LYNX_TEST_OWN(name, index) \
    static void name();             \
    static LynxSplitReg reg_##name (index, #name, name); \
    static void name()
#define LYNX_TEST_SKIP(name) [[maybe_unused]] static void name()
#if LYNX_SPLIT_CHUNK == 0
#define LYNX_SPLIT_MAIN int main
#else
#define LYNX_SPLIT_MAIN [[maybe_unused]] static int lynxSplitUnusedMain
#endif
''')
    write(os.path.join(out, "lynx_split_main.cpp"), r'''// generated by scripts/split-tests.py: do not edit
#include "lynx_split.h"
#include <tuple>
int lynxSplitFailures = 0;
int lynxSplitChecks = 0;
std::string lynxSplitFlpDir;
static std::vector<std::tuple<int, const char*, void (*)()>>& lynxSplitPending()
{
    static std::vector<std::tuple<int, const char*, void (*)()>> p;
    return p;
}
void lynxSplitAdd (int index, const char* name, void (*fn)()) { lynxSplitPending().emplace_back (index, name, fn); }
std::vector<std::pair<const char*, std::function<void()>>>& lynxSplitRegistry()
{
    // called from main() only (after every chunk registered its tests): the single-TU order
    static std::vector<std::pair<const char*, std::function<void()>>> r;
    if (r.empty())
    {
        auto p = lynxSplitPending();
        std::sort (p.begin(), p.end(), [] (const auto& a, const auto& b) { return std::get<0> (a) < std::get<0> (b); });
        for (auto& [index, name, fn] : p)
            r.emplace_back (name, fn);
    }
    return r;
}
''')
    # stale chunks of an earlier, bigger N
    for f in os.listdir(out):
        m = re.match(r"chunk(\d+)\.cpp$", f)
        if m and int(m.group(1)) >= n:
            os.remove(os.path.join(out, f))
    print("split-tests: %d tests in %d chunks (%d inline definitions)" % (len(tests), n, len(edits_inline)))


if __name__ == "__main__":
    main()
