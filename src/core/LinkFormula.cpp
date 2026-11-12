#include "core/LinkFormula.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace lynx::linkformula
{
namespace
{
std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

std::string trimmed (const std::string& s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace ((unsigned char) s[a]))
        ++a;
    while (b > a && std::isspace ((unsigned char) s[b - 1]))
        --b;
    return s.substr (a, b - a);
}

// Round half to even, as FL's Round() does.
double roundEven (double x)
{
    return std::nearbyint (x);
}

double truth (bool b) { return b ? 1.0 : 0.0; }

std::uint32_t& randomState()
{
    static std::uint32_t state = 0x9e3779b9u;
    return state;
}

double nextRandom()
{
    auto& s = randomState();
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (double) (s >> 8) / (double) (1u << 24);
}

double seededRandom (double seed)
{
    std::uint64_t bits = 0;
    std::memcpy (&bits, &seed, sizeof bits);
    bits ^= bits >> 33;
    bits *= 0xff51afd7ed558ccdull;
    bits ^= bits >> 33;
    bits *= 0xc4ceb9fe1a85ec53ull;
    bits ^= bits >> 33;
    return (double) (bits >> 40) / (double) (1ull << 24);
}

using Fn = double (*) (const double*);

struct Function
{
    const char* name;
    int arity;
    Fn call;
};

constexpr double pi = 3.14159265358979323846;

// The names FL's formula accepts. First FL's own additions, then the general maths set of its expression parser.
const Function functions[] = {
    { "rand", 0, [] (const double*) { return nextRandom(); } },
    { "seededrand", 1, [] (const double* a) { return seededRandom (a[0]); } },
    { "min", 2, [] (const double* a) { return a[1] <= a[0] ? a[1] : a[0]; } },
    { "max", 2, [] (const double* a) { return a[0] <= a[1] ? a[1] : a[0]; } },
    { "int", 1, [] (const double* a) { return std::trunc (a[0]); } },
    { "round", 1, [] (const double* a) { return roundEven (a[0]); } },
    { "frac", 1, [] (const double* a) { return a[0] - std::trunc (a[0]); } },
    { "ife", 2, [] (const double* a) { return truth (a[0] == a[1]); } },
    { "ifl", 2, [] (const double* a) { return truth (a[0] < a[1]); } },
    { "ifg", 2, [] (const double* a) { return truth (a[0] > a[1]); } },
    { "ifle", 2, [] (const double* a) { return truth (a[0] <= a[1]); } },
    { "ifge", 2, [] (const double* a) { return truth (a[0] >= a[1]); } },
    { "case", 3, [] (const double* a) { return a[0] == 1.0 ? a[1] : a[2]; } },
    { "sum", 2, [] (const double* a) { return a[0] + a[1]; } },
    { "neg", 1, [] (const double* a) { return -a[0]; } },
    { "log2", 1, [] (const double* a) { return std::log2 (a[0]); } },
    { "tg", 1, [] (const double* a) { return std::tan (a[0]); } },
    { "ctg", 1, [] (const double* a) { return 1.0 / std::tan (a[0]); } },
    { "arctg", 1, [] (const double* a) { return std::atan (a[0]); } },
    { "sec", 1, [] (const double* a) { const auto c = std::cos (a[0]); return c == 0.0 ? 1.0 : 1.0 / c; } },
    { "cosec", 1, [] (const double* a) { const auto s = std::sin (a[0]); return s == 0.0 ? 1.0 : 1.0 / s; } },
    { "tension", 2, [] (const double* a) { return tension (a[0], a[1]); } },

    { "exp", 1, [] (const double* a) { return std::exp (a[0]); } },
    { "if", 3, [] (const double* a) { return a[0] != 0.0 ? a[1] : a[2]; } },
    { "sqr", 1, [] (const double* a) { return a[0] * a[0]; } },
    { "sqrt", 1, [] (const double* a) { return std::sqrt (a[0]); } },
    { "abs", 1, [] (const double* a) { return std::fabs (a[0]); } },
    { "trunc", 1, [] (const double* a) { return std::trunc (a[0]); } },
    { "floor", 1, [] (const double* a) { return std::floor (a[0]); } },
    { "ceil", 1, [] (const double* a) { return std::ceil (a[0]); } },
    { "ln", 1, [] (const double* a) { return std::log (a[0]); } },
    { "log10", 1, [] (const double* a) { return std::log10 (a[0]); } },
    { "logn", 2, [] (const double* a) { return std::log (a[1]) / std::log (a[0]); } },
    { "power", 2, [] (const double* a) { return std::pow (a[0], a[1]); } },
    { "pow", 2, [] (const double* a) { return std::pow (a[0], a[1]); } },
    { "intpower", 2, [] (const double* a) { return std::pow (a[0], std::trunc (a[1])); } },
    { "sin", 1, [] (const double* a) { return std::sin (a[0]); } },
    { "cos", 1, [] (const double* a) { return std::cos (a[0]); } },
    { "tan", 1, [] (const double* a) { return std::tan (a[0]); } },
    { "arcsin", 1, [] (const double* a) { return std::asin (a[0]); } },
    { "arccos", 1, [] (const double* a) { return std::acos (a[0]); } },
    { "arctan", 1, [] (const double* a) { return std::atan (a[0]); } },
    { "arctan2", 2, [] (const double* a) { return std::atan2 (a[0], a[1]); } },
    { "atan2", 2, [] (const double* a) { return std::atan2 (a[0], a[1]); } },
    { "sinh", 1, [] (const double* a) { return std::sinh (a[0]); } },
    { "cosh", 1, [] (const double* a) { return std::cosh (a[0]); } },
    { "tanh", 1, [] (const double* a) { return std::tanh (a[0]); } },
    { "arcsinh", 1, [] (const double* a) { return std::asinh (a[0]); } },
    { "arccosh", 1, [] (const double* a) { return std::acosh (a[0]); } },
    { "arctanh", 1, [] (const double* a) { return std::atanh (a[0]); } },
    { "degtorad", 1, [] (const double* a) { return a[0] * pi / 180.0; } },
    { "radtodeg", 1, [] (const double* a) { return a[0] * 180.0 / pi; } },
};

// FL's own expression functions (round 190cs, [H 0xbaec40] registrations, laws read from each handler):
// inter (x, a, b): 1 when x lies between a and b (either order), else 0; internoswap: the same with a <= b only. Both
// compare at 1/1000 steps (each value is rounded to thousandths first) [H 0xbae920, 0xbae9c0].
double interValue (double x, double a, double b, bool swap)
{
    const auto r = [] (double v) { return std::nearbyint (v * 1000.0); };
    const auto xi = r (x), ai = r (a), bi = r (b);
    if (swap && ! (ai < bi))
        return truth (! (xi < bi || ai < xi));
    return truth (! (xi < ai || bi < xi));
}

// fold (x): a triangle that goes 0 -> 1 -> 0 as x runs 0 -> 1 -> 2 (period 2, any x) [H 0xbaea40].
double foldValue (double x)
{
    const auto v = x * 2.0 - 1.0;
    auto t = std::fabs (v) * 0.25;
    t -= std::floor (t);
    double r;
    if (t < 0.25)
        r = t * 4.0;
    else if (t < 0.75)
        r = 1.0 - (t - 0.25) * 4.0;
    else
        r = (t - 0.75) * 4.0 - 1.0;
    if (v < 0.0)
        r = -r;
    return r / 2.0 + 0.5;
}

// FL's FMod: x - Trunc (x / y) * y (keeps the sign of x) [H 0x477690].
double flMod (double x, double y)
{
    const auto q = x / y;
    if (! std::isfinite (q))
        return std::nan ("");
    return x - std::trunc (q) * y;
}

// warp (x, p): x inside 0...1 stays (1 itself wraps to 0); outside, x + p wrapped by FMod 1 [H 0xbaebb0].
double warpValue (double x, double p)
{
    if (x > 1.0 || x < 0.0)
        x += p;
    return flMod (x, 1.0);
}

// x! (the factorial of x rounded) [H 0xbae800].
double factorial (double x)
{
    const auto n = std::nearbyint (x);
    if (n < 0.0)
        return std::nan ("");
    if (n > 170.0)
        return HUGE_VAL;
    double r = 1.0;
    for (int i = 2; i <= (int) n; ++i)
        r *= i;
    return r;
}

// FL compares with a tiny tolerance: |a - b| < 1e-30 [H 0xbae410].
bool same (double a, double b) { return std::fabs (a - b) < 1.0e-30; }

double interFn (const double* a) { return interValue (a[0], a[1], a[2], true); }
double interNoSwapFn (const double* a) { return interValue (a[0], a[1], a[2], false); }

const Function laterFunctions[] = {
    { "inter", 3, interFn },
    { "internoswap", 3, interNoSwapFn },
    { "fold", 1, [] (const double* a) { return foldValue (a[0]); } },
    { "warp", 2, [] (const double* a) { return warpValue (a[0], a[1]); } },
    { "map", 3, [] (const double* a) { return a[0] * (a[2] - a[1]) + a[1]; } },
};

const Function* findFunction (const std::string& name)
{
    for (const auto& f : functions)
        if (name == f.name)
            return &f;
    for (const auto& f : laterFunctions)
        if (name == f.name)
            return &f;
    return nullptr;
}

enum class Op : std::uint8_t
{
    number, input, call,
    add, sub, mul, div, intDiv, mod, fmod, power, neg, logicalNot, factorial, percent,
    logicalAnd, logicalOr, logicalXor, shl, shr,
    equal, notEqual, less, greater, lessEqual, greaterEqual
};

struct Node
{
    Op op = Op::number;
    double value = 0.0;
    const Function* function = nullptr;
    int args[3] { -1, -1, -1 };
};
} // namespace

