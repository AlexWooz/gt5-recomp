#!/usr/bin/env python3
"""ps3_unlock.py - unlock a PS3 game executable and Sony's firmware file for the PC port, without RPCS3.

    python ps3_unlock.py eboot    <EBOOT.BIN>     <out.elf>           a game's executable (disc or update)
    python ps3_unlock.py firmware <PS3UPDAT.PUP>  <dev_flash folder>  Sony's firmware file, as RPCS3 installs it
    python ps3_unlock.py keys                                         only fetch + check the keys

An EBOOT.BIN that is already a plain ELF is copied as it is. Free-licence NPDRM executables (game
updates) are handled; ones that need a licence file (.rap) and system modules (.sprx) are not.

Options (before or after the command):
    --keys-dir DIR   where the fetched key source files are kept (default: ./ps3_keys next to this file,
                     or $PS3_UNLOCK_KEYS). Files already there are reused when their SHA-256 matches.
    --offline        never download; use only --keys-dir.

No keys are in this file. At run time it downloads two public text files from RPCS3's source on GitHub
at a pinned commit, checks each against the SHA-256 written below, and reads the key constants out of
the text. The code here is written from the published file-format descriptions (psdevwiki: SELF / SCE
header, Certified File, PUP, ustar), not from RPCS3's code.

Exit codes - the installer falls back to RPCS3 on anything but 0:
    0  done
    1  bad command line
    2  KEYS: the key files could not be fetched, or did not match their pinned SHA-256, or did not parse
    3  INPUT: the input is not something this tool can unlock (unknown format / no key for it / bad hash)
    4  OUTPUT: could not write the result
    5  INTERNAL: anything unexpected (a traceback is printed first)
The last line printed on failure always starts with "PS3_UNLOCK_FAIL <code> <KEYS|INPUT|OUTPUT|INTERNAL>:".
Nothing is left at the output path after a failure: the ELF is written as <out>.part and renamed, the
firmware is unpacked into <dev_flash>.part and moved into place only when every package unpacked.

Standard library only. AES: Windows bcrypt.dll, else OpenSSL's libcrypto (ctypes), else pure Python (slow).
"""
import ctypes
import ctypes.util
import hashlib
import hmac
import os
import re
import shutil
import struct
import sys
import time
import urllib.request
import zlib

# --------------------------------------------------------------------------------------------------
# Where the keys come from (public RPCS3 source, pinned). Only these bytes are trusted.
# --------------------------------------------------------------------------------------------------
KEY_COMMIT = "29108e1cb4dc884ff88295831aed790278b7e75b"   # RPCS3 v0.0.37-18154
KEY_FILES = {
    # local name          (path in the repository,              SHA-256 of the exact bytes)
    "key_vault.cpp": ("rpcs3/Crypto/key_vault.cpp", "048b03adde6f79d564d5002afaa32f69c90bcb6ec5b8dc535b631dca5417e8d1"),
    "key_vault.h":   ("rpcs3/Crypto/key_vault.h",   "7e2830a930127ab8b0291e3e940df50329cf486b7fb7566fdb8438648948f54a"),
}
KEY_URL = "https://raw.githubusercontent.com/RPCS3/rpcs3/{commit}/{path}"


class UnlockError(Exception):
    def __init__(self, code, kind, msg):
        super().__init__(msg)
        self.code, self.kind = code, kind


def keys_fail(msg):
    return UnlockError(2, "KEYS", msg)


def input_fail(msg):
    return UnlockError(3, "INPUT", msg)


def fetch_key_sources(keys_dir, offline=False):
    """Return {local name: text}. Every file is checked against its pinned SHA-256."""
    os.makedirs(keys_dir, exist_ok=True)
    out, fetched = {}, 0
    for name, (path, want) in KEY_FILES.items():
        local = os.path.join(keys_dir, name)
        data = None
        if os.path.isfile(local):
            with open(local, "rb") as f:
                data = f.read()
            if hashlib.sha256(data).hexdigest() != want:
                data = None
        if data is None:
            if offline:
                raise keys_fail(f"{name} is not in {keys_dir} (or differs from the pinned file) and --offline was given")
            url = KEY_URL.format(commit=KEY_COMMIT, path=path)
            try:
                req = urllib.request.Request(url, headers={"User-Agent": "ps3-unlock"})
                with urllib.request.urlopen(req, timeout=60) as r:
                    data = r.read()
            except Exception as e:  # network, TLS, HTTP errors
                raise keys_fail(f"could not download {url}: {e}")
            got = hashlib.sha256(data).hexdigest()
            if got != want:
                raise keys_fail(f"{url} has SHA-256 {got}, expected {want}")
            fetched += len(data)
            with open(local, "wb") as f:
                f.write(data)
        out[name] = data.decode("utf-8", "replace")
    return out, fetched


