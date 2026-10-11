# KFC WebUI builder

`kfc_webbuilder` turns the WebUI sources (HTML pages, JavaScript, CSS, images, fonts) into the files of the LittleFS
data directory: minified, combined, gzip compressed and stored under hashed names with an index that the firmware
reads (`include/fs_mapping.h`). It replaces the former PHP builder (PHP 7.4 only): with GNU gzip it wrote byte for
byte the same files for all environments of kfc_fw, since then it compresses with zopfli (about 4% smaller files,
the same content).

Only LittleFS is supported.

## Running it

PlatformIO runs the builder before it packs the filesystem image (`scripts/extra_script.py` of KFCLibrary:
`littlefs.bin` depends on an always built command), with PlatformIO's own Python:

```
pio run -e <env> -t buildfs     # build the WebUI if a source changed, then littlefs.bin
pio run -e <env> -t uploadfs    # the same, then upload the image (formats the filesystem)
pio run -e <env> -t rebuildfs   # always build the WebUI (data/ only, no image)
```

The builder writes `data/webui/**` and skips the build if no source file, define, tool or builder file changed
since the last build (`configuration.package_hash_file`). It can also be run by hand, from the project directory:

```
set PYTHONPATH=lib\KFCLibrary\KFCWebBuilder            (export PYTHONPATH=lib/KFCLibrary/KFCWebBuilder)
python -m kfc_webbuilder KFCWebBuilder.yaml --env <env> --branch littlefs --defines-from .pio/build/<env>/cppdefines.txt
```

| option | |
|---|---|
| `-e, --env <env>` | PlatformIO environment, `env:` is optional |
| `-b, --branch <name>` | branch to execute, can be used more than once (PlatformIO uses `littlefs`) |
| `--defines-from <file>` | CPPDEFINES of the environment, `KEY=VALUE` per line (written by `extra_script.py`) |
| `-f, --force` | build even if nothing changed |
| `-v, --verbose` | show the groups, processors and the mapped files |
| `--clean-exit-code <n>` / `--dirty-exit-code <n>` | exit code without / after a build (127 / 0) |
| `--files` | list the source files of the package |
| `--dep` | list the configuration files the build depends on |
| `-DD, --dump-defines` | show the preprocessor defines |
| `-D, --dump-config` | show the resolved configuration as JSON |
| `--show-env` | list the sections of platformio.ini |
| `--dirty` | mark the package as modified, the next build runs |
| `--check-tools` | find or install the tools and show their paths and versions |
| `--debug` | show a stack trace on errors |

Without `--defines-from` the defines are read from the `-D` flags of `build_flags` in `platformio.ini`. The
defines of `global_defines_header` (`include/global.h`) and `OS_WIN`, `OS_OSX`, `OS_UNIX` are added in both cases.

## Tools