struct Formula::Program
{
    std::vector<Node> nodes;
    int root = -1;

    double run (int n, double input) const
    {
        const auto& node = nodes[(std::size_t) n];
        const auto a = [&] { return run (node.args[0], input); };
        const auto b = [&] { return run (node.args[1], input); };
        const auto whole = [] (double v) { return (std::int64_t) std::trunc (std::clamp (v, -9.0e18, 9.0e18)); };
        switch (node.op)
        {
            case Op::number: return node.value;
            case Op::input: return input;
            case Op::call:
            {
                double values[3] {};
                for (int i = 0; i < node.function->arity; ++i)
                    values[i] = run (node.args[i], input);
                return node.function->call (values);
            }
            case Op::add: return a() + b();
            case Op::sub: return a() - b();
            case Op::mul: return a() * b();
            case Op::div: return a() / b();
            case Op::intDiv:
            {
                const auto d = whole (b());
                return d == 0 ? std::nan ("") : (double) (whole (a()) / d);
            }
            case Op::mod:
            {
                const auto d = whole (b());
                return d == 0 ? std::nan ("") : (double) (whole (a()) % d);
            }
            case Op::fmod: return flMod (a(), b());
            case Op::power: return std::pow (a(), b());
            case Op::neg: return -a();
            case Op::logicalNot: return truth (std::nearbyint (a()) == 0.0);
            case Op::factorial: return factorial (a());
            case Op::percent: return a() * 0.01;
            case Op::logicalAnd: return truth (a() != 0.0 && b() != 0.0);
            case Op::logicalOr: return truth (a() != 0.0 || b() != 0.0);
            case Op::logicalXor: return truth ((a() != 0.0) != (b() != 0.0));
            case Op::shl: return (double) (whole (a()) << std::clamp<std::int64_t> (whole (b()), 0, 62));
            case Op::shr: return (double) (whole (a()) >> std::clamp<std::int64_t> (whole (b()), 0, 62));
            case Op::equal: return truth (same (a(), b()));
            case Op::notEqual: return truth (! same (a(), b()));
            case Op::less: return truth (a() < b());
            case Op::greater: return truth (a() > b());
            case Op::lessEqual: return truth (a() <= b());
            case Op::greaterEqual: return truth (a() >= b());
        }
        return 0.0;
    }
};

