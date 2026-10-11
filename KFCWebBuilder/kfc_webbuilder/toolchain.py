#
# Author: sascha_lammers@gmx.de
#
# External tools of the web builder and their automatic installation.
#
#   python packages  PyYAML, zopfli, nodejs-wheel-binaries and install-jdk (only if Node.js or Java is missing),
#                    installed with "pip install --target <tools>/python<major><minor>", no Python environment
#                    is modified
#   node / npm       the system installation if "node" (>= 14) is in PATH, otherwise nodejs-wheel-binaries
#   uglify-js        "npm ci" of tools/package-lock.json into <tools>/node, the version must match exactly
#   java             the system installation if "java" (>= 8) is in PATH, otherwise a Temurin JRE downloaded
#                    by install-jdk into <tools>/jre
#   zopfli           pip package for the gzip compression, zlib if it cannot be installed
#
# <tools> is $KFC_WEBBUILDER_TOOLS, default $PLATFORMIO_CORE_DIR/kfc_webbuilder (~/.platformio/kfc_webbuilder).
# KFC_WEBBUILDER_NODE / KFC_WEBBUILDER_JAVA select an executable explicitly.
#

import contextlib
import glob
import hashlib
import importlib
import json
import os
import re
import shutil
import subprocess
import sys

from . import BuilderError
from .fileutils import find_executable

PACKAGE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REQUIREMENTS_FILE = os.path.join(PACKAGE_DIR, 'requirements.txt')
NPM_DIR = os.path.join(PACKAGE_DIR, 'tools')
UGLIFY_JS_VERSION = '3.17.4'
JRE_VERSION = '17'
MIN_NODE_VERSION = 14
MIN_JAVA_VERSION = 8

MANUAL_INSTALL = '''
The web builder could not install %(what)s automatically (no network access, proxy?).
Install it manually and make sure it is in PATH, or point the builder to it:

  Node.js >= %(node)u:   https://nodejs.org/  (or set KFC_WEBBUILDER_NODE=<path to node>)
  Java >= %(java)u:      https://adoptium.net/ (or set KFC_WEBBUILDER_JAVA=<path to java>)
  uglify-js %(uglify)s: npm ci --prefix "%(tools)s/node" with the files of %(npm_dir)s
  Python packages:     "%(python)s" -m pip install -r "%(requirements)s"
'''


class ToolchainError(BuilderError):
    pass


def tools_dir():
    path = os.environ.get('KFC_WEBBUILDER_TOOLS')
    if not path:
        core_dir = os.environ.get('PLATFORMIO_CORE_DIR') or os.path.join(os.path.expanduser('~'), '.platformio')
        path = os.path.join(core_dir, 'kfc_webbuilder')
    return os.path.abspath(path)


def _manual_hint(what):
    return MANUAL_INSTALL % {'what': what, 'node': MIN_NODE_VERSION, 'java': MIN_JAVA_VERSION,
                             'uglify': UGLIFY_JS_VERSION, 'tools': tools_dir(), 'npm_dir': NPM_DIR,
                             'python': sys.executable, 'requirements': REQUIREMENTS_FILE}


@contextlib.contextmanager
def _install_lock():
    # serializes installations of parallel builds
    os.makedirs(tools_dir(), exist_ok=True)
    with open(os.path.join(tools_dir(), '.lock'), 'a+b') as f:
        if os.name == 'nt':
            import msvcrt
            f.seek(0)
            while True:
                try:
                    msvcrt.locking(f.fileno(), msvcrt.LK_LOCK, 1)
                    break
                except OSError:
                    pass
            try:
                yield
            finally:
                f.seek(0)
                msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            fcntl.flock(f.fileno(), fcntl.LOCK_EX)
            try:
                yield
            finally:
                fcntl.flock(f.fileno(), fcntl.LOCK_UN)


def _run(args, **kwargs):
    try:
        return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kwargs)
    except OSError as e:
        return subprocess.CompletedProcess(args, 127, str(e).encode())


def _output(args):
    res = _run(args)
    return res.returncode, res.stdout.decode('utf-8', errors='replace').strip()


# ---------------------------------------------------------------------------------------------------------
# python packages


def _requirement(name):
    with open(REQUIREMENTS_FILE, 'rt') as f:
        for line in f:
            line = line.split('#', 1)[0].strip()
            if re.match(r'^%s\b' % re.escape(name), line, re.I):
                return line
    raise ToolchainError('%s is not listed in %s' % (name, REQUIREMENTS_FILE))


