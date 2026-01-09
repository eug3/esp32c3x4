#!/usr/bin/env python3
import zipfile
import sys
import os

# 检查命令行参数
if len(sys.argv) > 1:
    epub_file = sys.argv[1]
else:
    epub_file = 'examples/绑架游戏 ( 日 东野圭吾, 郑悦) (Z-Library).epub'

if not os.path.exists(epub_file):
    print(f'File not found: {epub_file}')
    sys.exit(1)

with zipfile.ZipFile(epub_file, 'r') as zip_ref:
    # 查找 content.opf
    opf_file = None
    for name in zip_ref.namelist():
        if 'content.opf' in name or name.endswith('.opf'):
            opf_file = name
            break
    
    if opf_file:
        print(f'Found OPF file: {opf_file}')
        print('=' * 60)
        content = zip_ref.read(opf_file).decode('utf-8', errors='ignore')
        
        # 打印完整的 OPF 文件用于调试
        print(f'OPF content length: {len(content)} bytes')
        
        # 查找可能导致解析问题的字符
        print('\nChecking for potential XML parsing issues:')
        
        # 检查不可打印字符
        non_printable = [i for i, c in enumerate(content[:500]) if ord(c) < 32 and c not in '\n\r\t']
        if non_printable:
            print(f'Found non-printable characters at positions: {non_printable[:10]}')
        
        # 检查属性引号问题
        import re
        # 查找可能的属性格式问题
        attr_patterns = re.findall(r'(\w+)=(["\'])([^"\']*?)\2', content[:1000])
        print(f'Sample attributes found: {len(attr_patterns)}')
        if attr_patterns:
            print(f'First few attributes: {attr_patterns[:3]}')
        
        print('\nFull OPF content:')
        print(content)
        print('\n' + '='*60 + '\n')
        
        # 查找 spine 部分
        spine_start = content.find('<spine')
        if spine_start != -1:
            spine_end = content.find('</spine>', spine_start)
            if spine_end == -1:
                spine_end = spine_start + 1000
            else:
                spine_end += 8
            spine_section = content[spine_start:spine_end]
            print('SPINE SECTION:')
            print(spine_section)
            print()
        else:
            print('No <spine> tag found!')
        
        # 查找 manifest 部分
        manifest_start = content.find('<manifest')
        if manifest_start != -1:
            manifest_end = content.find('</manifest>', manifest_start)
            if manifest_end == -1:
                manifest_end = manifest_start + 500
            else:
                manifest_end += 11
            manifest_section = content[manifest_start:manifest_end]
            print('\nMANIFEST SECTION (first 500 chars):')
            print(manifest_section[:500])
            print()
    else:
        print('No OPF file found!')
        print('\nAll files in EPUB:')
        for name in zip_ref.namelist():
            print(f'  {name}')