# Program types of a SELF (psdevwiki "SELF - SPRX", program identification header)
PROGRAM_TYPES = {"KEY_LV0": 1, "KEY_LV1": 2, "KEY_LV2": 3, "KEY_APP": 4, "KEY_ISO": 5,
                 "KEY_LDR": 6, "KEY_UNK7": 7, "KEY_NPDRM": 8}


class Keys:
    """The constants read out of the fetched text: SELF keysets plus a few fixed keys."""

    def __init__(self, texts):
        cpp, hdr = texts["key_vault.cpp"], texts["key_vault.h"]
        self.selfkeys = []          # (program type, version start, version end, revision, erk, riv)
        rx = re.compile(r'emplace_back\(\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(KEY_\w+)\s*,'
                        r'\s*"([0-9A-Fa-f]*)"\s*,\s*"([0-9A-Fa-f]*)"')
        for m in rx.finditer(cpp):
            ptype = PROGRAM_TYPES.get(m.group(4))
            if ptype is None:
                raise keys_fail(f"unknown key type {m.group(4)}")
            erk, riv = bytes.fromhex(m.group(5)), bytes.fromhex(m.group(6))
            if len(erk) != 32 or len(riv) != 16:
                raise keys_fail("a SELF keyset has the wrong length")
            self.selfkeys.append((ptype, int(m.group(1), 16), int(m.group(2), 16), int(m.group(3), 16), erk, riv))
        if len(self.selfkeys) != cpp.count("emplace_back(") or len(self.selfkeys) < 50:
            raise keys_fail("could not read every SELF keyset from key_vault.cpp")
        self.arrays = {}
        for m in re.finditer(r'constexpr\s+u8\s+(\w+)\s*\[[^\]]*\]\s*=\s*\{(.*?)\};', hdr, re.S):
            body = re.sub(r'//[^\n]*', '', m.group(2))
            self.arrays[m.group(1)] = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', body))
        need = {"SCEPKG_ERK": 32, "SCEPKG_RIV": 16, "NP_KLIC_FREE": 16, "NP_KLIC_KEY": 16, "PUP_KEY": 64}
        for n, ln in need.items():
            if len(self.arrays.get(n, b"")) != ln:
                raise keys_fail(f"could not read {n} from key_vault.h")

    def find_self_key(self, ptype, revision):
        """Keyset of a game executable: application (4) or NPDRM (8) program, picked by key revision."""
        for (t, _v0, _v1, rev, erk, riv) in self.selfkeys:
            if t == ptype and rev == revision:
                return erk, riv
        return None


# --------------------------------------------------------------------------------------------------
# AES (ECB primitives; CBC and CTR are built on them here)
# --------------------------------------------------------------------------------------------------
class _BCrypt:
    name = "Windows bcrypt"

    def __init__(self):
        self.b = ctypes.WinDLL("bcrypt.dll")
        self.alg = ctypes.c_void_p()
        if self.b.BCryptOpenAlgorithmProvider(ctypes.byref(self.alg), ctypes.c_wchar_p("AES"), None, 0):
            raise OSError("BCryptOpenAlgorithmProvider")
        mode = ctypes.create_unicode_buffer("ChainingModeECB")
        if self.b.BCryptSetProperty(self.alg, ctypes.c_wchar_p("ChainingMode"), mode, ctypes.sizeof(mode), 0):
            raise OSError("BCryptSetProperty")
        self.b.BCryptEncrypt.argtypes = self.b.BCryptDecrypt.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_ulong, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong,
            ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.c_ulong]

    def _run(self, key, data, enc):
        h = ctypes.c_void_p()
        kb = ctypes.create_string_buffer(key, len(key))
        if self.b.BCryptGenerateSymmetricKey(self.alg, ctypes.byref(h), None, 0, kb, len(key), 0):
            raise OSError("BCryptGenerateSymmetricKey")
        try:
            out = ctypes.create_string_buffer(len(data))
            n = ctypes.c_ulong()
            fn = self.b.BCryptEncrypt if enc else self.b.BCryptDecrypt
            if fn(h, data, len(data), None, None, 0, out, len(data), ctypes.byref(n), 0):
                raise OSError("BCryptEncrypt/Decrypt")
            return out.raw[:n.value]
        finally:
            self.b.BCryptDestroyKey(h)

    def ecb_encrypt(self, key, data):
        return self._run(key, data, True)

    def ecb_decrypt(self, key, data):
        return self._run(key, data, False)


