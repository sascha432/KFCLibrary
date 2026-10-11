#
# Author: sascha_lammers@gmx.de
#
# YAML configuration of the web builder.
#
# - "includes" lists files whose sections bin/commands/configuration/web_builder are merged key by key (a key
#   of the including file wins), other missing top level keys are copied
# - a mapping whose keys are '#if <expr>' / '#ifdef' / '#ifndef' / '#elif' / '#else' / '#default' is replaced
#   by the selected value
# - web_builder.conditional_excludes: '#if <expr>': [files] excludes the files if the expression is true
# - ${section.key} refers to a value of a mapping without nested mappings (except web_builder), ${bin.<name>}
#   is the quoted path of an executable, ${env.NAME}, %NAME% (Windows), $NAME (others) and a leading ~ are
#   environment variables, ${path.current_config} is the directory of the file that contains the value
# - paths are relative to the file that contains them
#

import datetime
import os
import re
import tempfile

import yaml

from . import BuilderError
from .fileutils import find_executable, find_files, is_absolute, normalize, realpath
from .regex import PhpRegex

MERGED_SECTIONS = ('bin', 'commands', 'post_process', 'configuration', 'web_builder')

LOG_LEVELS = {'debug': 100, 'notice': 200, 'warning': 300, 'error': 400}

COMMAND_TYPES = ('command', 'internal')


class ConfigError(BuilderError):
    pass


# ---------------------------------------------------------------------------------------------------------
# YAML


class _UniqueKeyLoader(yaml.SafeLoader):
    pass


def _construct_mapping(loader, node, deep=False):
    loader.flatten_mapping(node)
    mapping = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in mapping:
            raise ConfigError('%s: duplicate key %r' % (key_node.start_mark, key))
        mapping[key] = loader.construct_object(value_node, deep=deep)
    return mapping


_UniqueKeyLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _construct_mapping)


def load_yaml(filename):
    try:
        with open(filename, 'rt', encoding='utf-8') as f:
            data = yaml.load(f, Loader=_UniqueKeyLoader)
    except OSError as e:
        raise ConfigError('Cannot read %s: %s' % (filename, e))
    except yaml.YAMLError as e:
        raise ConfigError('%s: %s' % (filename, e))
    if not isinstance(data, dict):
        raise ConfigError('%s: the top level must be a mapping' % filename)
    return data


# ---------------------------------------------------------------------------------------------------------
# helpers


def php_escapeshellarg(arg):
    if os.name == 'nt':
        return '"%s"' % re.sub(r'["%!]', ' ', arg)
    return "'%s'" % arg.replace("'", "'\\''")


def _walk_strings(value, callback, path=''):
    if isinstance(value, dict):
        return {k: _walk_strings(v, callback, '%s.%s' % (path, k) if path else str(k)) for k, v in value.items()}
    if isinstance(value, list):
        return [_walk_strings(v, callback, '%s[%d]' % (path, i)) for i, v in enumerate(value)]
    if isinstance(value, str):
        return callback(value, path)
    return value


_IF_RE = re.compile(r'^#\s*(if|ifdef|ifndef|elif|else|default)\b\s*(.*)$')


def _is_if_mapping(value):
    return isinstance(value, dict) and value and all(isinstance(k, str) and _IF_RE.match(k.strip()) for k in value)


