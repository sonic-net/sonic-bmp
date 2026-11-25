"""Check the installed Redis collector, its default configuration and symbols."""

import struct
import sys
import tarfile
from pathlib import Path


def build_id(elf):
    # GNU NT_GNU_BUILD_ID uses a four-byte name and a SHA-1 descriptor.
    note = struct.pack("<III", 4, 20, 3) + b"GNU\0"
    start = elf.index(note) + len(note)
    return elf[start:start + 20].hex()


def main(runtime, symbols, configuration):
    with tarfile.open(runtime) as archive:
        files = {entry.name.removeprefix("./"): entry for entry in archive if entry.isfile()}
        assert set(files) == {"usr/bin/openbmpd", "etc/bmp/openbmpd.conf"}, set(files)
        daemon = files["usr/bin/openbmpd"]
        assert daemon.mode == 0o755 and daemon.uid == daemon.gid == 0
        config = files["etc/bmp/openbmpd.conf"]
        assert config.mode == 0o644 and archive.extractfile(config).read() == Path(configuration).read_bytes()
        elf = archive.extractfile(daemon).read()
        assert elf.startswith(b"\x7fELF\x02\x01")
        assert struct.unpack_from("<H", elf, 18)[0] == 62  # EM_X86_64
        ident = build_id(elf)
    with tarfile.open(symbols) as archive:
        symbol = next(entry for entry in archive if entry.name.endswith("/.build-id/" + ident[:2] + "/" + ident[2:] + ".debug"))
        assert build_id(archive.extractfile(symbol).read()) == ident


if __name__ == "__main__":
    main(*sys.argv[1:])