class _OpenSSL:
    name = "OpenSSL libcrypto"

    def __init__(self):
        path = ctypes.util.find_library("crypto")
        names = [path] if path else []
        names += ["libcrypto.so.3", "libcrypto.so.1.1", "libcrypto.dylib"]
        if os.name == "nt":                            # the libcrypto python.org's Windows builds ship with
            import glob
            for d in (os.path.join(sys.base_prefix, "DLLs"), sys.base_prefix, os.path.dirname(sys.executable)):
                names += sorted(glob.glob(os.path.join(d, "libcrypto*.dll")))
        last = None
        for n in names:
            try:
                self.c = ctypes.CDLL(n)
                break
            except OSError as e:
                last = e
        else:
            raise OSError(f"libcrypto not found ({last})")
        c = self.c
        c.EVP_CIPHER_CTX_new.restype = ctypes.c_void_p
        c.EVP_CIPHER_CTX_free.argtypes = [ctypes.c_void_p]
        for f in ("EVP_aes_128_ecb", "EVP_aes_256_ecb"):
            getattr(c, f).restype = ctypes.c_void_p
        c.EVP_CipherInit_ex.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p,
                                        ctypes.c_char_p, ctypes.c_int]
        c.EVP_CIPHER_CTX_set_padding.argtypes = [ctypes.c_void_p, ctypes.c_int]
        c.EVP_CipherUpdate.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_int),
                                       ctypes.c_char_p, ctypes.c_int]

    def _run(self, key, data, enc):
        c = self.c
        ctx = c.EVP_CIPHER_CTX_new()
        try:
            cipher = c.EVP_aes_256_ecb() if len(key) == 32 else c.EVP_aes_128_ecb()
            if c.EVP_CipherInit_ex(ctx, cipher, None, key, None, 1 if enc else 0) != 1:
                raise OSError("EVP_CipherInit_ex")
            c.EVP_CIPHER_CTX_set_padding(ctx, 0)
            out = ctypes.create_string_buffer(len(data) + 32)
            res, pos, step = [], 0, 1 << 28
            while pos < len(data):
                chunk = data[pos:pos + step]
                n = ctypes.c_int()
                if c.EVP_CipherUpdate(ctx, out, ctypes.byref(n), chunk, len(chunk)) != 1:
                    raise OSError("EVP_CipherUpdate")
                res.append(out.raw[:n.value])
                pos += step
            return b"".join(res)
        finally:
            c.EVP_CIPHER_CTX_free(ctx)

    def ecb_encrypt(self, key, data):
        return self._run(key, data, True)

    def ecb_decrypt(self, key, data):
        return self._run(key, data, False)


