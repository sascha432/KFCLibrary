#
# Author: sascha_lammers@gmx.de
#
# Runs the processors of every group over its files.
#
# A file starts with its source as input. Each processor writes a new temporary file (or the target if it
# publishes) that is the input of the next processor. "copy-combined" switches the group to a single file
# that contains all files of the group, in the order of the configuration.
#

import concurrent.futures
import datetime
import hashlib
import os
import random
import re
import shutil
import subprocess
import sys

from . import BuilderError
from .compress import Compressor
from .config import php_escapeshellarg
from .fileutils import normalize
from .mapper import Mapper
from .template import HtmlTemplate

INTERNAL_COMMANDS = ('remove-file', 'create-html-template', 'copy', 'copy-combined', 'gzip', 'assign-name', 'map',
                     'publish-dir')

LOG_LEVEL_NAMES = {100: 'DEBUG', 200: 'NOTICE', 300: 'WARNING', 400: 'ERROR'}


class File(object):

    def __init__(self, source, target_dir=None):
        self.source = source
        self.target = os.path.join(target_dir, os.path.basename(source)) if target_dir else None
        self.target_dir = target_dir
        self.tmp_in = None
        self.tmp_out = None
        self.processors = []

    def source_str(self):
        return '\n'.join(self.source) if isinstance(self.source, list) else self.source

    def vars(self):
        is_list = isinstance(self.source, list)
        return {
            '${file.target_dir}': self.target_dir,
            '${file.all_sources}': self.source if is_list else None,
            '${file.source}': None if is_list else self.source,
            '${file.source_name}': None if is_list else os.path.basename(self.source),
            '${file.target}': self.target,
            '${file.target_name}': os.path.basename(self.target) if self.target else '',
            '${file.tmp_in}': self.tmp_in,
            '${file.tmp_out}': self.tmp_out,
        }


