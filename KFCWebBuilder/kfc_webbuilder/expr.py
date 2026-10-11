#
# Author: sascha_lammers@gmx.de
#
# Preprocessor expressions (#if ...) with C semantics: "defined X" / "defined(X)", macros are expanded
# recursively, an undefined identifier is 0, && || ?: short-circuit. The PHP builder evaluated the expression
# with eval() and replaced undefined names with str_replace(), which also replaced a name inside a longer one
# (IOT_WEATHER_STATION inside IOT_WEATHER_STATION2): the tokenizer only replaces whole identifiers.
#
# Values follow the PHP rules of the old builder where C has none: string literals are strings, "0" and ""
# are false, == compares a string and a number numerically.
#

import re

from . import BuilderError

_TOKEN_RE = re.compile(r'''
    (?P<ws>\s+)
  | (?P<number>0[xX][0-9a-fA-F]+|0[bB][01]+|\d+(?:\.\d+)?)(?P<suffix>[uUlL]*)
  | (?P<ident>[A-Za-z_][A-Za-z0-9_]*)
  | (?P<string>"(?:[^"\\]|\\.)*")
  | (?P<char>'(?:[^'\\]|\\.)+')
  | (?P<op>\|\||&&|==|!=|<=|>=|<<|>>|[-+*/%<>!~&|^?:(),])
''', re.X | re.S)

# same comment handling as the PHP builder: strings are kept, /* */ and // (not after ":") are removed
_COMMENT_RE = re.compile(r'(".*?")|/\*[\s\S]*?\*/|([^:]|^)//.*$', re.M)

_ESCAPES = {'n': '\n', 't': '\t', 'r': '\r', '0': '\0', '\\': '\\', '"': '"', "'": "'"}


class ExpressionError(BuilderError):
    pass


def strip_comments(expr):
    def repl(m):
        if m.group(0).startswith('"'):
            return m.group(0)
        return m.group(2) or ''
    return _COMMENT_RE.sub(repl, expr)


def tokenize(text):
    tokens = []
    pos = 0
    while pos < len(text):
        m = _TOKEN_RE.match(text, pos)
        if not m:
            raise ExpressionError('invalid character %r in %r' % (text[pos], text))
        pos = m.end()
        if m.group('ws'):
            continue
        if m.group('number') is not None:
            tokens.append(('number', m.group('number')))
        elif m.group('ident') is not None:
            tokens.append(('ident', m.group('ident')))
        elif m.group('string') is not None:
            tokens.append(('string', m.group('string')))
        elif m.group('char') is not None:
            tokens.append(('char', m.group('char')))
        else:
            tokens.append(('op', m.group('op')))
    return tokens


def _unescape(body):
    return re.sub(r'\\(.)', lambda m: _ESCAPES.get(m.group(1), m.group(1)), body)


def _number(text):
    lower = text.lower()
    if lower.startswith('0x'):
        return int(lower, 16)
    if lower.startswith('0b'):
        return int(lower[2:], 2)
    if '.' in lower:
        return float(lower)
    if len(lower) > 1 and lower.startswith('0'):
        return int(lower, 8)
    return int(lower)


# ---------------------------------------------------------------------------------------------------------
# PHP value semantics


def to_bool(value):
    if isinstance(value, str):
        return value not in ('', '0')
    return bool(value)


def to_number(value):
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, (int, float)):
        return value
    m = re.match(r'\s*[-+]?(\d+(\.\d*)?|\.\d+)([eE][-+]?\d+)?', value)
    if not m:
        return 0
    num = float(m.group(0)) if (m.group(2) or m.group(3) or m.group(0).strip().startswith('.')) else int(m.group(0))
    return num


def _is_numeric_string(value):
    return isinstance(value, str) and re.fullmatch(r'\s*[-+]?(\d+(\.\d*)?|\.\d+)([eE][-+]?\d+)?\s*', value) is not None


def _compare(a, b):
    # PHP 7 loose comparison, returns -1, 0, 1
    if isinstance(a, bool) or isinstance(b, bool) or a is None or b is None:
        a, b = to_bool(a), to_bool(b)
    elif isinstance(a, str) and isinstance(b, str):
        if not (_is_numeric_string(a) and _is_numeric_string(b)):
            return (a > b) - (a < b)
        a, b = to_number(a), to_number(b)
    else:
        a, b = to_number(a), to_number(b)
    return (a > b) - (a < b)


def _int(value):
    return int(to_number(value))


def _c_div(a, b):
    if b == 0:
        raise ExpressionError('division by zero')
    if isinstance(a, int) and isinstance(b, int):
        if a % b == 0:
            return a // b
    return a / b


def _c_mod(a, b):
    a, b = _int(a), _int(b)
    if b == 0:
        raise ExpressionError('modulo by zero')
    result = abs(a) % abs(b)
    return -result if a < 0 else result


_BINARY = {
    '*': (10, lambda a, b: to_number(a) * to_number(b)),
    '/': (10, lambda a, b: _c_div(to_number(a), to_number(b))),
    '%': (10, _c_mod),
    '+': (9, lambda a, b: to_number(a) + to_number(b)),
    '-': (9, lambda a, b: to_number(a) - to_number(b)),
    '<<': (8, lambda a, b: _int(a) << _int(b)),
    '>>': (8, lambda a, b: _int(a) >> _int(b)),
    '<': (7, lambda a, b: _compare(a, b) < 0),
    '<=': (7, lambda a, b: _compare(a, b) <= 0),
    '>': (7, lambda a, b: _compare(a, b) > 0),
    '>=': (7, lambda a, b: _compare(a, b) >= 0),
    '==': (6, lambda a, b: _compare(a, b) == 0),
    '!=': (6, lambda a, b: _compare(a, b) != 0),
    '&': (5, lambda a, b: _int(a) & _int(b)),
    '^': (4, lambda a, b: _int(a) ^ _int(b)),
    '|': (3, lambda a, b: _int(a) | _int(b)),
    '&&': (2, None),
    '||': (1, None),
}


