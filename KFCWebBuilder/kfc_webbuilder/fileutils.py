#
# Author: sascha_lammers@gmx.de
#

import os
import re

_ABSOLUTE_RE = re.compile(r'^([\\/]|[a-z]:)', re.I)


def is_absolute(path):
    return bool(_ABSOLUTE_RE.match(path))


def normalize(path, base_dir=None):
    # absolute path with "." and ".." resolved, the file does not need to exist
    if not is_absolute(path):
        path = os.path.join(base_dir or os.getcwd(), path)
    return os.path.normpath(path)


def realpath(path, base_dir=None):
    path = normalize(path, base_dir)
    if os.path.exists(path):
        return os.path.realpath(path)
    return path


def same_path(a, b):
    return os.path.normcase(a) == os.path.normcase(b)


def list_dir(path):
    # directory order (readdir), without "." and ".."
    return os.listdir(path)


def find_files(filename):
    # a file name or a pattern with "*" in the file name, files starting with a "." are skipped
    if os.path.exists(filename):
        return [os.path.realpath(filename)]
    path, name = os.path.split(filename)
    if '*' not in name or not os.path.isdir(path):
        return []
    pattern = re.compile('^' + '.*?'.join(re.escape(part) for part in name.split('*')) + '$')
    files = []
    for entry in list_dir(path):
        if not entry.startswith('.') and pattern.match(entry):
            files.append(os.path.realpath(os.path.join(path, entry)))
    return files


def find_in_path(cmd):
    if is_absolute(cmd):
        return os.path.realpath(cmd) if os.path.exists(cmd) else None
    for path in os.environ.get('PATH', '').split(os.pathsep):
        filename = os.path.join(path, cmd)
        if os.path.exists(filename):
            return os.path.realpath(filename)
    return None


def find_executable(cmd):
    # like find_in_path(), but tries the Windows extensions as well
    candidates = [cmd]
    if os.name == 'nt' and not os.path.splitext(cmd)[1]:
        candidates += [cmd + ext for ext in ('.exe', '.bat', '.cmd')]
    for candidate in candidates:
        result = find_in_path(candidate)
        if result and os.path.isfile(result):
            return result
    return None


def remove_matching(dir_name, pattern):
    # recursively delete the files whose name matches the pattern, empty sub directories are removed
    files = dirs = 0
    for entry in list_dir(dir_name):
        full_path = os.path.join(dir_name, entry)
        if os.path.isdir(full_path) and not os.path.islink(full_path):
            sub_files, sub_dirs = remove_matching(full_path, pattern)
            files += sub_files
            dirs += sub_dirs
            try:
                os.rmdir(full_path)
                dirs += 1
            except OSError:
                pass
        elif pattern.search(entry):
            try:
                os.unlink(full_path)
                files += 1
            except OSError:
                pass
    return files, dirs
