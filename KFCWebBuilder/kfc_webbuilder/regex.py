#
# Author: sascha_lammers@gmx.de
#
# Regular expressions in the PHP/PCRE notation of the configuration: "/pattern/flags". A "!" in the flags
# negates the result, e.g. '/%[A-Z0-9_]{1,33}%/!' matches if the content does NOT contain a %VARIABLE%.
#

import re

from . import BuilderError

_FLAGS = {'i': re.I, 'm': re.M, 's': re.S, 'x': re.X, 'u': 0}


class PhpRegex(object):

    def __init__(self, pattern, allow_not=True):
        if not isinstance(pattern, str) or len(pattern) < 2:
            raise BuilderError('Invalid regular expression: %r' % (pattern,))
        self.source = pattern
        delimiter = pattern[0]
        end = {'(': ')', '[': ']', '{': '}', '<': '>'}.get(delimiter, delimiter)
        pos = pattern.rfind(end)
        if pos <= 0:
            raise BuilderError('Invalid regular expression, missing delimiter %s: %s' % (end, pattern))
        body, flags = pattern[1:pos], pattern[pos + 1:]
        self.negate = False
        if '!' in flags and allow_not:
            self.negate = True
            flags = flags.replace('!', '')
        re_flags = 0
        for flag in flags:
            if flag == 'D':
                body = re.sub(r'(?<!\\)\$$', r'\\Z', body)
            elif flag in _FLAGS:
                re_flags |= _FLAGS[flag]
            else:
                raise BuilderError('Unsupported regular expression flag %s: %s' % (flag, pattern))
        if delimiter != '\\':
            body = body.replace('\\' + delimiter, delimiter) if delimiter not in '.^$*+?()[]{}|' else body
        try:
            self.regex = re.compile(body, re_flags)
        except re.error as e:
            raise BuilderError('Invalid regular expression: %s: %s' % (e, pattern))

    def search(self, subject):
        return self.regex.search(subject) is not None

    def is_match(self, subject):
        result = self.search(subject)
        return not result if self.negate else result

    def __repr__(self):
        return self.source