class _PurePython:
    """FIPS-197 AES, table driven. Correct but slow (a large firmware takes many minutes)."""
    name = "pure Python (slow)"

    def __init__(self):
        sbox = [0] * 256
        p = q = 1
        while True:                                    # build the S-box from GF(2^8) inverses
            p = p ^ ((p << 1) & 0xFF) ^ (0x1B if p & 0x80 else 0)
            q ^= q << 1
            q ^= q << 2
            q ^= q << 4
            q &= 0xFF
            if q & 0x80:
                q ^= 0x09
            x = q ^ ((q << 1 | q >> 7) & 0xFF) ^ ((q << 2 | q >> 6) & 0xFF) ^ ((q << 3 | q >> 5) & 0xFF) ^ ((q << 4 | q >> 4) & 0xFF)
            sbox[p] = x ^ 0x63
            if p == 1:
                break
        sbox[0] = 0x63
        inv = [0] * 256
        for i, v in enumerate(sbox):
            inv[v] = i
        self.sbox, self.inv = sbox, inv

        def mul(a, b):
            r = 0
            while b:
                if b & 1:
                    r ^= a
                a = ((a << 1) ^ 0x1B) & 0xFF if a & 0x80 else a << 1
                b >>= 1
            return r
        self.te = [[0] * 256 for _ in range(4)]
        self.td = [[0] * 256 for _ in range(4)]
        for i in range(256):
            s, t = sbox[i], inv[i]
            e = (mul(s, 2) << 24) | (s << 16) | (s << 8) | mul(s, 3)
            d = (mul(t, 14) << 24) | (mul(t, 9) << 16) | (mul(t, 13) << 8) | mul(t, 11)
            for r in range(4):
                self.te[r][i] = ((e >> (8 * r)) | (e << (32 - 8 * r))) & 0xFFFFFFFF
                self.td[r][i] = ((d >> (8 * r)) | (d << (32 - 8 * r))) & 0xFFFFFFFF
        self._cache = {}

    def _expand(self, key):
        if key in self._cache:
            return self._cache[key]
        nk = len(key) // 4
        nr = nk + 6
        w = list(struct.unpack(">%dI" % nk, key))
        rcon = 1
        sb = self.sbox
        for i in range(nk, 4 * (nr + 1)):
            t = w[i - 1]
            if i % nk == 0:
                t = ((t << 8) | (t >> 24)) & 0xFFFFFFFF
                t = (sb[t >> 24] << 24) | (sb[(t >> 16) & 255] << 16) | (sb[(t >> 8) & 255] << 8) | sb[t & 255]
                t ^= rcon << 24
                rcon = ((rcon << 1) ^ 0x11B) if rcon & 0x80 else rcon << 1
            elif nk > 6 and i % nk == 4:
                t = (sb[t >> 24] << 24) | (sb[(t >> 16) & 255] << 16) | (sb[(t >> 8) & 255] << 8) | sb[t & 255]
            w.append(w[i - nk] ^ t)
        ek = [w[4 * r:4 * r + 4] for r in range(nr + 1)]
        # decryption schedule (equivalent inverse cipher)
        dk = [ek[nr]]
        for r in range(nr - 1, 0, -1):
            row = []
            for x in ek[r]:
                row.append(self.td[0][sb[x >> 24]] ^ self.td[1][sb[(x >> 16) & 255]] ^
                           self.td[2][sb[(x >> 8) & 255]] ^ self.td[3][sb[x & 255]])
            dk.append(row)
        dk.append(ek[0])
        self._cache[key] = (nr, ek, dk)
        return self._cache[key]

    def _blocks(self, key, data, enc):
        nr, ek, dk = self._expand(key)
        ks = ek if enc else dk
        t0, t1, t2, t3 = self.te if enc else self.td
        sb = self.sbox if enc else self.inv
        out = bytearray(len(data))
        unpack, pack = struct.unpack_from, struct.pack_into
        for off in range(0, len(data), 16):
            s0, s1, s2, s3 = unpack(">4I", data, off)
            k = ks[0]
            s0 ^= k[0]; s1 ^= k[1]; s2 ^= k[2]; s3 ^= k[3]
            for r in range(1, nr):
                k = ks[r]
                if enc:
                    a0 = t0[s0 >> 24] ^ t1[(s1 >> 16) & 255] ^ t2[(s2 >> 8) & 255] ^ t3[s3 & 255] ^ k[0]
                    a1 = t0[s1 >> 24] ^ t1[(s2 >> 16) & 255] ^ t2[(s3 >> 8) & 255] ^ t3[s0 & 255] ^ k[1]
                    a2 = t0[s2 >> 24] ^ t1[(s3 >> 16) & 255] ^ t2[(s0 >> 8) & 255] ^ t3[s1 & 255] ^ k[2]
                    a3 = t0[s3 >> 24] ^ t1[(s0 >> 16) & 255] ^ t2[(s1 >> 8) & 255] ^ t3[s2 & 255] ^ k[3]
                else:
                    a0 = t0[s0 >> 24] ^ t1[(s3 >> 16) & 255] ^ t2[(s2 >> 8) & 255] ^ t3[s1 & 255] ^ k[0]
                    a1 = t0[s1 >> 24] ^ t1[(s0 >> 16) & 255] ^ t2[(s3 >> 8) & 255] ^ t3[s2 & 255] ^ k[1]
                    a2 = t0[s2 >> 24] ^ t1[(s1 >> 16) & 255] ^ t2[(s0 >> 8) & 255] ^ t3[s3 & 255] ^ k[2]
                    a3 = t0[s3 >> 24] ^ t1[(s2 >> 16) & 255] ^ t2[(s1 >> 8) & 255] ^ t3[s0 & 255] ^ k[3]
                s0, s1, s2, s3 = a0, a1, a2, a3
            k = ks[nr]
            if enc:
                o = (s0, s1, s2, s3, s1, s2, s3, s0, s2, s3, s0, s1, s3, s0, s1, s2)
            else:
                o = (s0, s3, s2, s1, s1, s0, s3, s2, s2, s1, s0, s3, s3, s2, s1, s0)
            words = []
            for j in range(4):
                a, b, c, d = o[4 * j:4 * j + 4]
                words.append(((sb[a >> 24] << 24) | (sb[(b >> 16) & 255] << 16) | (sb[(c >> 8) & 255] << 8) | sb[d & 255]) ^ k[j])
            pack(">4I", out, off, *words)
        return bytes(out)

    def ecb_encrypt(self, key, data):
        return self._blocks(key, data, True)

    def ecb_decrypt(self, key, data):
        return self._blocks(key, data, False)


