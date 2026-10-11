#
# Author: sascha_lammers@gmx.de
#
# Internal command "map": stores every file under a hashed name in the web directory and writes the index
# that the firmware reads (KFCWebBuilder/include/fs_mapping.h).
#
#   <web dir>/<crc32 of the path>       content of the file (gzipped if it starts with 1f 8b 08)
#   <web dir>/<crc32 of the path>.lnk   <uint32 mtime><uint32 flags << 24 | original size><path>
#   <web dir>/.listings                 directories, then files sorted by path:
#                                       <uint32 uid><uint32 size><uint32 original size><uint32 mtime><uint8 flags><path>\n
#   <web dir>/.listings.txt             the same as text
#
# All integers are little endian, the path is relative to the web directory (or the data directory for files
# outside of it) and starts with "/".
#

import os
import re
import shutil
import struct
import zlib

from . import BuilderError
from .fileutils import remove_matching

FLAGS_GZIPPED = 0x01
FLAGS_DIR = 0x02


def _slashes(path):
    return path.replace('\\', '/')


class Mapper(object):

    def __init__(self, builder):
        config = builder.config
        self.builder = builder
        self.data_dir = config.data_dir
        self.web_dir = config.web_target_dir
        self.listings_file = config.listings_file
        self.verbose = builder.verbose
        self.mapped_files = []
        self.crcs = {}
        if self.web_dir and os.path.isdir(self.web_dir):
            # remove the files of the previous build
            pattern = re.compile(r'^([a-z0-9]{2,8}(?:\.(lnk|gz))?|%s(\.txt)?)$' %
                                 re.escape(os.path.basename(self.listings_file)))
            remove_matching(self.web_dir, pattern)

    def process(self, file):
        target = _slashes(file.target or '')
        web_dir = _slashes(self.web_dir)
        data_dir = _slashes(self.data_dir)
        if target.lower().startswith(web_dir.lower()):
            mapped_file = target[len(web_dir):]
        elif target.lower().startswith(data_dir.lower()):
            mapped_file = target[len(data_dir):]
        else:
            raise BuilderError('%s: Outside data directory %s' % (file.source_str(), target))

        crc = '%08x' % (zlib.crc32(mapped_file.encode('utf-8')) & 0xffffffff)
        if crc in self.crcs:
            raise BuilderError('%s: %s and %s have the same crc' % (file.source_str(), self.crcs[crc], mapped_file))
        out_file = os.path.join(self.web_dir, crc)
        try:
            shutil.copyfile(file.tmp_in, out_file)
        except OSError as e:
            raise BuilderError('%s: Cannot copy %s to %s: %s' % (file.source_str(), file.tmp_in, out_file, e))

        sources = file.source if isinstance(file.source, list) else [file.source]
        mtime = max(int(os.path.getmtime(f)) for f in sources)
        org_size = sum(os.path.getsize(f) for f in sources)

        flags = 0
        with open(file.tmp_in, 'rb') as f:
            if f.read(3) == b'\x1f\x8b\x08':
                flags |= FLAGS_GZIPPED

        with open(out_file + '.lnk', 'wb') as f:
            f.write(struct.pack('<LL', mtime, (flags << 24) | org_size) + mapped_file.encode('utf-8'))

        self.crcs[crc] = mapped_file
        self.mapped_files.append({
            'fs_file': _slashes(out_file[len(self.data_dir):]),
            'mapped_file': mapped_file,
            'original_file_size': org_size,
            'file_size': os.path.getsize(out_file),
            'flags': flags,
            'mtime': mtime,
            'uid': int(crc, 16),
        })
        file.tmp_out = out_file
        return False

    def finish(self):
        files = sorted(self.mapped_files, key=lambda f: f['mapped_file'].encode('utf-8'))
        total_size = 2 + 12 * len(files)
        dirs = {}
        for f in files:
            total_size += f['file_size']
            if self.verbose:
                print('%s => %s%s, size %u/%u, %.2f%% mtime %u' % (
                    f['mapped_file'], f['fs_file'], ', gzipped' if f['flags'] & FLAGS_GZIPPED else '',
                    f['original_file_size'], f['file_size'],
                    f['file_size'] / f['original_file_size'] * 100 if f['original_file_size'] else 0, f['mtime']))
            name = os.path.dirname(f['mapped_file']).replace('\\', '/')
            if name != '/':
                dirs[name] = max(dirs.get(name, 0), f['mtime'])

        listing = bytearray()
        plain = ['filename:uid:file-size:original-file-size:modification-time:flags:original-filename\n']
        for name, mtime in dirs.items():
            listing += struct.pack('<LLLLB', 0, 0, 0, mtime, FLAGS_DIR) + name.encode('utf-8') + b'\n'
            plain.append('%08x:%08x:%08x:%08x:%08x:%02x:%s\n' % (0, 0, 0, 0, mtime, FLAGS_DIR, name))
        for f in files:
            listing += struct.pack('<LLLLB', f['uid'], f['file_size'], f['original_file_size'], f['mtime'],
                                   f['flags']) + f['mapped_file'].encode('utf-8') + b'\n'
            plain.append('%s:%08x:%08x:%08x:%08x:%02x:%s\n' % (f['fs_file'][-8:], f['uid'], f['file_size'],
                                                              f['original_file_size'], f['mtime'], f['flags'],
                                                              f['mapped_file']))
        with open(self.listings_file, 'wb') as fh:
            fh.write(bytes(listing))
        with open(self.listings_file + '.txt', 'wb') as fh:
            fh.write(''.join(plain).encode('utf-8'))

        print('--------------------------------------------')
        print('Total size of the filesystem usage: %u' % total_size)