class Config(object):

    def __init__(self, filename, defines, env_name=None, tools=None):
        self.filename = os.path.realpath(filename)
        self.config_dir = os.path.dirname(self.filename)
        self.defines = defines
        self.env_name = env_name or ''
        self.tools = tools or {}
        self.dependencies = [self.filename]
        self.includes = []
        self.temp_root = os.path.join(tempfile.gettempdir(), 'WebBuilderCli')
        now = datetime.datetime.now()
        self.static_vars = {
            '${pio.env}': self.env_name,
            '${path.main_config}': self.config_dir,
            '${path.sys_temp_dir}': tempfile.gettempdir(),
            '${path.shared_temp_dir}': self.temp_root,
            '${date.date}': now.strftime('%m/%d/%y'),
            '${date.time}': now.strftime('%H:%M:%S'),
            '${date.datetime}': now.strftime('%m/%d/%yT%H:%M:%S'),
            '${date.timestamp}': now.strftime('%Y%m%d%H%M%S'),
            '${date.unixtime}': str(int(now.timestamp())),
        }
        self.bins = {}
        self.data = None

    # -----------------------------------------------------------------------------------------------------
    # loading

    def _absolute(self, path, base_dir):
        if not isinstance(path, str):
            raise ConfigError('expected a path: %r' % (path,))
        if is_absolute(path) or re.match(r'^\$\{[^}]+\}|%[^%]+%', path):
            return path
        return normalize(path, base_dir)

    def _normalize_file(self, data, base_dir):
        # ${path.current_config} and the paths that are relative to the file that contains them
        def current_config(value, path):
            if '${path.current_config}' in value:
                return normalize(value.replace('${path.current_config}', base_dir))
            return value
        data = _walk_strings(data, current_config)
        for key in ('platformio_ini', 'global_defines_header'):
            if key in data:
                data[key] = self._absolute(data[key], base_dir)
        web_builder = data.get('web_builder') or {}
        if 'conditional_excludes' in web_builder:
            web_builder['conditional_excludes'] = _walk_strings(web_builder['conditional_excludes'],
                                                                lambda v, p: self._absolute(v, base_dir))
        for name, group in (web_builder.get('groups') or {}).items():
            if not isinstance(group, dict):
                raise ConfigError('web_builder.groups.%s must be a mapping' % name)
            target_dirs = {}
            for target_dir, sources in (group.get('target_dirs') or {}).items():
                if isinstance(sources, str):
                    sources = [sources]
                if isinstance(sources, list):
                    sources = [self._absolute(src, base_dir) for src in sources]
                elif isinstance(sources, dict):
                    sources = {self._absolute(src, base_dir): pattern for src, pattern in sources.items()}
                else:
                    raise ConfigError('web_builder.groups.%s.target_dirs.%s: expected a list or a mapping' %
                                      (name, target_dir))
                target_dirs[self._absolute(target_dir, base_dir)] = sources
            if 'target_dirs' in group:
                group['target_dirs'] = target_dirs
        return data

    def _load(self, filename, is_include):
        base_dir = os.path.dirname(filename)
        data = self._normalize_file(load_yaml(filename), base_dir)
        for include in data.pop('includes', None) or []:
            include = realpath(self._absolute(include, base_dir))
            self.includes.append(include)
            self.dependencies.append(include)
            data = self._merge_include(data, self._load(include, True))
        return data

    @staticmethod
    def _merge_include(data, include):
        for key, value in include.items():
            if key not in data:
                data[key] = value
            elif key in MERGED_SECTIONS and isinstance(data[key], dict) and isinstance(value, dict):
                section = data[key]
                for key2, value2 in value.items():
                    if key == 'configuration' and key2 == 'set_env':
                        merged = dict(value2 or {})
                        merged.update(section.get(key2) or {})
                        section[key2] = merged
                    elif key2 not in section:
                        section[key2] = value2
        return data

    # -----------------------------------------------------------------------------------------------------
    # conditions

    def _eval_if_mapping(self, items, path):
        results = []
        if_match = False
        default = None
        ifs = 0
        for key, value in items.items():
            m = _IF_RE.match(key.strip())
            directive, expr = m.group(1), m.group(2)
            if directive == 'if':
                ifs += 1
                if self.defines.evaluate(expr):
                    results.append(value)
                    if_match = True
            elif directive == 'ifdef':
                ifs += 1
                if self.defines.is_defined(expr.strip()):
                    results.append(value)
                    if_match = True
            elif directive == 'ifndef':
                ifs += 1
                if not self.defines.is_defined(expr.strip()):
                    results.append(value)
                    if_match = True
            elif directive == 'elif':
                if not if_match and self.defines.evaluate(expr):
                    results.append(value)
                    if_match = True
            elif directive == 'else':
                if not if_match:
                    results.append(value)
            elif directive == 'default':
                default = value
            if default is not None and not results:
                results.append(default)
        if ifs > 1:
            return results
        return results[-1] if results else None

    def _filter_if(self, value, path=''):
        if isinstance(value, list):
            return [self._filter_if(v, '%s[%d]' % (path, i)) for i, v in enumerate(value)]
        if not isinstance(value, dict):
            return value
        value = {k: self._filter_if(v, '%s.%s' % (path, k) if path else str(k)) for k, v in value.items()}
        flat = not any(isinstance(v, (dict, list)) for v in value.values())
        if flat and value:
            ifs = [k for k in value if isinstance(k, str) and _IF_RE.match(k.strip())]
            if ifs and len(ifs) != len(value):
                raise ConfigError('%s: invalid #if statement(s): %s' %
                                  (path, ', '.join(str(k) for k in value if k not in ifs)))
            if ifs:
                return self._eval_if_mapping(value, path)
        return value

    def _apply_conditional_excludes(self, items):
        result = []
        if isinstance(items, list):
            for item in items:
                result.extend(self._apply_conditional_excludes(item) if isinstance(item, (list, dict)) else [item])
            return result
        for condition, value in items.items():
            m = re.match(r'^#\s*if\s+(.*)', str(condition))
            if not m:
                raise ConfigError('web_builder.conditional_excludes: invalid condition %r' % condition)
            if self.defines.evaluate(m.group(1)):
                if isinstance(value, (list, dict)):
                    result.extend(self._apply_conditional_excludes(value))
                else:
                    result.append(value)
        return result

    # -----------------------------------------------------------------------------------------------------
    # variables

    def _resolve_env(self, value):
        if value.startswith('~'):
            value = os.path.expanduser('~') + value[1:]
        value = re.sub(r'\$\{env\.([^}]+)\}', lambda m: os.environ.get(m.group(1), m.group(0)), value)
        if os.name == 'nt':
            value = re.sub(r'%([a-z#$\'()*+,\-.?@[\]_`{}~][a-z0-9#$\'()*+,\-.?@[\]_`{}~]*)%',
                           lambda m: os.environ.get(m.group(1), m.group(0)), value, flags=re.I)
        else:
            value = re.sub(r'\$([a-z0-9_]+)', lambda m: os.environ.get(m.group(1), m.group(0)), value, flags=re.I)
        return value

    def _resolve_bins(self, bins):
        result = {}
        for key, value in (bins or {}).items():
            if value is None:
                continue
            if not isinstance(value, str):
                raise ConfigError('bin.%s: expected a string' % key)
            filename = self._resolve_env(value)
            silent = filename.startswith('@')
            if silent:
                filename = filename[1:]
            found = filename if os.path.isfile(filename) else find_executable(filename)
            if not found and is_absolute(filename):
                for ext in ('.exe', '.bat', '.cmd'):
                    if os.path.isfile(filename + ext):
                        found = filename + ext
                        break
            if not found:
                if silent:
                    continue
                raise ConfigError('bin.%s: cannot find %s' % (key, filename))
            result[key] = os.path.realpath(found)
        # executables of the toolchain (system installation or downloaded)
        result.update(self.tools)
        return result

    def _collect_variables(self, data, path, variables):
        for key, value in data.items():
            if key == 'web_builder' and not path:
                continue
            if isinstance(value, dict):
                self._collect_variables(value, path + [str(key)], variables)
        if path and not any(isinstance(v, (dict, list)) for v in data.values()):
            for key, value in data.items():
                if value is not None:
                    variables['${%s.%s}' % ('.'.join(path), key)] = value if isinstance(value, str) else str(value)

    def _resolve_variables(self, data, variables):
        names = sorted(variables, key=len, reverse=True)
        unresolved_re = re.compile(r'\$\{(?!(?:arg|file)\.)[^}]+?\}')

        def resolve(value, path):
            for _ in range(32):
                prev = value
                for name in names:
                    if name in value:
                        value = value.replace(name, variables[name])
                value = self._resolve_env(value)
                if value == prev:
                    break
            m = unresolved_re.findall(value)
            if m:
                raise ConfigError('%s=%s\nCannot resolve variable(s): %s' % (path, value, ', '.join(m)))
            return value
        return _walk_strings(data, resolve)

    # -----------------------------------------------------------------------------------------------------

    def read(self):
        data = self._load(self.filename, False)
        if not data.get('platformio_ini') or not data.get('global_defines_header'):
            raise ConfigError('%s: platformio_ini and global_defines_header are required' % self.filename)

        self.defines.read_platformio_ini(data['platformio_ini'])
        self.defines.read_header(data['global_defines_header'])
        self.defines.define_os()

        web_builder = data.setdefault('web_builder', {})
        web_builder['conditional_excludes'] = self._apply_conditional_excludes(
            web_builder.get('conditional_excludes') or {})
        data = self._filter_if(data)

        self.bins = self._resolve_bins(data.get('bin'))
        data['bin'] = {key: php_escapeshellarg(value) for key, value in self.bins.items()}
        variables = dict(self.static_vars)
        self._collect_variables(data, [], variables)
        data = self._resolve_variables(data, variables)

        self.dependencies += [data['global_defines_header'], data['platformio_ini']]
        self.data = data
        self._validate()
        return self

    # -----------------------------------------------------------------------------------------------------
    # schema

    def _path(self, section, key, required=True, must_exist=None):
        value = (self.data.get(section) or {}).get(key)
        if value is None:
            if required:
                raise ConfigError('%s.%s is required' % (section, key))
            return None
        value = realpath(value, self.config_dir)
        if must_exist == 'dir' and not os.path.isdir(value):
            raise ConfigError('%s.%s: directory does not exist: %s' % (section, key, value))
        return value

    def _validate(self):
        d = self.data
        self.data_dir = self._path('filesystem', 'data_dir', must_exist='dir')
        self.web_target_dir = self._path('filesystem', 'web_target_dir')
        self.listings_file = self._path('filesystem', 'listings_file')

        conf = d.get('configuration') or {}
        self.package_hash_file = self._path('configuration', 'package_hash_file', required=False)
        self.log_file = self._path('configuration', 'log_file', required=False)
        level = str(conf.get('log_level', 'notice')).lower()
        if level not in LOG_LEVELS:
            raise ConfigError("configuration.log_level: '%s' is not allowed, possible values %s" %
                              (level, '|'.join(LOG_LEVELS)))
        self.log_level = LOG_LEVELS[level]
        self.set_env = [(str(k), '' if v is None else str(v)) for k, v in (conf.get('set_env') or {}).items()]
        self.cleanup_temp_dir_on_failure = bool(conf.get('cleanup_temp_dir_on_failure', True))
        self.run_command_on_success = conf.get('run_command_on_success') or None
        self.run_command_on_failure = conf.get('run_command_on_failure') or None

        web_builder = d.get('web_builder') or {}
        self.branches = list(web_builder.get('branches') or [])
        self.conditional_excludes = list(web_builder.get('conditional_excludes') or [])
        self.groups = []
        for name, group in (web_builder.get('groups') or {}).items():
            self.groups.append(Group(name, group or {}, self))

    def known_branches(self):
        branches = list(self.branches)
        for group in self.groups:
            for processor in group.processors:
                branches += [b.lstrip('!') for b in processor.branch]
        return list(dict.fromkeys(branches))

    def dump(self):
        return self.data