def make_aes():
    force = os.environ.get("PS3_UNLOCK_AES", "")       # bcrypt | openssl | pure (for testing)
    order = {"bcrypt": (_BCrypt,), "openssl": (_OpenSSL,), "pure": ()}.get(force)
    if order is None:
        order = (_BCrypt, _OpenSSL) if os.name == "nt" else (_OpenSSL,)
    for cls in order:
        try:
            a = cls()
            if a.ecb_encrypt(bytes(16), bytes(16)).hex() == "66e94bd4ef8a2c3b884cfa59ca342b2e":  # FIPS-197 check
                return a
        except Exception:
            pass
    return _PurePython()


def _xor(a, b):
    n = len(a)
    return (int.from_bytes(a, "little") ^ int.from_bytes(b[:n], "little")).to_bytes(n, "little")


def cbc_decrypt(aes, key, iv, data):
    plain = aes.ecb_decrypt(key, data)
    return _xor(plain, iv + data[:-16]) if data else b""


def ctr_crypt(aes, key, iv, data):
    """AES-CTR with a 128-bit big-endian counter starting at iv."""
    if not data:
        return b""
    n = (len(data) + 15) // 16
    c = int.from_bytes(iv, "big")
    mask = (1 << 128) - 1
    stream = bytearray()
    step = 1 << 16
    for base in range(0, n, step):
        cnt = min(step, n - base)
        ctr = b"".join(((c + base + i) & mask).to_bytes(16, "big") for i in range(cnt))
        stream += aes.ecb_encrypt(key, ctr)
    return _xor(data, stream)


# --------------------------------------------------------------------------------------------------
# Certified file (SCE header) - common to SELF and to the firmware's update packages
# --------------------------------------------------------------------------------------------------
def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def u64(b, o):
    return struct.unpack_from(">Q", b, o)[0]


class CertifiedFile:
    """SCE header (0x20), encrypted metadata info (0x40), metadata header (0x20), section headers (0x30
    each), then the data keys. psdevwiki: "Certified File"."""

    def __init__(self, data):
        if data[:4] != b"SCE\0":
            raise input_fail("not an SCE (signed / encrypted) file")
        self.data = data
        self.version = u32(data, 4)
        self.key_revision = struct.unpack_from(">H", data, 8)[0]
        self.category = struct.unpack_from(">H", data, 10)[0]      # 1 SELF, 2 RVK, 3 PKG, 4 SPP
        self.meta_offset = u32(data, 12)
        self.header_len = u64(data, 16)
        self.data_len = u64(data, 24)

    def is_debug(self):
        return (self.key_revision & 0x8000) != 0

    def open_metadata(self, aes, erk, riv, pre=None):
        """Decrypt the metadata. `pre` optionally strips an extra layer (NPDRM) from the 0x40-byte info."""
        d = self.data
        start = 0x20 + self.meta_offset
        info = d[start:start + 0x40]
        hdrs = d[start + 0x40:self.header_len]
        if not self.is_debug():
            if pre:
                info = pre(info)
            info = cbc_decrypt(aes, erk, riv, info)
        key, key_pad, iv, iv_pad = info[0:16], info[16:32], info[32:48], info[48:64]
        if key_pad != bytes(16) or iv_pad != bytes(16):   # both pads are all zero in a good file
            raise input_fail("metadata did not decrypt (no key for this file, or a damaged file)")
        hdrs = ctr_crypt(aes, key, iv, hdrs)
        sig_len, _unk, nsec, nkeys, opt_size = struct.unpack_from(">QIIII", hdrs, 0)
        if nsec > 4096 or 0x20 + nsec * 0x30 + nkeys * 0x10 > len(hdrs):
            raise input_fail("metadata did not decrypt (section table out of range)")
        self.sections = []
        for i in range(nsec):
            off, size, typ, pidx, hashed, sha_idx, enc, kidx, ividx, comp = struct.unpack_from(">QQIIIIIIII", hdrs, 0x20 + 0x30 * i)
            self.sections.append(dict(offset=off, size=size, type=typ, index=pidx, enc=enc, key=kidx, iv=ividx, comp=comp))
        kstart = 0x20 + 0x30 * nsec
        self.keys = [hdrs[kstart + 16 * j:kstart + 16 * j + 16] for j in range(nkeys)]

    def section_plain(self, aes, s):
        raw = self.data[s["offset"]:s["offset"] + s["size"]]
        if len(raw) != s["size"]:
            raise input_fail("file is shorter than its section table says")
        if s["enc"] == 3:
            if s["key"] >= len(self.keys) or s["iv"] >= len(self.keys):
                raise input_fail("section key index out of range")
            raw = ctr_crypt(aes, self.keys[s["key"]], self.keys[s["iv"]], raw)
        return raw


