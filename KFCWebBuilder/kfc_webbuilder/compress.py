#
# Author: sascha_lammers@gmx.de
#
# Internal command "gzip": gzip compression with zopfli (about 4% smaller than "gzip -9", standard gzip format,
# no file name and no time stamp in the header). zopfli is slow, the results are cached by the hash of the
# content in <tools>/cache/zopfli, so only changed files are compressed again.
#
# If the pip package zopfli cannot be installed, zlib level 9 is used.
#

import hashlib
import os
import struct
import time
import zlib

GZIP_HEADER = b'\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\x03'
CACHE_MAX_AGE = 60 * 86400  # entries not used for 60 days are removed


def compress_zlib(data):
    deflate = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
    body = deflate.compress(data) + deflate.flush()
    return GZIP_HEADER + body + struct.pack('<II', zlib.crc32(data) & 0xffffffff, len(data) & 0xffffffff)


class Compressor(object):

    def __init__(self, zopfli_gzip=None, version=None, cache_dir=None):
        # zopfli_gzip: the module zopfli.gzip, None for zlib
        self.zopfli = zopfli_gzip
        self.version = version if zopfli_gzip else 'zlib %s' % zlib.ZLIB_RUNTIME_VERSION
        self.cache_dir = cache_dir if zopfli_gzip else None
        self.hits = self.misses = 0
        if self.cache_dir:
            os.makedirs(self.cache_dir, exist_ok=True)
            self._prune()

    def _prune(self):
        limit = time.time() - CACHE_MAX_AGE
        for name in os.listdir(self.cache_dir):
            path = os.path.join(self.cache_dir, name)
            try:
                if os.path.getmtime(path) < limit:
                    os.unlink(path)
            except OSError:
                pass

    def compress(self, data):
        if not self.zopfli:
            return compress_zlib(data)
        key = hashlib.sha256(self.version.encode('utf-8') + b'\0' + data).hexdigest()
        path = os.path.join(self.cache_dir, key + '.gz')
        try:
            with open(path, 'rb') as f:
                result = f.read()
            if result.startswith(GZIP_HEADER[:3]):
                os.utime(path)
                self.hits += 1
                return result
        except OSError:
            pass
        self.misses += 1
        result = self.zopfli.compress(data)
        tmp = '%s.%u.tmp' % (path, os.getpid())
        try:
            with open(tmp, 'wb') as f:
                f.write(result)
            os.replace(tmp, path)
        except OSError:
            pass
        return result

    def compress_file(self, src, dst):
        with open(src, 'rb') as f:
            data = f.read()
        with open(dst, 'wb') as f:
            f.write(self.compress(data))
