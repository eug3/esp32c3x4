# IDA Python script for ESP32-C3 (RISC-V) firmware: ida_analyze_app0_riscv.py
# Usage (headless):
# ida64.exe -A -S"ida_analyze_app0_riscv.py" "d:\\esp32c3x4\\app0.bin"
# Note: For raw binary `app0.bin` (ESP32 partition image) prefer to load it in IDA
# as a "Binary file" with the loading address set to 0x10000 (ESP-IDF default).

import idaapi
import idc
import idautils
import ida_auto
import os

input_file = idc.get_input_file_path()
if not input_file:
    print("No input file path available. Open the target file in IDA or use -S with the file path.")
    raise SystemExit(1)

out_dir = os.path.dirname(input_file)
print("Input:", input_file)
print("Output dir:", out_dir)

# Set processor to riscv (ESP32-C3 uses RISC-V)
try:
    idaapi.set_processor_type("riscv", idaapi.SETPROC_ALL)
    print("Processor set to RISC-V (requested).")
except Exception as e:
    print("Failed to set processor to riscv:", e)

# Let IDA finish autoanalysis (if already loaded) or trigger it
ida_auto.auto_wait()

# Check segments and warn if load address looks wrong
segs = list(idautils.Segments())
if not segs:
    print("No segments detected. Make sure you loaded the raw binary in IDA as a Binary file and set the base address (0x10000 recommended).")

min_segm = min(segs) if segs else None
if min_segm is not None:
    print("Existing segment start: 0x%X" % min_segm)
    if min_segm == 0:
        print("Warning: segment starts at 0x0 — raw binary might have been loaded at 0x0.\nPlease reload as 'Binary file' with loading address 0x10000, or rebase the program in IDA to 0x10000 before running the script.")

# Run analysis pass again
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
    sfile = open(os.path.join(out_dir, "strings.txt"), "w", encoding="utf-8")
    for s in idautils.Strings():
        try:
            sfile.write("0x%08X\t%s\n" % (s.ea, str(s)))
        except Exception:
            sfile.write("0x%08X\t<unprintable>\n" % s.ea)
    sfile.close()
except Exception as e:
    print("Strings extraction failed:", e)

# Dump disassembly (address + disasm + comment)
with open(os.path.join(out_dir, "full_disasm.asm"), "w", encoding="utf-8") as d:
    for head in idautils.Heads():
        try:
            if idc.is_code(idc.get_full_flags(head)):
                dis = idc.generate_disasm_line(head, 0)
                cmt = idc.get_cmt(head, False) or ""
                d.write("0x%08X:\t%s\t;%s\n" % (head, dis, cmt))
        except Exception:
            continue

# Optional: export cross-references for top functions
with open(os.path.join(out_dir, "xrefs.txt"), "w", encoding="utf-8") as xf:
    for func_ea in idautils.Functions():
        for ref in idautils.CodeRefsTo(func_ea, 0):
            xf.write("0x%08X -> 0x%08X\n" % (ref, func_ea))

# Export symbols (names and addresses)
with open(os.path.join(out_dir, "symbols.txt"), "w", encoding="utf-8") as sf:
    for ea, name in idautils.Names():
        sf.write("0x%08X\t%s\n" % (ea, name))

# Export data segments (non-code)
with open(os.path.join(out_dir, "data_segments.txt"), "w", encoding="utf-8") as df:
    for seg_ea in idautils.Segments():
        seg_name = idc.get_segm_name(seg_ea)
        seg_end = idc.get_segm_end(seg_ea)
        if not idc.is_code(idc.get_full_flags(seg_ea)):  # Non-code segment
            df.write("0x%08X-0x%08X %s (data)\n" % (seg_ea, seg_end, seg_name))

# Export ELF-like headers (if applicable, but for raw binary, basic info)
with open(os.path.join(out_dir, "headers.txt"), "w", encoding="utf-8") as hf:
    hf.write("File: %s\n" % input_file)
    hf.write("Base: 0x%X\n" % idaapi.get_imagebase())
    hf.write("Size: 0x%X\n" % (idc.get_segm_end(max(idautils.Segments())) - idaapi.get_imagebase() if idautils.Segments() else 0))
    hf.write("Processor: %s\n" % idaapi.get_inf_structure().procname)
    hf.write("File type: %s\n" % ("Binary" if idaapi.get_file_type_name() == "Binary file" else idaapi.get_file_type_name()))

print("RISC-V analysis finished. Outputs: segments.txt, functions.txt, strings.txt (if available), full_disasm.asm, xrefs.txt, symbols.txt, data_segments.txt, headers.txt")
print("If segments start at 0x0, reload with base 0x10000 and re-run for correct addresses.")
