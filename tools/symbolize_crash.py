#!/usr/bin/env python3
"""
Symbolize a KallistiOS crash dump against the .elf that produced it.

KOS prints on an unhandled CPU exception (kernel/arch/dreamcast/kernel/irq.c):
    Unhandled exception: PC 8c01xxxx, code 1, evt 00e0
     R0-R7: ........
     R8-R15: ........
     SR ........ PR 8c01xxxx
Paste the whole serial/Flycast log into a file and run:
    python3 symbolize_crash.py battle.elf crash.log
Needs: pip install pyelftools capstone   (or use DreamSDK's sh-elf-addr2line -f -C -i -e battle.elf <addr>)
"""
import re, sys, bisect
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection

EVT = {0x0e0: "address error (read) - misaligned load / bad pointer", 0x100: "address error (write) - misaligned store / bad pointer",
       0x040: "TLB miss (read)", 0x060: "TLB miss (write)", 0x0a0: "TLB protection (read)", 0x0c0: "TLB protection (write)",
       0x180: "illegal instruction (bad jump / corrupted code or stack)", 0x1a0: "illegal slot instruction",
       0x120: "FPU exception", 0x800: "FPU disabled", 0x820: "FPU disabled (slot)", 0x160: "TRAPA"}

def load(elf_path):
    f = ELFFile(open(elf_path, "rb"))
    funcs = []
    for sec in f.iter_sections():
        if isinstance(sec, SymbolTableSection):
            for s in sec.iter_symbols():
                if s["st_info"]["type"] == "STT_FUNC" and s["st_size"]:
                    funcs.append((s["st_value"], s["st_size"], s.name))
    funcs.sort()
    lines = []
    if f.has_dwarf_info():
        dw = f.get_dwarf_info()
        for cu in dw.iter_CUs():
            lp = dw.line_program_for_CU(cu)
            if not lp: continue
            files = lp["file_entry"]; ver = lp.header["version"]
            for e in lp.get_entries():
                st = e.state
                if st and not st.end_sequence:
                    idx = st.file - (0 if ver >= 5 else 1)
                    name = files[idx].name.decode() if 0 <= idx < len(files) else "?"
                    lines.append((st.address, name, st.line))
    lines.sort()
    text = f.get_section_by_name(".text")
    return f, funcs, lines, text

def func_for(funcs, a):
    i = bisect.bisect_right([x[0] for x in funcs], a) - 1
    if i >= 0 and funcs[i][0] <= a < funcs[i][0] + funcs[i][1]:
        return f"{demangle(funcs[i][2])}+0x{a - funcs[i][0]:x}"
    return None

def line_for(lines, a):
    i = bisect.bisect_right([x[0] for x in lines], a) - 1
    return f"{lines[i][1]}:{lines[i][2]}" if i >= 0 else "?"

def demangle(n):
    try:
        import subprocess
        out = subprocess.run(["c++filt", "-_", n], capture_output=True, text=True, timeout=2).stdout.strip()
        return out or n
    except Exception:
        return n

def main():
    elf, log = sys.argv[1], open(sys.argv[2], errors="replace").read()
    f, funcs, lines, text = load(elf)
    lo, hi = text["sh_addr"], text["sh_addr"] + text["sh_size"]
    m = re.search(r"Unhandled exception: PC ([0-9a-fA-F]{8}), code (\d+), evt ([0-9a-fA-F]{4})", log)
    if not m:
        print("No 'Unhandled exception' line found. If the log just stops, the last [diag] line is the failing stage;")
        print("if Flycast itself crashed/hung, check its own log (renderer / TA errors).")
    else:
        pc, evt = int(m.group(1), 16), int(m.group(3), 16)
        print(f"EXCEPTION evt 0x{evt:03x}: {EVT.get(evt, 'see SH7750 manual, exception codes')}")
        print(f"  PC {pc:08x}  {func_for(funcs, pc) or '(not in a known function)'}  {line_for(lines, pc)}")
        try:
            import capstone
            md = capstone.Cs(capstone.CS_ARCH_SH, capstone.CS_MODE_SH4 | capstone.CS_MODE_LITTLE_ENDIAN)
            off = text["sh_offset"] + (pc - 16 - lo)
            f.stream.seek(off); code = f.stream.read(40)
            for ins in md.disasm(code, pc - 16):
                print(f"   {'>>' if ins.address == pc else '  '} {ins.address:08x}: {ins.mnemonic:9s} {ins.op_str}")
        except Exception as ex:
            print(f"  (no disassembly: {ex})")
    pr = re.search(r"PR ([0-9a-fA-F]{8})", log)
    if pr:
        a = int(pr.group(1), 16)
        print(f"  PR {a:08x}  (return address = caller) {func_for(funcs, a) or '?'}  {line_for(lines, a)}")
    regs = re.findall(r"R(?:0-R7|8-R15):((?:\s+[0-9a-fA-F]{8}){8})", log)
    vals = [int(v, 16) for blk in regs for v in blk.split()]
    for i, v in enumerate(vals):
        tag = func_for(funcs, v) if lo <= v < hi else None
        note = f"-> {tag}" if tag else ("  <- not 32-bit aligned pointer?" if 0x8c000000 <= v < 0x8d000000 and v & 3 else "")
        if note: print(f"  R{i:<2} {v:08x} {note}")

main()