# --------------------------------------------------------------------------------------------------
# SELF -> ELF
# --------------------------------------------------------------------------------------------------
def self_to_elf(data, keys, aes):
    if data[:4] == b"\x7fELF":
        return data                                    # already plain
    if len(data) < 0x70 or data[:4] != b"SCE\0":
        raise input_fail("not a SELF or ELF file")
    cf = CertifiedFile(data)
    if cf.category != 1:
        raise input_fail(f"SCE file of category {cf.category}, not a SELF")
    # Extended header (psdevwiki "SELF - SPRX"): offsets of the parts that are stored in the clear
    (_hdr_type, appinfo_off, elf_off, phdr_off, shdr_off, _segext_off, _ver_off,
     ctrl_off, ctrl_size) = struct.unpack_from(">9Q", data, 0x20)
    auth_id, vendor, ptype, sce_version = struct.unpack_from(">QIIQ", data, appinfo_off)
    e_type = struct.unpack_from(">H", data, elf_off + 0x10)[0]
    if ptype not in (4, 8) or e_type != 2:
        # Only game executables: application or NPDRM programs whose ELF is an executable (ET_EXEC),
        # the kind proven byte for byte against RPCS3. Modules (.sprx, ELF type PRX) and system
        # programs are refused: the port uses the firmware's files as they are, still locked.
        raise input_fail(f"not a game executable (program type {ptype}, ELF type 0x{e_type:x}); only EBOOT.BIN files are handled")
    elf32 = data[elf_off + 4] == 1
    if elf32:
        ehsize, phentsize, shentsize = 0x34, 0x20, 0x28
        phnum, shnum = struct.unpack_from(">H", data, elf_off + 0x2C)[0], struct.unpack_from(">H", data, elf_off + 0x30)[0]
        phoff, shoff = u32(data, elf_off + 0x1C), u32(data, elf_off + 0x20)
        def ph(i):
            p = phdr_off + i * phentsize
            return u32(data, p + 4), u32(data, p + 16)          # p_offset, p_filesz
    else:
        ehsize, phentsize, shentsize = 0x40, 0x38, 0x40
        phnum, shnum = struct.unpack_from(">H", data, elf_off + 0x38)[0], struct.unpack_from(">H", data, elf_off + 0x3C)[0]
        phoff, shoff = u64(data, elf_off + 0x20), u64(data, elf_off + 0x28)
        def ph(i):
            p = phdr_off + i * phentsize
            return u64(data, p + 8), u64(data, p + 32)
    # NPDRM layer: a control-info entry of type 3 holds the NPD header
    npd_license = None
    pos, end = ctrl_off, ctrl_off + ctrl_size
    while pos < end:
        ctype, csize = struct.unpack_from(">II", data, pos)
        if csize == 0:
            break
        if ctype == 3 and data[pos + 0x10:pos + 0x14] == b"NPD\0":
            npd_license = u32(data, pos + 0x18)
        pos += csize
    pre = None
    if npd_license is not None:
        if npd_license != 3:
            raise input_fail("NPDRM executable that needs a licence file (RAP); only free-licence ones are supported")
        klic = aes.ecb_decrypt(keys.arrays["NP_KLIC_KEY"], keys.arrays["NP_KLIC_FREE"])
        pre = lambda info: cbc_decrypt(aes, klic, bytes(16), info)
    ks = keys.find_self_key(ptype, cf.key_revision)
    if ks is None:
        raise input_fail(f"no key for program type {ptype}, key revision 0x{cf.key_revision:x}")
    cf.open_metadata(aes, ks[0], ks[1], pre)
    # The ELF: its header, the program header table at e_phoff, each segment at its p_offset, then the
    # section header table at e_shoff (when the SELF carries one).
    out = bytearray()

    def put(off, blob):
        if len(out) < off + len(blob):
            out.extend(bytes(off + len(blob) - len(out)))
        out[off:off + len(blob)] = blob
    put(0, data[elf_off:elf_off + ehsize])
    put(phoff, data[phdr_off:phdr_off + phnum * phentsize])
    for s in cf.sections:
        if s["type"] != 2:                              # 2 = a program segment
            continue
        if s["index"] >= phnum:
            raise input_fail("segment index out of range")
        p_offset, p_filesz = ph(s["index"])
        blob = cf.section_plain(aes, s)
        if s["comp"] == 2:
            try:
                blob = zlib.decompressobj().decompress(blob, p_filesz) if p_filesz else b""
            except zlib.error as e:
                raise input_fail(f"segment {s['index']} did not decompress ({e}): wrong key?")
            blob = blob.ljust(p_filesz, b"\0")
        put(p_offset, blob)
    if shdr_off:
        put(shoff, data[shdr_off:shdr_off + shnum * shentsize])
    return bytes(out)


