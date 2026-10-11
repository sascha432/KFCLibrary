#
# Author: sascha_lammers@gmx.de
#
# HTML template preprocessor (internal command create-html-template)
#
# - <!--#if EXPR--> / <!--#elif EXPR--> / <!--#else--> / <!--#endif--> at the start of a line, or the same
#   with /*-- --*/ for <script> and <style> blocks. EXPR cannot contain a "-". A level is not combined with
#   the outer one: a nested #if is evaluated even if the outer block is inactive.
# - <!--%%NAME-->...<!--NAME%%--> is replaced with the preprocessed file .<name>.html of the same directory
# - %ASSETS_JS%, %ASSETS_CSS%: the hashed names of the combined JavaScript and CSS files
#

import os
import re

from . import BuilderError
from .defines import _read_lines

_START = r'(?:/\*|<!)'
_END = r'(?:\*/|>)'
_IF_RE = re.compile(r'^\s*' + _START + r'--\s*#if\s+([^-]+)--' + _END, re.A)
_ELIF_RE = re.compile(r'^\s*' + _START + r'--\s*#elif\s+([^-]+)--' + _END, re.A)
_ELSE_RE = re.compile(r'^\s*' + _START + r'--\s*#else\s*--' + _END, re.A)
_ENDIF_RE = re.compile(r'^\s*' + _START + r'--\s*#endif\s*--' + _END, re.A)
_INCLUDE_RE = re.compile(_START + r'--%%([^-]+)--' + _END + r'[\S\s]*' + _START + r'--\1%%--' + _END, re.M | re.A)


class HtmlTemplate(object):

    def __init__(self, defines):
        self.defines = defines
        self.source = None

    def _evaluate(self, expr, filename, line_no):
        try:
            return self.defines.evaluate(expr)
        except BuilderError as e:
            raise BuilderError('%s:%d: %s' % (filename, line_no, e))

    def preprocess(self, filename):
        try:
            lines = _read_lines(filename)
        except OSError:
            raise BuilderError('%s: Cannot read %s' % (self.source, filename))
        active = [True]
        level = 0
        content = []
        for line_no, line in enumerate(lines, 1):
            m = _IF_RE.match(line)
            if m:
                level += 1
                value = self._evaluate(m.group(1), filename, line_no)
                if len(active) <= level:
                    active.append(value)
                else:
                    active[level] = value
                continue
            m = _ELIF_RE.match(line)
            if m:
                if not active[level]:
                    active[level] = self._evaluate(m.group(1), filename, line_no)
                continue
            if _ELSE_RE.match(line):
                active[level] = not active[level]
                continue
            if _ENDIF_RE.match(line):
                active[level] = False
                level -= 1
                if level < 0:
                    raise BuilderError('%s:%d: #endif without #if' % (filename, line_no))
                continue
            if active[level]:
                content.append(line + '\n')
        return ''.join(content)

    def _include_files(self, content, directory):
        for m in list(_INCLUDE_RE.finditer(content)):
            include_file = os.path.join(directory, '.%s.html' % m.group(1).lower())
            if not os.path.exists(include_file):
                raise BuilderError('%s: Cannot include %s' % (self.source, include_file))
            content = content.replace(m.group(0), self.preprocess(include_file))
        return content

    @staticmethod
    def replace_assets(content, assets):
        # PHP strtr(): the longest key first, replaced text is not searched again
        if not assets:
            return content
        keys = sorted(assets, key=len, reverse=True)
        return re.sub('|'.join(re.escape(k) for k in keys), lambda m: assets[m.group(0)], content)

    def process(self, source_file, input_file, output_file, assets):
        self.source = source_file
        content = self.preprocess(input_file)
        content = self._include_files(content, os.path.dirname(source_file))
        content = self.replace_assets(content, assets)
        try:
            with open(output_file, 'wb') as f:
                f.write(content.encode('utf-8', errors='surrogateescape'))
        except OSError as e:
            raise BuilderError('%s: Cannot write %s: %s' % (source_file, output_file, e))