class Group(object):

    def __init__(self, name, data, config):
        self.name = name
        path = 'web_builder.groups.%s' % name
        unknown = set(data) - {'target_dirs', 'prepend', 'append', 'processors'}
        if unknown:
            raise ConfigError('%s: unknown key(s): %s' % (path, ', '.join(sorted(unknown))))
        self.prepend = list(data.get('prepend') or [])
        self.append = list(data.get('append') or [])
        self.target_dirs = []
        for target_dir, sources in (data.get('target_dirs') or {}).items():
            entry = TargetDir(realpath(target_dir, config.config_dir))
            if isinstance(sources, list):
                for source in sources:
                    if not find_files(source):
                        raise ConfigError('%s.target_dirs.%s: no file matches %s' % (path, target_dir, source))
                    entry.source_files.append(source)
            else:
                for source_dir, pattern in sources.items():
                    source_dir = realpath(source_dir)
                    if not os.path.isdir(source_dir):
                        raise ConfigError('%s.target_dirs.%s: directory does not exist: %s' %
                                          (path, target_dir, source_dir))
                    entry.source_dirs.append((source_dir, PhpRegex(pattern if pattern.startswith('/')
                                                                   else '/%s/' % pattern, allow_not=False)))
            self.target_dirs.append(entry)
        self.processors = [Processor(pname, pdata or {}, '%s.processors.%s' % (path, pname))
                           for pname, pdata in (data.get('processors') or {}).items()]

    def __repr__(self):
        return 'Group(%s)' % self.name


