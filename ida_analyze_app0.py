# IDA Python script: ida_analyze_app0.py
# Usage (headless):
# ida64.exe -A -S"ida_analyze_app0.py" "path\\to\\app0.bin"

import idaapi
import idc
import idautils
import ida_auto
import os

input_file = idc.get_input_file_path()
if not input_file:
    print("No input file path available. Run this script with the target file open or use -S with IDA CLI.")
    raise SystemExit(1)

out_dir = os.path.dirname(input_file)
print("Input:", input_file)
print("Output dir:", out_dir)

# Let IDA finish autoanalysis
ida_auto.auto_wait()

# Try set processor to riscv (ESP32-C3 is RISC-V). If unsuccessful, continue.
try:
    idaapi.set_processor_type("riscv", idaapi.SETPROC_ALL)
except Exception:
    pass

ida_auto.auto_wait()

# Dump segments
with open(os.path.join(out_dir, "segments.txt"), "w", encoding="utf-8") as segf:
    for seg_ea in idautils.Segments():
        segname = idc.get_segm_name(seg_ea)
        segf.write("0x%08X-0x%08X %s\n" % (seg_ea, idc.get_segm_end(seg_ea), segname))

# Dump functions (ea + name + size)
with open(os.path.join(out_dir, "functions.txt"), "w", encoding="utf-8") as ff:
    for func_ea in idautils.Functions():
        func_name = idc.get_func_name(func_ea)
        func_end = idc.get_func_attr(func_ea, idc.FUNCATTR_END)
        size = func_end - func_ea if func_end else 0
        ff.write("0x%08X\t%s\t0x%X\n" % (func_ea, func_name, size))

# Dump strings
try:
    import ida_nalt
    import ida_lines
    sfile = open(os.path.join(out_dir, "strings.txt"), "w", encoding="utf-8")
    for s in idautils.Strings():
        try:
            sfile.write("0x%08X\t%s\n" % (s.ea, str(s)))
        except Exception:
            sfile.write("0x%08X\t<unprintable>\n" % s.ea)
    sfile.close()
except Exception:
    print("ida_nalt.Strings() not available or failed; skipping strings output.")

# Dump disassembly (head-level) — only instructions and comments
with open(os.path.join(out_dir, "full_disasm.asm"), "w", encoding="utf-8") as d:
    for head in idautils.Heads():
        try:
            if idc.is_code(idc.get_full_flags(head)):
                dis = idc.generate_disasm_line(head, 0)
                cmt = idc.get_cmt(head, False) or ""
                d.write("0x%08X:\t%s\t;%s\n" % (head, dis, cmt))
        except Exception:
            continue

print("Analysis finished. Outputs: segments.txt, functions.txt, strings.txt (if available), full_disasm.asm")
