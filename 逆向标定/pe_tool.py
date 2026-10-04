#!/usr/bin/env python3
"""GTAIV EXE 静态标定工具：PE 信息、VA 读字节、特征码搜索、反汇编。"""
import hashlib
import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

EXES = {
    "107": "/home/user/.doubao/agent_mode/workspace/.sessions/38445262234467842/attachments/GTAIV1.0.7.exe",
    "108": "/home/user/.doubao/agent_mode/workspace/.sessions/38445262234467842/attachments/GTAIV1.0.8.exe",
    "ce":  "/home/user/.doubao/agent_mode/workspace/.sessions/38445262234467842/attachments/GTAIV1.2.0.59.exe",
}

class Exe:
    def __init__(self, tag):
        self.tag = tag
        self.path = EXES[tag]
        self.pe = pefile.PE(self.path, fast_load=True)
        self.pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_RESOURCE']])
        self.image_base = self.pe.OPTIONAL_HEADER.ImageBase
        self.data = open(self.path, "rb").read()

    def va_to_off(self, va):
        return self.pe.get_offset_from_rva(va - self.image_base)

    def read_va(self, va, n):
        off = self.va_to_off(va)
        return self.data[off:off+n]

    def version(self):
        try:
            fi = self.pe.FileInfo[0]
            for entry in fi:
                if hasattr(entry, "StringTable"):
                    for st in entry.StringTable:
                        return {k.decode(): v.decode(errors="replace") for k, v in st.entries.items()}
        except Exception as e:
            return {"error": str(e)}
        return {}

    def sections(self):
        out = []
        for s in self.pe.sections:
            out.append((s.Name.rstrip(b"\x00").decode(errors="replace"),
                        self.image_base + s.VirtualAddress, s.Misc_VirtualSize,
                        s.PointerToRawData, s.SizeOfRawData))
        return out

    def find_pattern(self, pattern):
        """pattern like '8B 3D ? ? ? ?'; returns list of hit VAs (scan executable sections)."""
        toks = pattern.split()
        needle = []
        for t in toks:
            needle.append(None if t == "?" else int(t, 16))
        hits = []
        for s in self.pe.sections:
            if not (s.Characteristics & 0x20000000):  # IMAGE_SCN_MEM_EXECUTE
                continue
            base = s.PointerToRawData
            size = s.SizeOfRawData
            blob = self.data[base:base+size]
            n = len(needle)
            for i in range(len(blob) - n + 1):
                ok = True
                for j, b in enumerate(needle):
                    if b is not None and blob[i+j] != b:
                        ok = False
                        break
                if ok:
                    rva = s.VirtualAddress + i
                    hits.append(self.image_base + rva)
        return hits

    def disasm(self, va, n=40):
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        code = self.read_va(va, n)
        lines = []
        for ins in md.disasm(code, va):
            lines.append(f"{ins.address:08X}  {ins.bytes.hex():<20} {ins.mnemonic} {ins.op_str}")
        return "\n".join(lines)

if __name__ == "__main__":
    for tag in EXES:
        e = Exe(tag)
        h = hashlib.sha256(e.data).hexdigest()[:16]
        print(f"=== {tag}  sha256={h}  ImageBase=0x{e.image_base:08X}  SizeOfImage=0x{e.pe.OPTIONAL_HEADER.SizeOfImage:X}")
        v = e.version()
        for k in ("FileVersion", "ProductVersion", "FileDescription", "ProductName"):
            if k in v:
                print(f"   {k}: {v[k]}")
        for name, va, vsz, raw, rsz in e.sections()[:6]:
            print(f"   sec {name:<8} VA=0x{va:08X} VSize=0x{vsz:X}")
        print()