# ---------------------------------------------------------------------------------------------------------
# parser


class _Parser(object):

    def __init__(self, tokens, source):
        self.tokens = tokens
        self.pos = 0
        self.source = source

    def peek(self):
        return self.tokens[self.pos] if self.pos < len(self.tokens) else (None, None)

    def take(self, value=None):
        token = self.peek()
        if token[0] is None or (value is not None and token[1] != value):
            raise ExpressionError('expected %r in %r' % (value or 'operand', self.source))
        self.pos += 1
        return token

    def parse(self):
        node = self.ternary()
        if self.pos != len(self.tokens):
            raise ExpressionError('unexpected %r in %r' % (self.peek()[1], self.source))
        return node

    def ternary(self):
        cond = self.binary(1)
        if self.peek() == ('op', '?'):
            self.take('?')
            a = self.ternary()
            self.take(':')
            b = self.ternary()
            return ('?', cond, a, b)
        return cond

    def binary(self, min_prec):
        left = self.unary()
        while True:
            kind, value = self.peek()
            if kind != 'op' or value not in _BINARY or _BINARY[value][0] < min_prec:
                return left
            prec = _BINARY[value][0]
            self.take()
            right = self.binary(prec + 1)
            left = ('bin', value, left, right)

    def unary(self):
        kind, value = self.peek()
        if kind == 'op' and value in ('!', '~', '-', '+'):
            self.take()
            return ('unary', value, self.unary())
        return self.primary()

    def primary(self):
        kind, value = self.take()
        if kind == 'op' and value == '(':
            node = self.ternary()
            self.take(')')
            return node
        if kind == 'number':
            return ('value', _number(value))
        if kind == 'string':
            return ('value', _unescape(value[1:-1]))
        if kind == 'char':
            return ('value', ord(_unescape(value[1:-1])[0]))
        if kind == 'ident':
            if self.peek() == ('op', '('):
                # function-like macro or function call, an error if it is evaluated
                depth = 0
                while True:
                    k, v = self.take()
                    if v == '(':
                        depth += 1
                    elif v == ')':
                        depth -= 1
                        if depth == 0:
                            break
                return ('call', value)
            return ('value', {'true': True, 'false': False, 'null': None}.get(value.lower(), 0))
        raise ExpressionError('unexpected %r in %r' % (value, self.source))


def _evaluate(node):
    kind = node[0]
    if kind == 'value':
        return node[1]
    if kind == 'unary':
        op, value = node[1], _evaluate(node[2])
        if op == '!':
            return not to_bool(value)
        if op == '~':
            return ~_int(value)
        if op == '-':
            return -to_number(value)
        return to_number(value)
    if kind == 'bin':
        op = node[1]
        if op == '&&':
            return to_bool(_evaluate(node[2])) and to_bool(_evaluate(node[3]))
        if op == '||':
            return to_bool(_evaluate(node[2])) or to_bool(_evaluate(node[3]))
        return _BINARY[op][1](_evaluate(node[2]), _evaluate(node[3]))
    if kind == '?':
        return _evaluate(node[2]) if to_bool(_evaluate(node[1])) else _evaluate(node[3])
    if kind == 'call':
        raise ExpressionError('cannot evaluate the function call %s()' % node[1])
    raise ExpressionError('invalid node %r' % (node,))


# ---------------------------------------------------------------------------------------------------------
# macro expansion


class Evaluator(object):

    def __init__(self, defines):
        # defines: dict name -> value (str) or None (listed, but not defined)
        self.defines = defines
        self._cache = {}

    def is_defined(self, name):
        return self.defines.get(name) is not None

    def _expand(self, tokens, hidden):
        result = []
        i = 0
        while i < len(tokens):
            kind, value = tokens[i]
            if kind == 'ident' and value == 'defined':
                # defined X, defined(X)
                j = i + 1
                paren = j < len(tokens) and tokens[j] == ('op', '(')
                if paren:
                    j += 1
                if j >= len(tokens) or tokens[j][0] != 'ident':
                    raise ExpressionError('defined without a name')
                name = tokens[j][1]
                j += 1
                if paren:
                    if j >= len(tokens) or tokens[j] != ('op', ')'):
                        raise ExpressionError('defined(%s without ")"' % name)
                    j += 1
                result.append(('number', '1' if self.is_defined(name) else '0'))
                i = j
                continue
            if kind == 'ident' and value not in hidden and self.is_defined(value):
                result.extend(self._expand(tokenize(strip_comments(self.defines[value])), hidden | {value}))
            elif kind == 'ident' and value.lower() not in ('true', 'false', 'null') and \
                    not (i + 1 < len(tokens) and tokens[i + 1] == ('op', '(')):
                result.append(('number', '0'))
            else:
                result.append((kind, value))
            i += 1
        return result

    def evaluate(self, expr):
        expr = expr.strip()
        tokens = self._expand(tokenize(strip_comments(expr)), frozenset())
        if not tokens:
            raise ExpressionError('empty expression')
        return to_bool(_evaluate(_Parser(tokens, expr).parse()))
