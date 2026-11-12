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

enum class Op : std::uint8_t
{
    number, input, call,
    add, sub, mul, div, intDiv, mod, power, neg, logicalNot,
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
            case Op::power: return std::pow (a(), b());
            case Op::neg: return -a();
            case Op::logicalNot: return truth (a() == 0.0);
            case Op::logicalAnd: return truth (a() != 0.0 && b() != 0.0);
            case Op::logicalOr: return truth (a() != 0.0 || b() != 0.0);
            case Op::logicalXor: return truth ((a() != 0.0) != (b() != 0.0));
            case Op::shl: return (double) (whole (a()) << std::clamp<std::int64_t> (whole (b()), 0, 62));
            case Op::shr: return (double) (whole (a()) >> std::clamp<std::int64_t> (whole (b()), 0, 62));
            case Op::equal: return truth (a() == b());
            case Op::notEqual: return truth (a() != b());
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
// Pascal-style precedence: comparisons, then + - or xor, then * / div mod and shl shr, then unary, then ^.
class Parser
{
public:
    Parser (const std::string& t, std::vector<Node>& n) : text (t), nodes (n) {}

    int parse()
    {
        const auto root = comparison();
        skipSpace();
        if (! failed && at < text.size())
            fail ("Unexpected '" + std::string (1, text[at]) + "'");
        return failed ? -1 : root;
    }

    bool failed = false;
    std::string message;
    int errorAt = 0;

private:
    void fail (const std::string& what)
    {
        if (failed)
            return;
        failed = true;
        message = what;
        errorAt = (int) std::min (at, text.size());
    }

    void skipSpace()
    {
        while (at < text.size() && std::isspace ((unsigned char) text[at]))
            ++at;
    }

    bool symbol (const char* s)
    {
        skipSpace();
        const auto n = std::strlen (s);
        if (text.compare (at, n, s) != 0)
            return false;
        at += n;
        return true;
    }

    // a word operator (div, mod, and...): matched whole, any letter case
    bool word (const char* w)
    {
        skipSpace();
        const auto n = std::strlen (w);
        if (at + n > text.size())
            return false;
        for (std::size_t i = 0; i < n; ++i)
            if (std::tolower ((unsigned char) text[at + i]) != w[i])
                return false;
        if (at + n < text.size() && (std::isalnum ((unsigned char) text[at + n]) || text[at + n] == '_'))
            return false;
        at += n;
        return true;
    }

    int add (Op op, int a = -1, int b = -1)
    {
        Node n;
        n.op = op;
        n.args[0] = a;
        n.args[1] = b;
        nodes.push_back (n);
        return (int) nodes.size() - 1;
    }

    int comparison()
    {
        auto left = additive();
        while (! failed)
        {
            Op op;
            if (symbol ("<>")) op = Op::notEqual;
            else if (symbol ("<=")) op = Op::lessEqual;
            else if (symbol (">=")) op = Op::greaterEqual;
            else if (symbol ("<")) op = Op::less;
            else if (symbol (">")) op = Op::greater;
            else if (symbol ("=")) op = Op::equal;
            else break;
            left = add (op, left, additive());
        }
        return left;
    }

    int additive()
    {
        auto left = multiplicative();
        while (! failed)
        {
            Op op;
            if (symbol ("+")) op = Op::add;
            else if (symbol ("-")) op = Op::sub;
            else if (word ("or")) op = Op::logicalOr;
            else if (word ("xor")) op = Op::logicalXor;
            else break;
            left = add (op, left, multiplicative());
        }
        return left;
    }

    int multiplicative()
    {
        auto left = unary();
        while (! failed)
        {
            Op op;
            if (symbol ("*")) op = Op::mul;
            else if (symbol ("/")) op = Op::div;
            else if (word ("div")) op = Op::intDiv;
            else if (word ("mod")) op = Op::mod;
            else if (word ("and")) op = Op::logicalAnd;
            else if (word ("shl")) op = Op::shl;
            else if (word ("shr")) op = Op::shr;
            else break;
            left = add (op, left, unary());
        }
        return left;
    }

    int unary()
    {
        if (++depth > 200)
        {
            fail ("Formula too deep");
            return -1;
        }
        int result;
        if (symbol ("-"))
            result = add (Op::neg, unary());
        else if (symbol ("+"))
            result = unary();
        else if (word ("not"))
            result = add (Op::logicalNot, unary());
        else
        {
            result = primary();
            if (! failed && symbol ("^"))
                result = add (Op::power, result, unary());
        }
        --depth;
        return result;
    }

    int primary()
    {
        skipSpace();
        if (at >= text.size())
        {
            fail ("Unexpected end of formula");
            return -1;
        }
        const auto c = text[at];
        if (c == '(')
        {
            ++at;
            const auto inner = comparison();
            if (! failed && ! symbol (")"))
                fail ("Missing ')'");
            return inner;
        }
        if (std::isdigit ((unsigned char) c) || c == '.' || c == '$')
            return number();
        if (std::isalpha ((unsigned char) c) || c == '_')
        {
            const auto start = at;
            while (at < text.size() && (std::isalnum ((unsigned char) text[at]) || text[at] == '_'))
                ++at;
            const auto name = lower (text.substr (start, at - start));
            if (name == "input")
                return add (Op::input);
            if (name == "pi")
            {
                const auto n = add (Op::number);
                nodes[(std::size_t) n].value = pi;
                return n;
            }
            for (const auto& f : functions)
                if (name == f.name)
                    return call (f, start);
            at = start;
            fail ("Unknown name '" + text.substr (start, name.size()) + "'");
            return -1;
        }
        fail ("Unexpected '" + std::string (1, c) + "'");
        return -1;
    }

    int call (const Function& f, std::size_t nameStart)
    {
        int args[3] { -1, -1, -1 };
        int count = 0;
        if (symbol ("("))
        {
            if (! symbol (")"))
            {
                while (! failed)
                {
                    const auto a = comparison();
                    if (count < 3)
                        args[count] = a;
                    ++count;
                    if (symbol (","))
                        continue;
                    if (! symbol (")"))
                        fail ("Missing ')'");
                    break;
                }
            }
        }
        if (failed)
            return -1;
        if (count != f.arity)
        {
            at = nameStart;
            fail (std::string (f.name) + ": " + std::to_string (f.arity) + (f.arity == 1 ? " value expected" : " values expected"));
            return -1;
        }
        const auto n = add (Op::call);
        auto& node = nodes[(std::size_t) n];
        node.function = &f;
        for (int i = 0; i < 3; ++i)
            node.args[i] = args[i];
        return n;
    }

    int number()
    {
        const auto start = at;
        double value = 0.0;
        if (text[at] == '$') // hexadecimal
        {
            ++at;
            const auto digits = at;
            while (at < text.size() && std::isxdigit ((unsigned char) text[at]))
                ++at;
            if (at == digits)
            {
                fail ("Number expected");
                return -1;
            }
            value = (double) std::strtoull (text.substr (digits, at - digits).c_str(), nullptr, 16);
        }
        else
        {
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
                at = start;
                fail ("Number expected");
                return -1;
            }
            value = std::strtod (token.c_str(), nullptr); // (the C locale: '.' is the decimal point)
        }
        const auto n = add (Op::number);
        nodes[(std::size_t) n].value = value;
        return n;
    }

    const std::string& text;
    std::vector<Node>& nodes;
    std::size_t at = 0;
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
} // namespace lynx::linkformula
