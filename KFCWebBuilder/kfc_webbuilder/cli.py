#
# Author: sascha_lammers@gmx.de
#
# python -m kfc_webbuilder <KFCWebBuilder.yaml> --env <env> --branch littlefs --defines-from <cppdefines.txt>
#

import argparse
import hashlib
import json
import os
import sys
import traceback

from . import BuilderError, __version__
from . import toolchain


def _builder_source_hash():
    h = hashlib.sha1()
    package_dir = os.path.dirname(os.path.abspath(__file__))
    for name in sorted(os.listdir(package_dir)):
        if name.endswith('.py'):
            with open(os.path.join(package_dir, name), 'rb') as f:
                h.update(f.read())
    return h.hexdigest()


def parse_args(argv):
    parser = argparse.ArgumentParser(prog='kfc_webbuilder', description='KFC WebUI builder %s' % __version__)
    parser.add_argument('config', help='configuration file (KFCWebBuilder.yaml)')
    parser.add_argument('-f', '--force', action='store_true', help='build even if all files are up-to-date')
    parser.add_argument('--dirty', action='store_true', help='mark the files as modified')
    parser.add_argument('-v', '--verbose', action='store_true', help='be more talkative')
    parser.add_argument('-e', '--env', help='PlatformIO environment ("env:" is optional)')
    parser.add_argument('-b', '--branch', action='append', default=[], dest='branches',
                        help='branch to execute, can be used more than once')
    parser.add_argument('--clean-exit-code', type=int, default=127, help='exit code if nothing has been modified')
    parser.add_argument('--dirty-exit-code', type=int, default=0, help='exit code after a build')
    parser.add_argument('--dep', action='store_true', help='display the dependencies')
    parser.add_argument('--files', action='store_true', help='display the files of the package')
    parser.add_argument('--show-env', action='store_true', help='display the sections of platformio.ini')
    parser.add_argument('-D', '--dump-config', action='store_true', help='display the resolved configuration')
    parser.add_argument('-DD', '--dump-defines', action='store_true', help='display the preprocessor defines')
    parser.add_argument('--defines-from', help='read the CPPDEFINES from a file (KEY=VALUE per line)')
    parser.add_argument('--check-tools', action='store_true', help='find or install the tools and display them')
    parser.add_argument('--debug', action='store_true', help='display a stack trace on errors')
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    env = args.env
    if env and not env.startswith('env:'):
        env = 'env:' + env
    builder = None
    try:
        toolchain.ensure_python_package('yaml', 'PyYAML', args.verbose)
        from .config import Config
        from .defines import Defines
        from .package import PackageFiles
        from .pipeline import Builder

        needs_tools = args.check_tools or not (args.dep or args.files or args.show_env or args.dump_config or
                                               args.dump_defines or args.dirty)
        tools = toolchain.Toolchain(args.verbose) if needs_tools else None
        if args.check_tools:
            print(tools.describe())
            return 0

        defines = Defines()
        defines.set_environment(env)
        dependencies = []
        if args.defines_from:
            defines.read_defines(args.defines_from)
            dependencies.append(os.path.abspath(args.defines_from))

        config = Config(args.config, defines, env[4:] if env else None,
                        tools.bins if tools else toolchain.placeholder_bins()).read()

        if args.show_env:
            if not defines.environments:
                print('No environments defined in %s' % config.data['platformio_ini'])
            else:
                print('PlatformIO environments:')
                for name in defines.environments:
                    print(name)
            return 0
        if args.dump_config:
            print(json.dumps(config.dump(), indent=4))
            return 0
        if args.dump_defines:
            defines.dump()
            return 0

        builder = Builder(config, defines, args.branches, args.verbose, tools.compressor if tools else None)
        builder.validate_branches()
        package = PackageFiles(config)
        package.dependencies = config.dependencies + dependencies
        key = builder.branch_string()

        if args.dirty:
            package.store_hash(key, dirty=True)
            print('Marked files as modified')
            return 0

        builder.log(100, 'Scanning dependencies')
        package.scan()
        if args.files:
            print('Files:')
            for file in package.files:
                print(file.path)
            return 0
        if args.dep:
            print('Dependencies:')
            for filename in package.dependencies:
                print(filename)
            print('(and %u project files. Use --files to display)' % len(package.files))
            return 0

        package.calc_hash({'env': env or '', 'builder': _builder_source_hash(), 'branch': key,
                           'tools': json.dumps(tools.versions, sort_keys=True)})
        if package.is_hash_match(key):
            if not args.force:
                print('No changes detected.')
                return args.clean_exit_code
            print('No changes detected, build forced...')

        builder.build(package)
        package.store_hash(key)
        builder.run_hook(config.run_command_on_success)
        return args.dirty_exit_code

    except (BuilderError, OSError) as e:
        if builder:
            try:
                builder.run_hook(builder.config.run_command_on_failure)
            except BuilderError:
                pass
            if builder.get_path():
                print(builder.get_path() + ':')
        if args.debug:
            traceback.print_exc()
        print('Build failed with: %s' % e)
        return 1
    finally:
        if builder and builder.cleanup_on_exit:
            builder.remove_temp_dir()