namespace
{
// FL's expression parser, as its operator table registers it ([H 0xbaec40]; a lower number binds tighter):
//   10  prefix - and +, postfix ! (factorial) and % (hundredths), "in" (equality)
//   20  ^
//   30  * / div mod fmod
//   40  + -
//   50  > >= <= < <> =
//   60  not
//   70  or and xor shl shr
// So the unary minus binds tighter than ^: -Input^2 = (-Input)^2, as FL draws it. Equal levels group from the left.
// Its checks run in FL's order: names first ("Invalid identifier: "), then brackets ("Unequal brackets"), then the
// neighbours of brackets and operands, then the tree ("Syntax error: function or operand has too few arguments",
// "Too many arguments", "Syntax error") [H 0xbab340, 0xbacb80, 0xba9ee0, strings near 0xbadab0].
struct Token
{
    enum Kind { number, name, symbol } kind = symbol;
    std::string text;  // as typed (names), the symbol
    std::string lower; // names in lower case
    double value = 0.0;
    std::size_t at = 0;
};

bool isOperatorWord (const std::string& w)
{
    for (const char* o : { "div", "mod", "fmod", "and", "or", "xor", "not", "shl", "shr", "in" })
        if (w == o)
            return true;
    return false;
}

bool isOperandName (const std::string& w) { return w == "input" || w == "pi"; }

class Parser
{
public:
    Parser (const std::string& t, std::vector<Node>& n) : text (t), nodes (n) {}