| tool | used for | provided by |
|---|---|---|
| Python >= 3.8 + PyYAML | the builder | PlatformIO's Python; PyYAML is installed if it is missing |
| zopfli | gzip compression | pip package `zopfli` (Python >= 3.10), otherwise zlib |
| Node.js >= 14 + npm | uglify-js | `node` in PATH, otherwise the pip package `nodejs-wheel-binaries` |
| uglify-js 3.17.4 | JavaScript minifier | `npm ci` of [tools/package-lock.json](tools/package-lock.json), exactly this version |
| Java >= 8 | YUI compressor (CSS), htmlcompressor (HTML) | `java` in PATH, otherwise a Temurin JRE 17 downloaded with the pip package `install-jdk` |
| [bin/*.jar](bin/) | YUI compressor 2.4.8, htmlcompressor 1.5.3, closure compiler | part of this directory |

Everything that is installed automatically goes into the tool directory, no Python environment or system
directory is changed:

```
~/.platformio/kfc_webbuilder/          $KFC_WEBBUILDER_TOOLS, default $PLATFORMIO_CORE_DIR/kfc_webbuilder
    python311/                         pip install --target, one directory per Python version
    node/node_modules/uglify-js/       npm ci, reinstalled when tools/package-lock.json changes
    jre/<jdk-17...>/                   only if no Java is found
    cache/zopfli/                      compressed files by the hash of their content, unused entries are removed
                                       after 60 days
```

The versions of the Python packages are pinned in [requirements.txt](requirements.txt). A suitable system
installation is always preferred; `KFC_WEBBUILDER_NODE` and `KFC_WEBBUILDER_JAVA` select an executable
explicitly. `python -m kfc_webbuilder KFCWebBuilder.yaml --check-tools` shows what is used.

**Without network access** install the tools manually: Node.js (<https://nodejs.org/>), a Java runtime
(<https://adoptium.net/>), then `npm ci --prefix ~/.platformio/kfc_webbuilder/node` with the two files of
`tools/` copied there, and `python -m pip install -r requirements.txt` for PyYAML and zopfli.

**Compression**: zopfli writes standard gzip files (no file name, no time stamp) that are about 4% smaller than
`gzip -9`, but it is much slower. The results are cached by the hash of the content, so a build only compresses
the files that changed (the first build of an environment takes a few seconds longer). Without zopfli the builder
uses zlib level 9.

`KFC_WEBBUILDER_JOBS` sets how many external commands run at the same time (default: number of CPUs, at most 4;
each one can be a Java VM).

## Configuration

The project file is `KFCWebBuilder.yaml` in the project directory, the defaults are in
[KFCWebBuilder.yaml](KFCWebBuilder.yaml) of this directory (included by the project file).

```yaml
platformio_ini: ./platformio.ini
global_defines_header: ./include/global.h
filesystem:
  data_dir: ./data
  web_target_dir: ./data/webui
  listings_file: ${filesystem.web_target_dir}/.listings
configuration:
  set_env: {PYTHONIOENCODING: UTF-8}       # environment variables of the commands
  cleanup_temp_dir_on_failure: true
  package_hash_file: ./logs/web_builder.hash
  log_file: ./logs/builder.log
  log_level: debug                         # debug, notice, warning, error
  run_command_on_success: ''               # shell commands after a successful or failed build
  run_command_on_failure: ''
web_builder:
  conditional_excludes:
    '#if !MQTT_SUPPORT':                   # files that are excluded if the expression is true
      - ./Resources/html/mqtt.html
      - ./Resources/js/forms/*.js          # "*" in the file name
  groups:
    js_main:
      target_dirs:
        ./data/webui/js:                   # a list of files ...
          - ./Resources/js/common.js
    html:
      target_dirs:
        ./data/webui:
          ./Resources/html: '/\.(html|xml)$/'   # ... or directories with a pattern for the file names
      processors:
        template2html:
          command: '@create-html-template'
    after:
      append: [js_main, html]              # adds these processors to the listed groups
      processors:
        map:
          branch: [littlefs]
          command: '@map'
          publish: true
includes:
  - ./lib/KFCLibrary/KFCWebBuilder/KFCWebBuilder.yaml
```

- Paths are relative to the file that contains them. `${path.current_config}` is the directory of that file.
- `includes`: the sections `bin`, `commands`, `configuration` and `web_builder` of an included file are merged key by
  key (the including file wins), other top level keys are only taken if they are missing.
- Variables: `${section.key}` is a value of a mapping without nested mappings (`${commands.uglify_js}`,
  `${filesystem.web_target_dir}`), `${bin.<name>}` the quoted absolute path of an executable or jar,
  `${env.NAME}`, `%NAME%` (Windows), `$NAME` (other systems) and a leading `~` are environment variables, and
  `${pio.env}`, `${path.main_config}`, `${path.sys_temp_dir}`, `${path.shared_temp_dir}`, `${date.date}`,
  `${date.time}`, `${date.datetime}`, `${date.timestamp}`, `${date.unixtime}` are built in. An unresolved
  variable is an error.
- A mapping whose keys are all `'#if <expr>'`, `'#ifdef NAME'`, `'#ifndef NAME'`, `'#elif <expr>'`, `'#else'` or
  `'#default'` is replaced by the value of the first matching key (a list of values if it has more than one `#if`).
- `'#if ...'` keys must be quoted: an unquoted `#` starts a YAML comment. Duplicate keys are an error.
- `bin`: a name without a path is searched in PATH (also with `.exe`, `.bat`, `.cmd`), a leading `@` makes a
  missing file non-fatal. `nodejs`, `java` and `uglify_js` come from the toolchain.

### Groups and processors

The groups are processed in the order of the file; a group with `append`/`prepend` has no files of its own, it adds
its processors to the listed groups. Every file of a group runs through the processors in their order: the input
of a processor is the output of the previous one, the first input is the source file.

| option | |
|---|---|
| `command` | a shell command, `'@<internal command>'` or a mapping `{type: internal, command: <name>, <options>}` |
| `branch` | runs if one of the listed branches is selected, `'!name'` skips it if `name` is selected |
| `condition` | `'#if <expr>'`, skipped if the expression is false |
| `filename_pattern` | skipped if the file name does not match the regular expression |
| `content_pattern` | skipped if the content (the input of the processor) does not match |
| `pattern` | skipped if the source path does not match |
| `publish` | the output is the target file (`map`, `publish-dir`) |
| `target` | an explicit output file, `${file.*}` variables are replaced |

Regular expressions are written as in PHP/PCRE: `'/pattern/flags'` with the flags `i m s x u D`. A `!` in the flags
negates the result: `'/%[A-Z0-9_]{1,33}%/!'` matches if the content does **not** contain a `%VARIABLE%`.

Shell commands get the quoted file names as `${file.tmp_in}`, `${file.tmp_out}`, `${file.source}`,
`${file.source_name}`, `${file.target}`, `${file.target_name}`, `${file.target_dir}` and `${file.all_sources}` (the
sources of a combined file). The command fails if its exit code is not 0 or the output file is empty. The commands
of one processor run in parallel.

| internal command | |
|---|---|
| `create-html-template` | the preprocessor below |
| `copy-combined` | the following processors get one file that contains all files of the group, in the order of the configuration |
| `gzip` | gzip compression with zopfli (zlib if zopfli is not available) |
| `assign-name` | target name `<first 8 hex digits of the md5 of the content>.<ext>`, `asset: NAME` makes it available as `%NAME%` in the templates (`ext`, `asset` are options of the mapping) |
| `remove-file` | removes the file from the group |
| `copy` | copies the input to the output |
| `map` | stores the file in the web directory under its hashed name and writes the index (below) |
| `publish-dir` | copies the file into `dir` (option), branch `test_www` for a local test of the WebUI |

## Preprocessor

Expressions follow the C preprocessor: `defined NAME`, `defined(NAME)`, `! ~ - +`, `* / %`, `+ -`, `<< >>`,
`< <= > >=`, `== !=`, `& ^ |`, `&& ||`, `?:`, numbers (decimal, hex, octal, binary, `U`/`L` suffixes), strings and
characters. A name is replaced by the value of its define (recursively), an undefined name is 0. `"0"` and `""`
are false, `==` compares a string and a number numerically.

HTML pages (`create-html-template`):

```html
<!--#if IOT_CLOCK && !IOT_LED_MATRIX-->
...
<!--#elif ESP32-->
...
<!--#else-->
...
<!--#endif-->
<script>
/*--#if ESP8266--*/
...
/*--#endif--*/
</script>
```

- A directive starts the line, the expression cannot contain a `-`.
- Levels are not combined: a nested `#if` is evaluated even if the outer block is inactive, and the lines of an
  `#elif` are kept if the `#if` was true (this is the behavior of the PHP builder, kept for the same output).
- `<!--%%NAME-->...<!--NAME%%-->` is replaced with the preprocessed file `.name.html` (lower case) of the same
  directory. Files starting with a `.` are not published, except `.message.html`.
- `%ASSETS_JS%` and `%ASSETS_CSS%` are replaced with the hashed names of the combined JavaScript and CSS files.
  Pages that still contain a `%VARIABLE%` are not compressed, the web server replaces it.

## Output

`map` copies every file to `<web_target_dir>/<crc32 of its path>` and writes:

| file | content |
|---|---|
| `<hash>` | the file, gzip compressed if it starts with `1f 8b 08` |
| `<hash>.lnk` | `uint32 mtime`, `uint32 flags << 24 \| original size`, the path |
| `.listings` | for each directory, then each file (sorted by path): `uint32 uid, uint32 size, uint32 original size, uint32 mtime, uint8 flags`, the path, `\n` |
| `.listings.txt` | the same as text: `filename:uid:file-size:original-file-size:modification-time:flags:original-filename` |

All integers are little endian. The flags are `0x01` gzipped and `0x02` directory. The path starts with `/` and is
relative to the web directory, or to the data directory for files outside of it (`data/stk500v1/...`). The mtime and
original size are those of the source files (the newest one and the sum for a combined file). At the start of the
first `map` the hashed files of the previous build are deleted from the web directory.

## Verifying a change

`scripts/tools/webui_baseline.py` of the kfc_fw project builds the image of every environment from a `git archive`
export of the committed state, with fixed timestamps, and stores it with the commits. A change of the builder that
should not change the output is compared against a baseline of the commits before the change:

```
python scripts/tools/webui_baseline.py capture -j 4                       # elf/webui_baseline/<kfc_fw>_<KFCLibrary>/
python scripts/tools/webui_baseline.py compare --baseline elf/webui_baseline/<name> --direct \
    --overlay <KFCLibrary>/KFCWebBuilder=lib/KFCLibrary/KFCWebBuilder --overlay KFCWebBuilder.yaml=KFCWebBuilder.yaml
```

`compare` rebuilds the commits of the baseline with the files of the working tree copied over the export and
reports every image, file list and define list that differs. `--direct` runs the builder with the recorded
defines instead of `pio run -t buildfs` (faster, the same result). The images are only reproducible with the same
tools (uglify-js, the jars and the zopfli version).

## Differences to the PHP builder

- YAML instead of JSON with comments; the `php_eval`/`php_class` commands are the internal commands `assign-name`,
  `map` and `publish-dir`; `gzip` is an internal command that compresses with zopfli instead of `gzip -9`
  (smaller files); the `spiffs` section is `filesystem` and the branch is
  `littlefs` (SPIFFS images are not supported).
- Expressions are tokenized: a name is no longer replaced inside a longer one (`IOT_WEATHER_STATION` inside
  `IOT_WEATHER_STATION2`). All expressions of the project evaluate as before.
- A processor with several `branch` entries checks all of them (PHP only checked the first one).
- The package hash includes the defines, the tool versions and the builder itself.