def python_site_dir():
    return os.path.join(tools_dir(), 'python%u%u' % sys.version_info[:2])


def _add_site_dir():
    site_dir = python_site_dir()
    if os.path.isdir(site_dir) and site_dir not in sys.path:
        sys.path.insert(0, site_dir)
        importlib.invalidate_caches()


def ensure_python_package(module, requirement_name, verbose=False):
    _add_site_dir()
    try:
        return importlib.import_module(module)
    except ImportError:
        pass
    requirement = _requirement(requirement_name)
    with _install_lock():
        _add_site_dir()
        try:
            return importlib.import_module(module)
        except ImportError:
            pass
        print('Installing %s into %s' % (requirement, python_site_dir()))
        res = _run([sys.executable, '-m', 'pip', 'install', '--disable-pip-version-check', '--no-warn-script-location',
                    '--upgrade', '--target', python_site_dir(), requirement])
        if res.returncode != 0:
            raise ToolchainError('pip install %s failed:\n%s\n%s' % (
                requirement, res.stdout.decode('utf-8', errors='replace')[-2000:], _manual_hint(requirement)))
    _add_site_dir()
    return importlib.import_module(module)


# ---------------------------------------------------------------------------------------------------------
# node


def _node_version(node):
    code, out = _output([node, '-v'])
    m = re.match(r'v(\d+)\.', out)
    return (int(m.group(1)), out) if code == 0 and m else (0, out)


def find_node(verbose=False):
    # returns (node, npm command as list, version)
    node = os.environ.get('KFC_WEBBUILDER_NODE') or find_executable('node')
    if node:
        major, version = _node_version(node)
        if major >= MIN_NODE_VERSION:
            npm_cli = None
            for candidate in (os.path.join(os.path.dirname(node), 'node_modules', 'npm', 'bin', 'npm-cli.js'),
                              os.path.join(os.path.dirname(os.path.dirname(node)), 'lib', 'node_modules', 'npm', 'bin',
                                           'npm-cli.js')):
                if os.path.isfile(candidate):
                    npm_cli = candidate
                    break
            npm = [node, npm_cli] if npm_cli else ([find_executable('npm')] if find_executable('npm') else None)
            if npm:
                return node, npm, version
        if verbose:
            print('Ignoring %s (%s), Node.js >= %u with npm is required' % (node, version or 'no version',
                                                                             MIN_NODE_VERSION))
    nodejs_wheel = ensure_python_package('nodejs_wheel.executable', 'nodejs-wheel-binaries', verbose)
    root = nodejs_wheel.ROOT_DIR
    node = os.path.join(root, 'node.exe') if os.name == 'nt' else os.path.join(root, 'bin', 'node')
    npm_cli = os.path.join(root, 'lib', 'node_modules', 'npm', 'bin', 'npm-cli.js')
    major, version = _node_version(node)
    if not major:
        raise ToolchainError('%s does not work: %s' % (node, version))
    return node, [node, npm_cli], version


def _lock_hash():
    h = hashlib.sha1()
    for name in ('package.json', 'package-lock.json'):
        with open(os.path.join(NPM_DIR, name), 'rb') as f:
            h.update(f.read())
    return h.hexdigest()


def find_uglify_js(node, npm, verbose=False):
    prefix = os.path.join(tools_dir(), 'node')
    script = os.path.join(prefix, 'node_modules', 'uglify-js', 'bin', 'uglifyjs')
    package_json = os.path.join(prefix, 'node_modules', 'uglify-js', 'package.json')
    marker = os.path.join(prefix, '.lock_hash')

    def installed():
        try:
            with open(package_json, 'rt') as f:
                version = json.load(f).get('version')
            with open(marker, 'rt') as f:
                lock_hash = f.read().strip()
        except (OSError, ValueError):
            return False
        return version == UGLIFY_JS_VERSION and lock_hash == _lock_hash() and os.path.isfile(script)

    if installed():
        return script
    with _install_lock():
        if installed():
            return script
        os.makedirs(prefix, exist_ok=True)
        for name in ('package.json', 'package-lock.json'):
            shutil.copyfile(os.path.join(NPM_DIR, name), os.path.join(prefix, name))
        print('Installing uglify-js %s into %s' % (UGLIFY_JS_VERSION, prefix))
        env = dict(os.environ)
        env['PATH'] = os.path.dirname(node) + os.pathsep + env.get('PATH', '')
        res = _run(npm + ['ci', '--no-audit', '--no-fund', '--prefix', prefix], cwd=prefix, env=env)
        if res.returncode != 0:
            raise ToolchainError('npm ci failed:\n%s\n%s' % (res.stdout.decode('utf-8', errors='replace')[-2000:],
                                                             _manual_hint('uglify-js')))
        with open(marker, 'wt') as f:
            f.write(_lock_hash())
    if not installed():
        raise ToolchainError('uglify-js %s is not installed in %s' % (UGLIFY_JS_VERSION, prefix))
    return script