    int parse()
    {
        if (! tokenize() || ! checkNames() || ! checkBrackets() || ! checkNeighbours())
            return -1;
        const auto root = level70();
        if (! failed && pos < tokens.size())
            fail ("Syntax error", tokens[pos].at);
        return failed ? -1 : root;
    }

    bool failed = false;
    std::string message;
    int errorAt = 0;

private:
    void fail (const std::string& what, std::size_t where)
    {
        if (failed)
            return;
        failed = true;
        message = what;
        errorAt = (int) std::min (where, text.size());
    }

    bool tokenize()
    {
        std::size_t at = 0;
        while (at < text.size())
        {
            const auto c = text[at];
            if (std::isspace ((unsigned char) c))
            {
                ++at;
                continue;
            }
            Token t;
            t.at = at;
            if (std::isdigit ((unsigned char) c) || c == '.' || c == '$')
            {
                t.kind = Token::number;
                if (! number (at, t.value))
                    return false;
            }
            else if (std::isalpha ((unsigned char) c) || c == '_')
            {
                t.kind = Token::name;
                while (at < text.size() && (std::isalnum ((unsigned char) text[at]) || text[at] == '_'))
                    ++at;
                t.text = text.substr (t.at, at - t.at);
                t.lower = lower (t.text);
            }
            else
            {
                t.kind = Token::symbol;
                for (const char* two : { "<>", "<=", ">=" })
                    if (text.compare (at, 2, two) == 0)
                        t.text = two;
                if (t.text.empty())
                {
                    if (std::strchr ("+-*/^()!%,<>=", c) == nullptr)
                    {
                        fail ("Syntax error", at);
                        return false;
                    }
                    t.text = std::string (1, c);
                }
                at += t.text.size();
            }
            tokens.push_back (std::move (t));
        }
        if (tokens.empty())
        {
            fail ("Syntax error: function or operand has too few arguments", 0);
            return false;
        }
        return true;
    }

    bool number (std::size_t& at, double& value)
    {
        const auto start = at;
        if (text[at] == '$') // hexadecimal
        {
            ++at;
            const auto digits = at;
            while (at < text.size() && std::isxdigit ((unsigned char) text[at]))
                ++at;
            if (at == digits)
            {
                fail ("Syntax error", start);
                return false;
            }
            value = (double) std::strtoull (text.substr (digits, at - digits).c_str(), nullptr, 16);
            return true;
        }
        while (at < text.size() && std::isdigit ((unsigned char) text[at]))
            ++at;
        if (at < text.size() && text[at] == '.')
        {
            ++at;
            while (at < text.size() && std::isdigit ((unsigned char) text[at]))
                ++at;
        }
        if (at < text.size() && (text[at] == 'e' || text[at] == 'E'))
        {
            auto p = at + 1;
            if (p < text.size() && (text[p] == '+' || text[p] == '-'))
                ++p;
            if (p < text.size() && std::isdigit ((unsigned char) text[p]))
            {
                while (p < text.size() && std::isdigit ((unsigned char) text[p]))
                    ++p;
                at = p;
            }
        }
        const auto token = text.substr (start, at - start);
        if (token == ".")
        {
            fail ("Syntax error", start);
            return false;
        }
        value = std::strtod (token.c_str(), nullptr); // (the C locale: '.' is the decimal point)
        return true;
    }

    bool checkNames()
    {
        for (const auto& t : tokens)
            if (t.kind == Token::name && ! isOperandName (t.lower) && ! isOperatorWord (t.lower) && findFunction (t.lower) == nullptr)
            {
                fail ("Invalid identifier: " + t.text, t.at);
                return false;
            }
        return true;
    }

    bool checkBrackets()
    {
        int depthNow = 0;
        for (const auto& t : tokens)
            if (t.kind == Token::symbol && t.text == "(")
                ++depthNow;
            else if (t.kind == Token::symbol && t.text == ")")
                --depthNow;
        if (depthNow != 0)
        {
            fail ("Unequal brackets", 0);
            return false;
        }
        return true;
    }