class Builder(object):

    def __init__(self, config, defines, branches, verbose=False, compressor=None):
        self.config = config
        self.compressor = compressor or Compressor()
        self.defines = defines
        self.branches = list(branches or [])
        self.verbose = verbose
        self.assets = {}
        self.path = []
        self.mapper = None
        self.tmp_combined = None
        self.temp_dir = None
        self.cleanup_on_exit = config.cleanup_temp_dir_on_failure
        # parallel external commands, each one can be a JVM
        self.jobs = max(1, int(os.environ.get('KFC_WEBBUILDER_JOBS') or min(4, os.cpu_count() or 1)))
        self._tmp_counter = 0

    # -----------------------------------------------------------------------------------------------------
    # logging

    def log(self, level, message):
        if self.config.log_file and level >= self.config.log_level:
            line = '%s [%s] %s\n' % (datetime.datetime.now().strftime('%Y-%m-%dT%H:%M:%S'), LOG_LEVEL_NAMES[level],
                                     message)
            if self.verbose:
                sys.stdout.write(line)
            try:
                os.makedirs(os.path.dirname(self.config.log_file), exist_ok=True)
                with open(self.config.log_file, 'at', encoding='utf-8') as f:
                    f.write(line)
            except OSError:
                pass

    def get_path(self):
        return '.'.join(self.path)

    def branch_string(self):
        if not self.branches:
            return 'default'
        return '[%s]' % '|'.join(sorted(set(self.branches)))

    # -----------------------------------------------------------------------------------------------------
    # temporary files

    def create_temp_dir(self):
        os.makedirs(self.config.temp_root, exist_ok=True)
        while True:
            path = os.path.join(self.config.temp_root, '%u_%08x' % (os.getpid(), random.getrandbits(32)))
            if not os.path.exists(path):
                os.makedirs(path)
                self.temp_dir = path
                return path

    def remove_temp_dir(self):
        if self.temp_dir and os.path.isdir(self.temp_dir):
            shutil.rmtree(self.temp_dir, ignore_errors=True)
        self.temp_dir = None

    def create_temp_file(self, processor):
        self._tmp_counter += 1
        path = os.path.join(self.temp_dir, '%s_%06u' % (processor.name, self._tmp_counter))
        open(path, 'wb').close()
        return path

    # -----------------------------------------------------------------------------------------------------
    # processors

    def validate_branches(self):
        known = self.config.known_branches()
        for branch in self.branches:
            if branch not in known:
                raise BuilderError("Branch '%s' does not exist. (%s)" % (branch, ','.join(known)))

    def is_branch_selected(self, processor):
        # positive entries: one of them must be selected, "!name": skipped if name is selected
        positive = [b for b in processor.branch if not b.startswith('!')]
        for branch in processor.branch:
            if branch.startswith('!') and branch[1:] in self.branches:
                return False
        return not positive or any(b in self.branches for b in positive)

    def groups(self):
        return [g for g in self.config.groups if not g.append and not g.prepend]

    def processors(self, group):
        processors = list(group.processors)
        for other in self.config.groups:
            if group.name in other.prepend:
                processors = list(other.processors) + processors
            if group.name in other.append:
                processors = processors + list(other.processors)
        result = []
        for processor in processors:
            if not self.is_branch_selected(processor):
                continue
            if processor.condition:
                condition = re.sub(r'^#\s*if', '', processor.condition)
                if not self.defines.evaluate(condition):
                    continue
            result.append(processor)
        return result

    def add_asset(self, name, filename):
        web_dir = self.config.web_target_dir
        if os.path.normcase(filename).startswith(os.path.normcase(web_dir)):
            filename = filename[len(web_dir) + 1:]
        self.assets['%' + name + '%'] = filename.replace('\\', '/')

    def _prepare(self, processor, file):
        # returns False if the processor does not apply to the file
        tmp_in = file.tmp_out if file.tmp_out else file.source
        if processor.pattern or processor.filename_pattern:
            if isinstance(file.source, list):
                raise BuilderError('%s: pattern and filename_pattern cannot be used for combined files' %
                                   processor.path)
            if processor.pattern and not processor.pattern.is_match(file.source):
                self.log(100, 'Pattern does not match %s' % file.source)
                return False
            if processor.filename_pattern and not processor.filename_pattern.is_match(os.path.basename(file.source)):
                self.log(100, 'Filename pattern does not match %s' % file.source)
                return False
        if processor.content_pattern:
            with open(tmp_in, 'rb') as f:
                contents = f.read().decode('latin-1')
            if not processor.content_pattern.is_match(contents):
                self.log(100, 'Content pattern does not match %s' % file.source_str())
                return False

        file.tmp_in = tmp_in
        if processor.target:
            target = processor.target
            for key, value in file.vars().items():
                target = target.replace(key, value if isinstance(value, str) else '')
            file.tmp_out = target
        elif processor.publish:
            if processor.command['type'] == 'command' or processor.command['command'] not in ('map', 'publish-dir'):
                if not file.target:
                    raise BuilderError('No target for file %s' % file.source_str())
            file.tmp_out = file.target
        else:
            file.tmp_out = self.create_temp_file(processor)
        file.processors.append(processor.name)
        return True

    def _shell_command(self, processor, file):
        os.makedirs(os.path.dirname(file.tmp_out), exist_ok=True)
        cmd = processor.command['command']
        for key, value in file.vars().items():
            if isinstance(value, list):
                value = ' '.join(php_escapeshellarg(normalize(v)) for v in value)
            else:
                value = php_escapeshellarg(os.path.normpath(value) if value else '')
            cmd = cmd.replace(key, value)
        return cmd

    def _run_shell(self, cmd, file):
        env = dict(os.environ)
        env.update(self.config.set_env)
        self.log(100, 'Executing command %s' % cmd)
        res = subprocess.run(cmd, shell=True, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if res.returncode != 0:
            raise BuilderError("Command '%s' failed.\nSource %s: Exit code %d\n%s" % (
                cmd, file.source_str(), res.returncode, res.stdout.decode('utf-8', errors='replace').strip()))
        if not os.path.exists(file.tmp_out) or not os.path.getsize(file.tmp_out):
            raise BuilderError("Command '%s' failed.\nSource %s: Output file %s is empty" % (
                cmd, file.source_str(), file.tmp_out))

    def _internal(self, processor, file):
        command = processor.command
        name = command['command']
        if name not in INTERNAL_COMMANDS:
            raise BuilderError("Invalid internal command '%s'" % name)
        if name not in ('map', 'publish-dir', 'assign-name'):
            os.makedirs(os.path.dirname(file.tmp_out), exist_ok=True)
        if name == 'remove-file':
            return 'remove'
        if name == 'create-html-template':
            HtmlTemplate(self.defines).process(file.source, file.tmp_in, file.tmp_out, self.assets)
        elif name == 'copy':
            shutil.copyfile(file.tmp_in, file.tmp_out)
        elif name == 'copy-combined':
            if not self.tmp_combined:
                self.tmp_combined = file.tmp_out
                shutil.copyfile(file.tmp_in, self.tmp_combined)
            else:
                with open(file.tmp_in, 'rb') as src, open(self.tmp_combined, 'ab') as dst:
                    shutil.copyfileobj(src, dst)
        elif name == 'gzip':
            self.compressor.compress_file(file.tmp_in, file.tmp_out)
        elif name == 'assign-name':
            ext = command.get('ext')
            asset = command.get('asset')
            if not ext:
                raise BuilderError('%s: assign-name requires "ext"' % processor.path)
            file.tmp_out = file.tmp_in
            with open(file.tmp_in, 'rb') as f:
                digest = hashlib.md5(f.read()).hexdigest()
            file.target = normalize(os.path.join(file.target_dir, '%s.%s' % (digest[:8], ext)))
            if asset:
                self.add_asset(asset, file.target)
        elif name == 'publish-dir':
            directory = command.get('dir')
            if not directory:
                raise BuilderError('%s: publish-dir requires "dir"' % processor.path)
            target = file.target or ''
            web_dir = self.config.web_target_dir
            if os.path.normcase(target).startswith(os.path.normcase(web_dir)):
                target = target[len(web_dir) + 1:]
            else:
                target = os.path.basename(target)
            file.target = os.path.join(normalize(os.path.expanduser(directory)), target)
            os.makedirs(os.path.dirname(file.target), exist_ok=True)
            shutil.copyfile(file.tmp_in, file.target)
            file.tmp_out = file.tmp_in
        elif name == 'map':
            if not self.mapper:
                self.mapper = Mapper(self)
            return 'remove' if not self.mapper.process(file) and processor.publish else 'keep'
        return 'keep'

    def _log_result(self, processor, file):
        try:
            self.log(100, '%s %s %u => %s %u' % (processor.name, os.path.basename(file.tmp_in),
                                                 os.path.getsize(file.tmp_in), os.path.basename(file.tmp_out),
                                                 os.path.getsize(file.tmp_out)))
        except OSError:
            pass

    def run_processor(self, processor, files):
        # returns the files that are kept
        keep = []
        commands = []
        for file in files:
            if not self._prepare(processor, file):
                keep.append(file)
                continue
            if processor.command['type'] == 'command':
                commands.append((file, self._shell_command(processor, file)))
                keep.append(file)
            else:
                if self._internal(processor, file) != 'remove':
                    keep.append(file)
                if processor.command['command'] != 'remove-file':
                    self._log_result(processor, file)
        if commands:
            with concurrent.futures.ThreadPoolExecutor(max_workers=min(self.jobs, len(commands))) as pool:
                for future in [pool.submit(self._run_shell, cmd, file) for file, cmd in commands]:
                    future.result()
            for file, cmd in commands:
                self._log_result(processor, file)
        return keep

    # -----------------------------------------------------------------------------------------------------

    def build(self, package):
        self.log(200, 'Starting build process, branch: ' + self.branch_string())
        self.create_temp_dir()
        for group in self.groups():
            if self.verbose:
                print('Group ' + group.name)
            files = [File(f.path, f.target_dir) for f in package.files_by_group(group)]
            if not files:
                print('%s: This group does not have any files assigned.' % group.name)
                continue
            self.path.append('group[%s]' % group.name)
            self.tmp_combined = None
            combined = False
            for processor in self.processors(group):
                self.path.append('processor[%s]' % processor.name)
                if self.tmp_combined and files and not combined:
                    file = File([f.source for f in files])
                    file.tmp_out = self.tmp_combined
                    file.target_dir = files[0].target_dir
                    files = [file]
                    combined = True
                if self.verbose:
                    print('Processor %s, %u file(s)' % (processor.name, len(files)))
                files = self.run_processor(processor, files)
                self.path.pop()
            self.path.pop()
        if self.mapper:
            self.path.append('finishBuild::Mapper')
            self.mapper.finish()
            self.path.pop()
        self.cleanup_on_exit = True

    def run_hook(self, command):
        if command:
            res = subprocess.run(command, shell=True)
            if res.returncode != 0:
                raise BuilderError('%s: exit code %d' % (command, res.returncode))