# --------------------------------------------------------------------------------------------------
# PUP -> dev_flash
# --------------------------------------------------------------------------------------------------
def read_pup(path, keys, check_hashes=True):
    with open(path, "rb") as f:
        pup = f.read()
    if pup[:8] != b"SCEUF\0\0\0":
        raise input_fail("not a PUP file")
    _pkg_ver, _img_ver, count, hdr_len, data_len = struct.unpack_from(">5Q", pup, 8)
    if len(pup) < hdr_len + data_len or not count or 0x30 + count * 0x40 > len(pup):
        raise input_fail("PUP file is incomplete")
    entries = {}
    for i in range(count):
        eid, off, ln = struct.unpack_from(">QQQ", pup, 0x30 + 0x20 * i)
        if off + ln > len(pup):
            raise input_fail("PUP file table points outside the file")
        hoff = 0x30 + 0x20 * count + 0x20 * i
        heid, digest = u64(pup, hoff), pup[hoff + 8:hoff + 28]
        if check_hashes and hmac.new(keys.arrays["PUP_KEY"], pup[off:off + ln], hashlib.sha1).digest() != digest:
            raise input_fail(f"PUP entry 0x{eid:x} fails its hash check (damaged download?)")
        entries[eid] = pup[off:off + ln]
    return entries


def tar_entries(blob):
    """ustar reader: returns [(name, type, mtime, data)] in archive order."""
    out, pos = [], 0
    while pos + 512 <= len(blob):
        h = blob[pos:pos + 512]
        if h[257:262] != b"ustar":
            break
        name = h[0:100].split(b"\0")[0]
        prefix = h[345:500].split(b"\0")[0]
        if not name and not prefix:
            break
        size = int(h[124:136].split(b"\0")[0].strip() or b"0", 8)
        mtime = int(h[136:148].split(b"\0")[0].strip() or b"0", 8)
        typ = h[156:157]
        full = (prefix + b"/" + name) if prefix else name
        out.append((full.decode("utf-8"), typ, mtime, blob[pos + 512:pos + 512 + size]))
        pos += 512 + (size + 511) // 512 * 512
    return out


