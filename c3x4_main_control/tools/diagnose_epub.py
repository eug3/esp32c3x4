#!/usr/bin/env python3
"""
诊断EPUB文件的spine结构问题
"""

import sys
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path
import re

def diagnose_epub(epub_path):
    """诊断EPUB文件的结构"""
    epub_path = Path(epub_path)
    
    if not epub_path.exists():
        print(f"错误: EPUB文件不存在: {epub_path}")
        return
    
    print(f"诊断EPUB文件: {epub_path}")
    print(f"文件大小: {epub_path.stat().st_size} 字节")
    print()
    
    try:
        with zipfile.ZipFile(epub_path, 'r') as zf:
            # 列出ZIP内容
            files = zf.namelist()
            print(f"ZIP文件中包含 {len(files)} 个文件:")
            
            # 查找content.opf
            opf_files = [f for f in files if f.endswith('.opf')]
            print(f"  OPF文件: {opf_files}")
            
            # 找到容器文件
            container_xml = None
            if 'META-INF/container.xml' in files:
                container_xml = zf.read('META-INF/container.xml').decode('utf-8', errors='ignore')
                print(f"  Found META-INF/container.xml")
            
            # 从container.xml找到OPF文件路径
            opf_path = None
            if container_xml:
                match = re.search(r'full-path="([^"]+)"', container_xml)
                if match:
                    opf_path = match.group(1)
                    print(f"  OPF路径: {opf_path}")
            
            # 读取OPF文件
            if opf_path and opf_path in files:
                opf_content = zf.read(opf_path).decode('utf-8', errors='ignore')
                diagnose_opf(opf_content, opf_path)
            elif opf_files:
                opf_content = zf.read(opf_files[0]).decode('utf-8', errors='ignore')
                diagnose_opf(opf_content, opf_files[0])
            else:
                print("错误: 找不到OPF文件")
                
    except Exception as e:
        print(f"错误: {e}")
        import traceback
        traceback.print_exc()

def diagnose_opf(opf_content, opf_path):
    """诊断OPF文件的spine结构"""
    print(f"\nOPF文件分析: {opf_path}")
    print(f"OPF文件大小: {len(opf_content)} 字节")
    print()
    
    # 检查spine标签
    if '<spine' in opf_content:
        print("✓ 找到 <spine 标签")
    else:
        print("✗ 未找到 <spine 标签")
    
    if '<opf:spine' in opf_content:
        print("✓ 找到 <opf:spine 标签 (带命名空间)")
    
    # 检查itemref标签
    itemref_count = opf_content.count('<itemref')
    opf_itemref_count = opf_content.count('<opf:itemref')
    
    print(f"找到 {itemref_count} 个 <itemref 标签")
    print(f"找到 {opf_itemref_count} 个 <opf:itemref 标签")
    print()
    
    # 尝试解析XML
    try:
        # 定义命名空间
        namespaces = {
            'opf': 'http://www.idpf.org/2007/opf',
            'dc': 'http://purl.org/dc/elements/1.1/',
        }
        
        root = ET.fromstring(opf_content)
        
        # 查找spine元素
        spine = None
        # 尝试带命名空间的查询
        for ns in ['{http://www.idpf.org/2007/opf}', '']:
            spine = root.find(f'{ns}spine')
            if spine is not None:
                break
        
        if spine is not None:
            print("✓ 成功解析XML中的spine元素")
            # 查找itemref元素
            itemrefs = list(spine)
            print(f"  spine元素包含 {len(itemrefs)} 个子元素")
            
            for i, itemref in enumerate(itemrefs[:5]):
                tag = itemref.tag.replace('{http://www.idpf.org/2007/opf}', '')
                idref = itemref.get('idref', 'N/A')
                linear = itemref.get('linear', 'yes')
                print(f"    [{i}] <{tag} idref='{idref}' linear='{linear}' />")
            
            if len(itemrefs) > 5:
                print(f"    ... 和 {len(itemrefs) - 5} 个其他itemref元素")
        else:
            print("✗ 无法在XML中找到spine元素")
            
            # 打印根元素信息
            print(f"  根元素: {root.tag}")
            print(f"  根元素的子元素:")
            for child in list(root)[:10]:
                print(f"    - {child.tag} ({len(child)} 个子元素)")
    
    except ET.ParseError as e:
        print(f"✗ XML解析错误: {e}")
        print("  OPF文件可能不是有效的XML")
        
        # 手动查找spine标签
        print("\n  手动分析:")
        spine_start = opf_content.find('<spine')
        if spine_start == -1:
            spine_start = opf_content.find('<opf:spine')
        
        if spine_start >= 0:
            spine_end = opf_content.find('</spine>', spine_start)
            if spine_end == -1:
                spine_end = opf_content.find('</opf:spine>', spine_start)
            if spine_end == -1:
                spine_end = spine_start + 500  # 显示前500个字符
            
            spine_content = opf_content[spine_start:spine_end + 50]
            print(f"  spine标签内容片段:")
            print("  " + "-" * 40)
            for line in spine_content.split('\n')[:10]:
                print(f"  {line}")
    
    # 打印OPF的前1000字符
    print("\nOPF文件开头:")
    print("-" * 60)
    print(opf_content[:1000])
    if len(opf_content) > 1000:
        print("... (内容已截断)")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("使用方法: python diagnose_epub.py <epub_file_path>")
        sys.exit(1)
    
    epub_path = sys.argv[1]
    diagnose_epub(epub_path)