class TargetDir(object):

    def __init__(self, target_dir):
        self.target_dir = target_dir
        self.source_files = []
        self.source_dirs = []


class Processor(object):

    KEYS = {'branch', 'condition', 'pattern', 'filename_pattern', 'content_pattern', 'target', 'publish', 'command'}

    def __init__(self, name, data, path):
        unknown = set(data) - self.KEYS
        if unknown:
            raise ConfigError('%s: unknown key(s): %s' % (path, ', '.join(sorted(unknown))))
        self.name = name
        self.path = path
        branch = data.get('branch') or []
        self.branch = [branch] if isinstance(branch, str) else list(branch)
        self.condition = data.get('condition')
        self.pattern = PhpRegex(data['pattern']) if data.get('pattern') else None
        self.filename_pattern = PhpRegex(data['filename_pattern']) if data.get('filename_pattern') else None
        self.content_pattern = PhpRegex(data['content_pattern']) if data.get('content_pattern') else None
        self.target = data.get('target')
        self.publish = bool(data.get('publish', False))
        command = data.get('command')
        if isinstance(command, str):
            if command.startswith('@'):
                command = {'type': 'internal', 'command': command[1:]}
            else:
                command = {'type': 'command', 'command': command}
        if not isinstance(command, dict) or command.get('type') not in COMMAND_TYPES or not command.get('command'):
            raise ConfigError('%s.command: expected a shell command, "@<internal command>" or a mapping with '
                              'type (%s) and command' % (path, '|'.join(COMMAND_TYPES)))
        self.command = command

    def __repr__(self):
        return 'Processor(%s)' % self.name