    static bool isOperand (const Token& t)
    {
        return t.kind == Token::number || (t.kind == Token::name && isOperandName (t.lower));
    }
    static bool isSymbol (const Token& t, const char* s) { return t.kind == Token::symbol && t.text == s; }

    bool checkNeighbours()
    {
        for (std::size_t i = 0; i + 1 < tokens.size(); ++i)
        {
            const auto& a = tokens[i];
            const auto& b = tokens[i + 1];
            const auto callBefore = i > 0 && tokens[i - 1].kind == Token::name && findFunction (tokens[i - 1].lower) != nullptr;
            std::string what;
            if (isSymbol (a, "(") && isSymbol (b, ")") && ! callBefore)
                what = "Empty brackets ()";
            else if (isSymbol (a, ")") && isSymbol (b, "("))
                what = "Missing operand between )(";
            else if (isSymbol (a, ")") && isOperand (b))
                what = "Missing operand between ) and constant/variable";
            else if (isOperand (a) && isSymbol (b, "("))
                what = "Missing operand between constant/variable and (";
            else if (isOperand (a) && isOperand (b))
                what = (a.kind == Token::name ? a.lower : text.substr (a.at, b.at - a.at)) + " two space limited variables/constants";
            if (! what.empty())
            {
                while (! what.empty() && what.front() == ' ')
                    what.erase (0, 1);
                fail (singleSpaced (what), b.at);
                return false;
            }
        }
        return true;
    }

    static std::string singleSpaced (std::string s)
    {
        // ("2 " + " two space ..." reads with one space)
        std::string out;
        for (auto c : s)
            if (! (c == ' ' && ! out.empty() && out.back() == ' '))
                out += c;
        return out;
    }

    bool peekSymbol (const char* s) const { return pos < tokens.size() && isSymbol (tokens[pos], s); }
    bool peekWord (const char* w) const { return pos < tokens.size() && tokens[pos].kind == Token::name && tokens[pos].lower == w; }
    bool takeSymbol (const char* s)
    {
        if (! peekSymbol (s))
            return false;
        ++pos;
        return true;
    }
    bool takeWord (const char* w)
    {
        if (! peekWord (w))
            return false;
        ++pos;
        return true;
    }
    std::size_t hereAt() const { return pos < tokens.size() ? tokens[pos].at : text.size(); }

    int add (Op op, int a = -1, int b = -1)
    {
        Node n;
        n.op = op;
        n.args[0] = a;
        n.args[1] = b;
        nodes.push_back (n);
        return (int) nodes.size() - 1;
    }

    bool enter()
    {
        if (++depth > 200)
        {
            fail ("Syntax error", hereAt());
            return false;
        }
        return true;
    }

    int level70()
    {
        if (! enter())
            return -1;
        auto left = level60();
        while (! failed)
        {
            Op op;
            if (takeWord ("or")) op = Op::logicalOr;
            else if (takeWord ("and")) op = Op::logicalAnd;
            else if (takeWord ("xor")) op = Op::logicalXor;
            else if (takeWord ("shl")) op = Op::shl;
            else if (takeWord ("shr")) op = Op::shr;
            else break;
            left = add (op, left, level60());
        }
        --depth;
        return left;
    }

    int level60()
    {
        if (takeWord ("not"))
        {
            if (! enter())
                return -1;
            const auto r = add (Op::logicalNot, level60());
            --depth;
            return r;
        }
        return level50();
    }

    int level50()
    {
        auto left = level40();
        while (! failed)
        {
            Op op;
            if (takeSymbol ("<>")) op = Op::notEqual;
            else if (takeSymbol ("<=")) op = Op::lessEqual;
            else if (takeSymbol (">=")) op = Op::greaterEqual;
            else if (takeSymbol ("<")) op = Op::less;
            else if (takeSymbol (">")) op = Op::greater;
            else if (takeSymbol ("=")) op = Op::equal;
            else break;
            left = add (op, left, level40());
        }
        return left;
    }

    int level40()
    {
        auto left = level30();
        while (! failed)
        {
            Op op;
            if (takeSymbol ("+")) op = Op::add;
            else if (takeSymbol ("-")) op = Op::sub;
            else break;
            left = add (op, left, level30());
        }
        return left;
    }

