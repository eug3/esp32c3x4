#!/usr/bin/env python3
"""
创建一个测试EPUB OPF文件内容
"""

test_opf_with_namespace = """<?xml version='1.0' encoding='utf-8'?>
<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="uuid_id" version="2.0">
  <metadata xmlns:calibre="http://calibre.kovidgoyal.net/2009/metadata" xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>Test Book</dc:title>
    <dc:creator>Test Author</dc:creator>
  </metadata>
  <manifest>
    <item id='cover.xhtml' href='cover.xhtml' media-type='application/xhtml+xml'/>
    <item id='toc.xhtml' href='toc.xhtml' media-type='application/xhtml+xml'/>
  </manifest>
  <spine toc='ncx'>
    <itemref idref='cover.xhtml' linear='yes'/>
    <itemref idref='toc.xhtml' linear='yes'/>
  </spine>
</package>"""

test_opf_without_spine_end = """<?xml version='1.0' encoding='utf-8'?>
<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="uuid_id" version="2.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>Test Book 2</dc:title>
  </metadata>
  <manifest>
    <item id='ch1' href='ch1.xhtml' media-type='application/xhtml+xml'/>
  </manifest>
  <spine toc='ncx'>
    <itemref idref='ch1' linear='yes'/>
  <manifest>
    <!-- 注意：这里的manifest标签会作为spine的结束边界 -->
  </manifest>
</package>"""

test_opf_spaces_in_attrs = """<?xml version='1.0' encoding='utf-8'?>
<package xmlns = "http://www.idpf.org/2007/opf" unique-identifier = "uuid_id" version = "2.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>Test Book 3</dc:title>
  </metadata>
  <manifest/>
  <spine toc = 'ncx'>
    <itemref idref = 'item1' linear = 'yes'/>
    <itemref idref = 'item2' linear = 'yes'/>
  </spine>
</package>"""

# 测试我们的XML解析器
if __name__ == '__main__':
    import sys
    sys.path.insert(0, '/Users/beijihu/Github/esp32c3x4/c3x4_main_control')
    
    from epub_parser import epub_xml_count_spine_items
    
    # 实际上我们需要在C代码中测试这个
    # 这里只是为了记录测试用例
    print("测试OPF内容:")
    print("1. 带命名空间的标准OPF")
    print("2. 没有spine结束标签的OPF")
    print("3. 属性中带空格的OPF")