def install_firmware(pup_path, dest, keys, aes, log=print):
    entries = read_pup(pup_path, keys)
    version = entries.get(0x100, b"").split(b"\n")[0].decode("ascii", "replace")
    if 0x300 not in entries:
        raise input_fail("PUP has no update_files.tar (entry 0x300)")
    outer = tar_entries(entries[0x300])
    pkgs = [(n, d) for (n, t, m, d) in outer if "dev_flash_" in n]
    if not pkgs:
        raise input_fail("PUP holds no dev_flash_* packages")
    log(f"  firmware {version}: {len(pkgs)} packages")
    erk, riv = keys.arrays["SCEPKG_ERK"], keys.arrays["SCEPKG_RIV"]
    dest = os.path.abspath(dest)
    files = {}                                        # later entries of the same name replace earlier ones
    for name, pkg in pkgs:
        cf = CertifiedFile(pkg)
        cf.open_metadata(aes, erk, riv)
        blob = None
        for s in cf.sections:                          # the package's content: the section holding a ustar archive
            b = cf.section_plain(aes, s)
            if s["comp"] == 2:
                try:
                    b = zlib.decompress(b)
                except zlib.error as e:
                    raise input_fail(f"{name}: a section did not decompress ({e})")
            if b[257:262] == b"ustar":
                blob = b
                break
        if blob is None:
            raise input_fail(f"{name}: no file archive inside the package")
        for (n, t, mtime, d) in tar_entries(blob):
            if not n.startswith("dev_flash/") and n != "dev_flash":
                raise input_fail(f"{name}: unexpected path {n!r}")
            if t not in (b"0", b"\0", b"5"):
                raise input_fail(f"{name}: unsupported tar entry type {t!r} for {n}")
            files[n] = (t, mtime, d)
    # Everything is unpacked into <dest>.part first; only a complete tree is moved to <dest>.
    part = dest.rstrip("\\/") + ".part"
    nfiles = 0
    try:
        if os.path.exists(part):
            shutil.rmtree(part)
        os.makedirs(part)
        for n, (t, mtime, d) in sorted(files.items()):
            rel = n[len("dev_flash"):].strip("/")
            parts = [p for p in rel.split("/") if p]
            if any(p in (".", "..") or ":" in p or "\\" in p for p in parts):
                raise input_fail(f"unsafe path in firmware: {n!r}")
            target = os.path.join(part, *parts) if parts else part
            if t == b"5":
                os.makedirs(target, exist_ok=True)
            else:
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with open(target, "wb") as f:
                    f.write(d)
                os.utime(target, (mtime, mtime))
                nfiles += 1
        if not os.path.exists(dest):
            os.rename(part, dest)
        else:                                          # merge into an existing folder, replacing same-named files
            for dp, dn, fn in os.walk(part):
                tdir = os.path.join(dest, os.path.relpath(dp, part))
                os.makedirs(tdir, exist_ok=True)
                for f in fn:
                    os.replace(os.path.join(dp, f), os.path.join(tdir, f))
            shutil.rmtree(part)
    except OSError as e:
        shutil.rmtree(part, ignore_errors=True)
        raise UnlockError(4, "OUTPUT", f"could not write the firmware files to {dest}: {e}")
    except BaseException:
        shutil.rmtree(part, ignore_errors=True)
        raise
    return version, nfiles


# --------------------------------------------------------------------------------------------------
def main(argv):
    args, keys_dir, offline = [], os.environ.get("PS3_UNLOCK_KEYS") or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "ps3_keys"), False
    it = iter(argv)
    for a in it:
        if a == "--keys-dir":
            keys_dir = next(it, "")
        elif a == "--offline":
            offline = True
        else:
            args.append(a)
    if not args or args[0] not in ("eboot", "firmware", "keys") or len(args) != (1 if args[0] == "keys" else 3):
        print(__doc__)
        return 1
    t0 = time.time()
    try:
        texts, fetched = fetch_key_sources(keys_dir, offline)
        try:
            keys = Keys(texts)
        except UnlockError:
            raise
        except Exception as e:
            raise keys_fail(f"key files did not parse: {e}")
        aes = make_aes()
        print(f"keys: RPCS3 {KEY_COMMIT[:8]}, {len(keys.selfkeys)} SELF keysets"
              f" ({'downloaded %d bytes' % fetched if fetched else 'cached'}); AES: {aes.name}")
        if args[0] == "keys":
            return 0
        src, dst = args[1], args[2]
        if args[0] == "eboot":
            try:
                with open(src, "rb") as f:
                    data = f.read()
            except OSError as e:
                raise input_fail(f"cannot read {src}: {e}")
            elf = self_to_elf(data, keys, aes)
            try:
                os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
                with open(dst + ".part", "wb") as f:
                    f.write(elf)
                os.replace(dst + ".part", dst)
            except OSError as e:
                try:
                    os.remove(dst + ".part")
                except OSError:
                    pass
                raise UnlockError(4, "OUTPUT", f"cannot write {dst}: {e}")
            print(f"done: {dst} ({len(elf)} bytes, SHA-1 {hashlib.sha1(elf).hexdigest().upper()}) in {time.time() - t0:.1f} s")
        else:
            version, n = install_firmware(src, dst, keys, aes)
            print(f"done: firmware {version}, {n} files in {dst} in {time.time() - t0:.1f} s")
        return 0
    except UnlockError as e:
        print(f"PS3_UNLOCK_FAIL {e.code} {e.kind}: {e}", flush=True)
        return e.code
    except Exception as e:                              # a damaged or unexpected file, or a bug here
        import traceback
        traceback.print_exc()
        print(f"PS3_UNLOCK_FAIL 5 INTERNAL: {type(e).__name__}: {e}", flush=True)
        return 5


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