    int level30()
    {
        auto left = level20();
        while (! failed)
        {
            Op op;
            if (takeSymbol ("*")) op = Op::mul;
            else if (takeSymbol ("/")) op = Op::div;
            else if (takeWord ("div")) op = Op::intDiv;
            else if (takeWord ("mod")) op = Op::mod;
            else if (takeWord ("fmod")) op = Op::fmod;
            else break;
            left = add (op, left, level20());
        }
        return left;
    }

    int level20()
    {
        auto left = level10();
        while (! failed && takeSymbol ("^"))
            left = add (Op::power, left, level10());
        return left;
    }

    int level10()
    {
        if (! enter())
            return -1;
        int result;
        if (takeSymbol ("-"))
            result = add (Op::neg, level10());
        else if (takeSymbol ("+"))
            result = level10();
        else
        {
            result = primary();
            while (! failed)
            {
                if (takeSymbol ("!"))
                    result = add (Op::factorial, result);
                else if (takeSymbol ("%"))
                    result = add (Op::percent, result);
                else if (takeWord ("in"))
                    result = add (Op::equal, result, primary());
                else
                    break;
            }
        }
        --depth;
        return result;
    }

    int primary()
    {
        if (failed)
            return -1;
        if (pos >= tokens.size())
        {
            fail ("Syntax error: function or operand has too few arguments", text.size());
            return -1;
        }
        const auto& t = tokens[pos];
        if (t.kind == Token::number)
        {
            ++pos;
            const auto n = add (Op::number);
            nodes[(std::size_t) n].value = t.value;
            return n;
        }
        if (isSymbol (t, "("))
        {
            ++pos;
            const auto inner = level70();
            if (! failed && ! takeSymbol (")"))
                fail (pos < tokens.size() ? "Syntax error" : "Syntax error: function or operand has too few arguments", hereAt());
            return inner;
        }
        if (t.kind == Token::name)
        {
            if (t.lower == "input")
            {
                ++pos;
                return add (Op::input);
            }
            if (t.lower == "pi")
            {
                ++pos;
                const auto n = add (Op::number);
                nodes[(std::size_t) n].value = pi;
                return n;
            }
            if (const auto* f = findFunction (t.lower))
            {
                ++pos;
                return call (*f, t.at);
            }
        }
        // an operator where an operand belongs ("Input*", "*2", "(,)")
        fail ("Syntax error: function or operand has too few arguments", t.at);
        return -1;
    }

    int call (const Function& f, std::size_t nameAt)
    {
        int args[3] { -1, -1, -1 };
        int count = 0;
        if (takeSymbol ("("))
        {
            if (! takeSymbol (")"))
            {
                while (! failed)
                {
                    const auto a = level70();
                    if (count < 3)
                        args[count] = a;
                    ++count;
                    if (takeSymbol (","))
                        continue;
                    if (! takeSymbol (")"))
                        fail ("Syntax error", hereAt());
                    break;
                }
            }
        }
        if (failed)
            return -1;
        if (count < f.arity)
        {
            fail ("Syntax error: function or operand has too few arguments", nameAt);
            return -1;
        }
        if (count > f.arity)
        {
            fail ("Too many arguments", nameAt);
            return -1;
        }
        const auto n = add (Op::call);
        auto& node = nodes[(std::size_t) n];
        node.function = &f;
        for (int i = 0; i < 3; ++i)
            node.args[i] = args[i];
        return n;
    }

    const std::string& text;
    std::vector<Node>& nodes;
    std::vector<Token> tokens;
    std::size_t pos = 0;
    int depth = 0;
};
} // namespace

bool isDefaultMapping (const std::string& text)
{
    const auto t = lower (trimmed (text));
    return t.empty() || t == "input";
}

Formula Formula::compile (const std::string& text, std::string* error, int* errorAt)
{
    Formula f;
    if (error != nullptr)
        error->clear();
    if (isDefaultMapping (text))
    {
        if (errorAt != nullptr)
            *errorAt = -2;
        return f;
    }
    auto program = std::make_shared<Program>();
    Parser parser (text, program->nodes);
    program->root = parser.parse();
    if (parser.failed || program->root < 0)
    {
        if (error != nullptr)
            *error = parser.message;
        if (errorAt != nullptr)
            *errorAt = std::max (0, parser.errorAt);
        return f;
    }
    if (errorAt != nullptr)
        *errorAt = -1;
    f.program = std::move (program);
    return f;
}

