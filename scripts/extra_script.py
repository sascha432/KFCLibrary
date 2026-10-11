Import("env")
import atexit
import datetime
try:
    import configparser
except ImportError:
    import ConfigParser as configparser
from SCons.Script import ARGUMENTS
import subprocess
from os import path
import os
import subprocess
import click
import re
from datetime import datetime
import platform

# NOTE: Single extra script per library.json

symlinks = []

def extract_re(regex, filename):
    with open(filename, 'rt') as file:
        contents = file.read()
        contents = contents.replace('\n', ' ').replace('\r', ' ')
        res = {}
        for regex in regex:
            m = re.search(regex, contents)
            if m:
                res.update(m.groupdict())
            else:
                click.secho('Cannot extract %s from %s' % (regex, filename), fg='red')
                exit(1)
        return res

def env_abspath(env, str):
    return path.abspath(env.subst(str))

def build_webui(source, target, env, force = False):
    global symlinks

    verbose = int(ARGUMENTS.get("PIOVERBOSE", 0))

    src = path.abspath(env.subst("$PROJECT_LIBDEPS_DIR/$PIOENV/KFCLibrary"))
    lib_dst = path.abspath(env.subst("$PROJECT_DIR/lib/KFCLibrary"))
    if platform.system() == 'Windows':
        a = [ 'mklink', '/J', lib_dst, src ]
        return_code = subprocess.run(a, shell=True).returncode
    else:
        try:
            os.symlink(src, lib_dst, target_is_directory=True)
            return_code = 0
        except:
            return_code = 1
    if return_code:
        click.secho('Failed to create symlink: [%u] %s -> %s\nMake sure this links to the current library' % (return_code, src, lib_dst), fg='yellow')

    # the Python web builder (KFCWebBuilder/kfc_webbuilder), run with the Python of PlatformIO
    builder_dir = env_abspath(env, '$PROJECT_LIBDEPS_DIR/$PIOENV/KFCLibrary/KFCWebBuilder')
    if not path.isdir(path.join(builder_dir, 'kfc_webbuilder')):
        builder_dir = env_abspath(env, '$PROJECT_DIR/lib/KFCLibrary/KFCWebBuilder')
    config_file = env_abspath(env, '$PROJECT_DIR/KFCWebBuilder.yaml')

    build = extract_re([r'number\s*=\s*(?P<build>[0-9]+)'], env_abspath(env, '$PROJECT_DIR/include/build_number.txt'))
    version = extract_re([r'#define\s+FIRMWARE_VERSION_MAJOR\s+(?P<major>[0-9]+) ', r'#define\s+FIRMWARE_VERSION_MINOR\s+(?P<minor>[0-9]+) ', r'#define\s+FIRMWARE_VERSION_REVISION\s+(?P<rev>[0-9]+) '], env_abspath(env, '$PROJECT_DIR/include/global.h'))
    version = '%s.%s.%s Build %s (%s)' % (version['major'], version['minor'], version['rev'], build['build'], datetime.now().strftime('%b %d %Y %H:%M:%S'))

    filename = env_abspath(env, '$PROJECT_DIR/data/.pvt/build')
    os.makedirs(path.dirname(filename), exist_ok=True)
    with open(filename, 'w') as file:
        file.write(version)

    defines = env.get('CPPDEFINES');
    definesFile = env.subst('$BUILD_DIR/cppdefines.txt');
    with open(definesFile, 'wt') as file:
        for define in defines:
            if isinstance(define, tuple):
                (key, val) = define
            else:
                key = define
                val = 1;
            file.write('%s=%s\n' % (env.subst(key), env.subst(val)))

    if not path.isdir(path.join(builder_dir, 'kfc_webbuilder')):
        click.secho('cannot find the web builder in %s' % builder_dir, fg='yellow')
        env.Exit(1)
    if not path.isfile(config_file):
        click.secho('cannot find %s' % config_file, fg='yellow')
        env.Exit(1)

    python_exe = env.subst('$PYTHONEXE')
    args = [ python_exe, '-m', 'kfc_webbuilder', config_file, '--branch', 'littlefs', '--env', 'env:%s' % env.subst('$PIOENV'), '--clean-exit-code', '0', '--dirty-exit-code', '0', '--defines-from', definesFile ]

    dirs = []
    for item in defines:
        try:
            if item[0]=='INCLUDE_DATA_DIRS':
                dirs = env.subst(item[1])
                dirs = dirs.split(',')
                break
        except:
            pass

    if len(dirs):
        for src in dirs:
            src = env_abspath(env, src)
            dst = env_abspath(env, '${PROJECTDATA_DIR}/%s' % os.path.basename(src))
            if not path.exists(dst):
                if verbose:
                    click.secho('Creating symlink %s -> %s' % (src, dst), fg='yellow')
                if platform.system() == 'Windows':
                    a = [ 'mklink', '/J', dst, src ]
                    return_code = subprocess.run(a, shell=True).returncode
                else:
                    try:
                        os.symlink(src, dst, False)
                        return_code = 0
                    except:
                        return_code = 1
                if return_code:
                    click.secho('Failed to create symlink: [%u] %s -> %s' % (return_code, src, dst), fg='red')
                    env.Exit(1)
            symlinks.append(dst)

    if force:
        args.append('--force')

    if verbose:
        args.append('--verbose');

    cli_cmd = subprocess.list2cmdline(args)
    if verbose:
        click.echo(cli_cmd)

    builder_env = dict(os.environ)
    builder_env['PYTHONPATH'] = builder_dir + (os.pathsep + builder_env['PYTHONPATH'] if builder_env.get('PYTHONPATH') else '')
    builder_env['PYTHONIOENCODING'] = 'UTF-8'
    return_code = subprocess.run(args, env=builder_env).returncode

    if return_code!=0:
        click.secho('failed to run: %s' % cli_cmd)
        print()
        env.Exit(1)

    # os.unlink(lib_dst)

def rebuild_webui(source, target, env):
    build_webui(source, target, env, True)

def build_webui_cleanup():
    global symlinks
    verbose = int(ARGUMENTS.get("PIOVERBOSE", 0))
    for lnk in symlinks:
        if verbose:
            click.secho('Removing symlink %s' % lnk, fg='yellow')
        os.remove(lnk)
    symlinks = []

# the INCLUDE_DATA_DIRS links must exist while mklittlefs packs data/, they are removed when SCons exits
atexit.register(build_webui_cleanup)

# The WebUI is built before the filesystem image is packed (buildfs, uploadfs, uploadfsota): littlefs.bin depends
# on an always built command. The library script runs before the platform script defines the target of littlefs.bin,
# so a pre-action on it would be lost when the builder is assigned; a dependency is stored in the node itself.
webui_target = env.Command("$BUILD_DIR/webui.stamp", None, build_webui)
env.AlwaysBuild(webui_target)
env.Depends("$BUILD_DIR/littlefs.bin", webui_target)

env.AlwaysBuild(env.Alias("rebuildfs", None, rebuild_webui))