# ---------------------------------------------------------------------------------------------------------
# java


def _java_version(java):
    code, out = _output([java, '-version'])
    m = re.search(r'version "(\d+)(?:\.(\d+))?', out)
    if code != 0 or not m:
        return 0, out
    major = int(m.group(1))
    if major == 1 and m.group(2):
        major = int(m.group(2))
    return major, out.splitlines()[0]


def _downloaded_java():
    exe = 'java.exe' if os.name == 'nt' else 'java'
    for java in sorted(glob.glob(os.path.join(tools_dir(), 'jre', '*', 'bin', exe)) +
                       glob.glob(os.path.join(tools_dir(), 'jre', '*', 'Contents', 'Home', 'bin', exe)), reverse=True):
        if _java_version(java)[0] >= MIN_JAVA_VERSION:
            return java
    return None


def find_java(verbose=False):
    java = os.environ.get('KFC_WEBBUILDER_JAVA') or find_executable('java')
    if java:
        major, version = _java_version(java)
        if major >= MIN_JAVA_VERSION:
            return java, version
        if verbose:
            print('Ignoring %s (%s), Java >= %u is required' % (java, version or 'no version', MIN_JAVA_VERSION))
    java = _downloaded_java()
    if not java:
        jdk = ensure_python_package('jdk', 'install-jdk', verbose)
        with _install_lock():
            java = _downloaded_java()
            if not java:
                path = os.path.join(tools_dir(), 'jre')
                print('Downloading a Java %s runtime into %s' % (JRE_VERSION, path))
                try:
                    jdk.install(JRE_VERSION, jre=True, path=path)
                except Exception as e:
                    raise ToolchainError('Java download failed: %s\n%s' % (e, _manual_hint('Java')))
                java = _downloaded_java()
        if not java:
            raise ToolchainError('No Java runtime found in %s' % os.path.join(tools_dir(), 'jre'))
    return java, _java_version(java)[1]


# ---------------------------------------------------------------------------------------------------------
# gzip


def find_compressor(verbose=False):
    # zopfli (pip package), zlib if it cannot be installed
    from .compress import Compressor
    try:
        zopfli_gzip = ensure_python_package('zopfli.gzip', 'zopfli', verbose)
        version = 'zopfli %s' % getattr(importlib.import_module('zopfli'), '__version__', '?')
        return Compressor(zopfli_gzip, version, os.path.join(tools_dir(), 'cache', 'zopfli'))
    except (ToolchainError, ImportError) as e:
        print('zopfli is not available, compressing with zlib (the files are about 4%% larger): %s' %
              str(e).strip().splitlines()[0])
        return Compressor()


# ---------------------------------------------------------------------------------------------------------


class Toolchain(object):

    def __init__(self, verbose=False):
        self.verbose = verbose
        node, npm, node_version = find_node(verbose)
        uglify_js = find_uglify_js(node, npm, verbose)
        java, java_version = find_java(verbose)
        self.compressor = find_compressor(verbose)
        self.bins = {'nodejs': os.path.realpath(node), 'uglify_js': os.path.realpath(uglify_js),
                     'java': os.path.realpath(java)}
        self.versions = {'node': node_version, 'uglify-js': UGLIFY_JS_VERSION, 'java': java_version,
                         'gzip': self.compressor.version}

    def describe(self):
        lines = ['tools directory: %s' % tools_dir()]
        for key, path in self.bins.items():
            version = self.versions.get({'nodejs': 'node'}.get(key, key.replace('_', '-')), '')
            lines.append('%-10s %s (%s)' % (key, path, version))
        lines.append('%-10s %s%s' % ('gzip', self.compressor.version,
                                     ', cache %s' % self.compressor.cache_dir if self.compressor.cache_dir else ''))
        return '\n'.join(lines)


def placeholder_bins():
    # for commands that do not run a build (--files, --dump-defines, ...)
    return {'nodejs': 'node', 'uglify_js': 'uglifyjs', 'java': 'java'}