double Formula::evaluate (double input) const
{
    return program == nullptr ? input : program->run (program->root, input);
}

float Formula::map (float input) const
{
    const auto v = evaluate ((double) input);
    if (std::isnan (v))
        return 0.0f;
    return (float) std::clamp (v, 0.0, 1.0);
}

double tension (double x, double t)
{
    if (t == 0.0)
        return x;
    if (t < 0.0)
    {
        const auto amount = -t;
        return (std::exp (x * std::log (amount + 1.0)) - 1.0) / amount;
    }
    return (t - (std::exp ((1.0 - x) * std::log (t + 1.0)) - 1.0)) / t;
}

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list {
        { "Linear (absolute)", nullptr },
        { "Default", "Input" },
        { "Inverted", "1-Input" },
        { "Log scale", "Tension(Input,10)", true },
        { "Inverted log scale", "Tension(Input,-10)" },
        { "Switch", "Round(Input)", true },
        { "Steppy", "Int(Input*9)/8" },
        { "Steppy (cents to semitones)", "Int(Input*25)/24" },
        { "First half", "Input*0.5", true },
        { "Last half", "Input*0.5+0.5" },
        { "Center dead zone (small)", "Min(Input*1.1,0.5)+Max(Input*1.1,0.6)-0.6", true },
        { "Center dead zone (large)", "Min(Input*1.3,0.5)+Max(Input*1.3,0.8)-0.8" },
        { "MIDI CC centered around 63", "Input*1.00793650794", true },
        { "MIDI CC centered around 64", "Input*0.9921875" },
        { "Incremental (relative)", nullptr },
        { "Default increment", "Input" },
        { "Inverted increment", "-Input" },
        { "4x smaller increment", "Input*0.25", true },
        { "2x smaller increment", "Input*0.5" },
        { "2x bigger increment", "Input*2" },
        { "4x bigger increment", "Input*4" },
        { "1/50 increment", "0.02", true },
        { "1/20 increment", "0.05" },
        { "1/10 increment", "0.1" },
        { "Force boolean", "Ceil(Input)", true },
        { "Special", nullptr },
        { "Launchpad XY - X", "(Frac(Input*7.875+0.001)-0.001)*1.142857142857143" },
        { "Launchpad XY - Y", "Int(Input*8)*0.14285714285714285" },
    };
    return list;
}

double smoothingMs (int position)
{
    const auto p = std::clamp (std::abs (position), 0, smoothingMax);
    return (std::exp ((double) p / 1024.0 * std::log (11.0)) - 1.0) * 100.0;
}

int smoothingPoles (int position)
{
    return position > 0 ? 3 : 1;
}

float Smoother::step (float target, double elapsedMs, int position)
{
    const auto poles = smoothingPoles (position);
    const auto time = smoothingMs (position);
    if (! started || time <= 0.0)
    {
        for (auto& s : stage)
            s = target;
        started = true;
        return target;
    }
    const auto k = 1.0 - std::exp (-std::max (0.0, elapsedMs) * (double) poles / time);
    double in = target;
    for (int i = 0; i < poles; ++i)
    {
        stage[i] += (in - stage[i]) * k;
        in = stage[i];
    }
    for (int i = poles; i < 3; ++i)
        stage[i] = in;
    return (float) in;
}

bool Smoother::settled (float target) const noexcept
{
    return started && std::fabs (stage[2] - (double) target) < 0.0005;
}
std::optional<float> linkModeValue (int mode, const Formula& formula, float input, float current)
{
    if (linkModeIsAbsolute (mode))
        return formula.map (input);
    if (! (input > 0.0f)) // (a release, or a button's "off")
        return std::nullopt;
    const auto clamp01 = [] (double v) { return std::isnan (v) ? 0.0f : (float) std::clamp (v, 0.0, 1.0); };
    switch ((LinkMode) mode)
    {
        case LinkMode::latch:
        {
            const auto off = clamp01 (formula.evaluate (0.0)), on = clamp01 (formula.evaluate (1.0));
            // (FL keeps f(1) when both are as far)
            return std::fabs (current - on) < std::fabs (current - off) ? off : on;
        }
        case LinkMode::inc:
        {
            const auto step = formula.evaluate (incrementResolution);
            if (std::isnan (step))
                return std::nullopt;
            return clamp01 ((double) current + step);
        }
        case LinkMode::stay: return formula.map (input);
        default: return formula.map (input);
    }
}
} // namespace lynx::linkformula
