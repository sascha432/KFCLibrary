#
# Author: sascha_lammers@gmx.de
#
# Source files of the groups, conditional excludes and the package hash that detects changes.
#

import datetime
import glob
import hashlib
import json
import os

from . import BuilderError
from .fileutils import find_files, list_dir, normalize, same_path


def sha1_file(filename):
    h = hashlib.sha1()
    with open(filename, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 16), b''):
            h.update(chunk)
    return h.hexdigest()


class SourceFile(object):

    def __init__(self, path, target_dir, group, is_static):
        self.path = path
        self.target_dir = target_dir
        self.group = group
        self.is_static = is_static
        self.hash = None
        self.size = None


class PackageFiles(object):

    def __init__(self, config):
        self.config = config
        self.files = []
        self.dependencies = []
        self.hash = None

    def scan(self):
        self.files = []
        directories = []
        # static files in the order of the configuration, then the files of the directories
        for group in self.config.groups:
            for target_dir in group.target_dirs:
                for source_dir, pattern in target_dir.source_dirs:
                    directories.append((source_dir, pattern, target_dir.target_dir, group))
                for source_file in target_dir.source_files:
                    for path in find_files(source_file):
                        self.files.append(SourceFile(path, target_dir.target_dir, group, True))
        for source_dir, pattern, target_dir, group in directories:
            for entry in list_dir(source_dir):
                path = os.path.realpath(os.path.join(source_dir, entry))
                if os.path.isfile(path) and pattern.search(entry):
                    self.files.append(SourceFile(path, target_dir, group, False))
        for filename in self.config.conditional_excludes:
            self.remove(filename)
        for file in self.files:
            file.hash = sha1_file(file.path)
            file.size = os.path.getsize(file.path)

    def remove(self, filename):
        # the paths of the files are resolved (lib/KFCLibrary can be a junction), the excluded ones as well
        names = sorted(glob.glob(filename)) if '*' in filename else [filename]
        count = 0
        for name in names:
            name = os.path.realpath(normalize(name))
            before = len(self.files)
            self.files = [f for f in self.files if not same_path(f.path, name)]
            count += before - len(self.files)
        return count

    def files_by_group(self, group):
        return [f for f in self.files if f.group is group]

    # -----------------------------------------------------------------------------------------------------
    # package hash

    def calc_hash(self, extra):
        h = hashlib.sha1()
        for key in sorted(extra):
            h.update(('%s=%s\n' % (key, extra[key])).encode('utf-8'))
        for filename in sorted(set(self.dependencies)):
            if not os.path.exists(filename):
                raise BuilderError("Cannot read hash of '%s'" % filename)
            h.update(('%s=%s\n' % (os.path.normcase(filename), sha1_file(filename))).encode('utf-8'))
        for file in self.files:
            h.update(('%s:%s=%s\n' % (file.group.name, os.path.normcase(file.path), file.hash)).encode('utf-8'))
        self.hash = h.hexdigest()
        return self.hash

    def _read_hash_file(self):
        filename = self.config.package_hash_file
        if not filename:
            return {}
        try:
            with open(filename, 'rt') as f:
                data = json.load(f)
            return data if isinstance(data, dict) else {}
        except (OSError, ValueError):
            return {}

    def is_hash_match(self, key):
        entry = self._read_hash_file().get(key)
        return isinstance(entry, dict) and entry.get('hash') == self.hash

    def store_hash(self, key, dirty=False):
        filename = self.config.package_hash_file
        if not filename:
            return
        data = self._read_hash_file()
        data[key] = {
            'hash': '***DIRTY***' if dirty else self.hash,
            'last_update': datetime.datetime.now().astimezone().isoformat(timespec='seconds'),
            'files': 0 if dirty else len(self.files),
            'file_size': 0 if dirty else sum(f.size or 0 for f in self.files),
        }
        os.makedirs(os.path.dirname(filename), exist_ok=True)
        with open(filename, 'wt') as f:
            json.dump(data, f)
