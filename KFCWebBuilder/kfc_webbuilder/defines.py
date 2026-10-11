#
# Author: sascha_lammers@gmx.de
#
# Preprocessor defines for the expressions: CPPDEFINES of the environment (cppdefines.txt written by
# scripts/extra_script.py, or the -D flags of platformio.ini), the #defines of the global header and
# OS_WIN/OS_OSX/OS_UNIX.
#

import glob
import os
import re
import sys

from . import BuilderError
from .expr import Evaluator


PHP_TRIM = ' \t\n\r\0\x0b'


def php_trim(value):
    return value.strip(PHP_TRIM)


def _read_lines(filename):
    # like PHP file(FILE_IGNORE_NEW_LINES): split at \n, a \r before the \n is removed
    with open(filename, 'rb') as f:
        data = f.read().decode('utf-8', errors='surrogateescape')
    lines = data.split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    return [line[:-1] if line.endswith('\r') else line for line in lines]


class Defines(object):

    def __init__(self):
        self.defines = {}
        self.environment = None
        self.environments = []
        self.sections = {}
        self.parse_defines = True
        self.evaluator = Evaluator(self.defines)

    # -----------------------------------------------------------------------------------------------------

    def evaluate(self, expr):
        return self.evaluator.evaluate(expr)

    def is_defined(self, name):
        return self.evaluator.is_defined(name)

    def set_environment(self, env):
        self.environment = env

    def read_defines(self, filename):
        if not os.path.exists(filename):
            raise BuilderError('Cannot read %s' % filename)
        self.defines.clear()
        for line in _read_lines(filename):
            key, sep, value = line.partition('=')
            self.defines[key] = value if sep else None
        self.parse_defines = False

    def dump(self, out=None):
        out = out or sys.stdout
        for name, value in self.defines.items():
            out.write('%s=%s\n' % (name, '' if value is None else value))

    def define_os(self):
        for name in ('OS_WIN', 'OS_OSX', 'OS_UNIX'):
            if self.is_defined(name):
                raise BuilderError('%s already defined' % name)
            self.defines[name] = '0'
        if sys.platform.startswith('win'):
            self.defines['OS_WIN'] = '1'
        elif sys.platform == 'darwin':
            self.defines['OS_OSX'] = '1'
        else:
            self.defines['OS_UNIX'] = '1'

    # -----------------------------------------------------------------------------------------------------
    # platformio.ini

    @staticmethod
    def _strip_ini_comment(line):
        return re.sub(r'(".*?")|[#;].*$', lambda m: m.group(1) or '', line)

    def _resolve_ini_variables(self, line):
        var_re = re.compile(r'\$\{(?:([a-zA-Z0-9_:-]+)\.)([a-zA-Z0-9_-]+)\}')
        while True:
            m = var_re.search(line)
            if not m:
                return line
            section, key = m.group(1), m.group(2)
            value = self.sections.get(section, {}).get(key)
            if value is None and section == 'sysenv':
                value = os.environ.get(key)
            if value is None:
                raise BuilderError("Variable '%s' does not exist" % m.group(0))
            line = line.replace(m.group(0), value)

    def _merge_section(self, environment, source, parse_build_flags):
        selected = self.sections.setdefault(environment, {})
        for key, value in self.sections.get(source, {}).items():
            selected.setdefault(key, value)
        if parse_build_flags and self.parse_defines and 'build_flags' in selected:
            tokens = self._resolve_ini_variables(selected['build_flags']).split()
            for i, token in enumerate(tokens):
                if token == '-D' and i + 1 < len(tokens):
                    name, sep, value = tokens[i + 1].partition('=')
                    self.defines[name] = value if sep else None

    def _process_ini_line(self, environment, keyword, line, lines, base_dir):
        if environment is None or keyword is None:
            return
        if keyword == 'extra_configs':
            for name in line.split():
                if name.startswith('/'):
                    continue
                filename = base_dir + '/' + name
                files = sorted(glob.glob(filename)) if '*' in filename else [filename]
                for filename in files:
                    if not os.path.exists(filename):
                        raise BuilderError("Cannot load extra config '%s'" % filename)
                    lines.extend(_read_lines(filename))
        elif keyword == 'extends':
            name = line.strip()
            if name not in self.sections:
                raise BuilderError("extends missing environment '%s'" % name)
            self._merge_section(environment, name, False)
        self.sections.setdefault(environment, {})[keyword] = line

    def read_platformio_ini(self, filename):
        if not os.path.exists(filename):
            raise BuilderError('Cannot read %s' % filename)
        base_dir = os.path.dirname(os.path.abspath(filename))
        lines = _read_lines(filename)
        environment = keyword = None
        full_line = ''
        found = False
        num = 0
        while num < len(lines):
            line = lines[num]
            num += 1
            if line.lstrip(PHP_TRIM)[:1] in ('#', ';'):
                continue
            line = self._strip_ini_comment(line)
            m = re.match(r'^\s*\[([^\]]+)\]\s*$', line)
            if m:
                self._process_ini_line(environment, keyword, full_line, lines, base_dir)
                keyword = None
                full_line = ''
                environment = m.group(1)
                self.environments.append(environment)
                if environment == self.environment:
                    found = True
                continue
            m = re.match(r'^([a-zA-Z0-9_.-]+)\s*=\s*(.*)', line)
            if m:
                self._process_ini_line(environment, keyword, full_line, lines, base_dir)
                keyword, full_line = m.group(1), m.group(2)
            elif keyword is not None:
                if re.match(r'^\s', line):
                    full_line += line
                else:
                    self._process_ini_line(environment, keyword, full_line, lines, base_dir)
                    keyword = None
                    full_line = ''
            elif line.strip():
                print("WARNING! '%s' skipped" % line.strip())
        self._process_ini_line(environment, keyword, full_line, lines, base_dir)

        if self.environment is not None:
            if not found:
                raise BuilderError('Environment %s not found in %s' % (self.environment, filename))
            self._merge_section(self.environment, 'env', True)

    # -----------------------------------------------------------------------------------------------------
    # global header
    #
    # Same line based reader as the PHP builder, its quirks are kept so that the same defines result: the
    # text from the last "//" on (and the character before it) is removed, #elif opens a new level

    def read_header(self, filename):
        if not os.path.exists(filename):
            raise BuilderError('Cannot read %s' % filename)
        active = [True]
        level = 0
        basename = os.path.basename(filename)
        for line_no, line in enumerate(_read_lines(filename), 1):
            line = php_trim(line)
            idx = line.rfind('//')
            if idx != -1:
                line = line[:idx - 1] if idx > 0 else line[:-1]
            line = php_trim(line)

            def push(value):
                nonlocal level
                level += 1
                if len(active) <= level:
                    active.append(value)
                else:
                    active[level] = value

            m = re.match(r'^#\s*if\s+(.*)', line)
            if m:
                push(active[level] and self.evaluate(m.group(1)) if active[level] else False)
            elif re.match(r'^#\s*ifdef\s+(.*)', line):
                name = re.match(r'^#\s*ifdef\s+(.*)', line).group(1)
                push(self.is_defined(name) if active[level] else False)
            elif re.match(r'^#\s*ifndef\s+(.*)', line):
                name = re.match(r'^#\s*ifndef\s+(.*)', line).group(1)
                push((not self.is_defined(name)) if active[level] else False)
            elif re.match(r'^#\s*elif\s+(.*)', line):
                expr = re.match(r'^#\s*elif\s+(.*)', line).group(1)
                push(self.evaluate(expr) if active[level] else False)
            elif re.match(r'^#\s*else$', line):
                if level > 0 and active[level - 1]:
                    active[level] = not active[level]
            elif re.match(r'^#\s*endif$', line):
                active[level] = False
                level -= 1
                if level < 0:
                    raise BuilderError('%s:%d: #endif without #if' % (basename, line_no))
            if active[level]:
                m = re.match(r'^#\s*define\s+([^ ]+)\s+(.*)', line)
                if m:
                    if self.is_defined(m.group(1)):
                        print('WARNING: %s:%d: %s already defined' % (basename, line_no, m.group(1)))
                    else:
                        self.defines[m.group(1)] = php_trim(m.group(2))
                    continue
                m = re.match(r'^#\s*undef\s*(\S*)', line)
                if m:
                    if self.is_defined(m.group(1)):
                        del self.defines[m.group(1)]
                    else:
                        print('WARNING: %s:%d: %s is not defined' % (basename, line_no, m.group(1)))
