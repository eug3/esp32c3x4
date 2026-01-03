#!/usr/bin/env python3
"""
生成 LVGL 内置中文字体脚本

用法:
  python3 gen_chinese_font.py

要求:
  - npm install -g lv_font_conv
  - 需要中文字体文件 (TTF/OTF)
"""

import os
import subprocess
import sys
import struct
import urllib.request

# 配置
FONT_SIZE = 16           # 字体大小
BPP = 1                  # 1位黑白 (适合电子墨水屏)
OUTPUT_BIN = "chinese_16.bin"
OUTPUT_C = "chinese_font.c"
OUTPUT_H = "chinese_font.h"

# 常用汉字 (约500个，涵盖界面常用词)
COMMON_CHARS = """
的一是在不了有和人这中大为上个上我到要他说时来用们生到作地于出就分对成会可主发年动同工也能下过子说产种面而方后多定行学法所民得经十三之进着等部度家电力里如水化高自二理起小物现实量都两体制机当使点从业本去把性好应开它合还因由其些然前外天政四日那社义事平形相全表间样与关各重新线内数正心反你明看原又么利比或但质气第向道命此变条只没结解问意建月公无系军很情者最立代想已通并提直题党程展五果料象员革位入常文总次品式活设及管特件长求老头基资边流身认字导区强示律王集场卫计报身关击杀写候需况养疑难调预属支格更走足论布即德复病奇验激仅靠注虽依班完敌责杨排始练转约未读阿爸八百步北必采菜草层查厂产常车陈晨诚成吃出川传船春辞从担但谈打代带待袋淡弹当刀岛导道得灯等低地弟第电店调定冬东都度短段对多顿饿儿耳二发法反饭范方房防飞粉风否夫服福府父付副盖改感干刚高哥歌格个跟根工更公狗古骨故顾瓜刮官关光广归规果过海含汉好和号喝合河和黑很红后呼户花欢换回婚活火机鸡吉急级挤计记家加假价间件健将江讲交角脚叫教接节结姐界金今进近精九酒久旧救就居句决军开看烤课肯空孔苦快宽来蓝老乐冷离李理连亮凉林零流留六龙楼路绿伦乱罗马买卖满忙毛帽没门美米面眠名明母木拿哪内那南男尼年念鸟牛农弄怒女怕排牌盘跑配朋瓶皮平瓶七期其起汽汽器千钱前强桥且青清请庆秋缺然热人认日容肉如三色沙杀山扇伤商上绍少蛇舍设社申身神生胜师诗石时识史始世市事室视试收手首守书术树双谁水税睡说思四送诉算虽随孙所他她台太谈特提题体天条铁听停通同头土图团推外完玩晚王往忘望伟为位温文闻问我五无午物西息喜系夏鲜香乡项笑写谢心新信星行姓修秀雪寻牙烟颜言眼验羊阳洋养邀要爷也夜页一业阴音引友雨鱼语玉元院月晕云杂再在早造则怎增展站张章招找照者这真正整之知直值职指纸至制治中钟种终周洲猪主住助注抓转装准桌子字总走租嘴最罪作坐做
"""

# 添加标点和 ASCII
ALL_CHARS = COMMON_CHARS + ' \t\n\r!"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'

def get_system_font():
    """获取系统中可用的中文字体"""

    # 尝试下载开源字体 (优先，因为系统字体可能是 .ttc 格式不支持)
    font_file = "NotoSansSC-Regular.otf"
    if os.path.exists(font_file):
        print(f"使用已下载的字体: {font_file}")
        return font_file

    print("正在下载开源字体 Noto Sans SC...")

    try:
        # 使用更可靠的源
        url = "https://github.com/notofonts/noto-cjk/releases/download/Sans2.004/03_NotoSansCJK-OTC.zip"

        print(f"从 {url} 下载...")

        import urllib.request
        import zipfile

        zip_file = "noto_cjk.zip"
        print("下载中...")
        urllib.request.urlretrieve(url, zip_file)

        print("解压中...")
        with zipfile.ZipFile(zip_file, 'r') as zip_ref:
            # 列出所有文件
            found = False
            for name in zip_ref.namelist():
                # 查找简体中文字体
                if 'Simplified' in name and name.endswith('.otf'):
                    # 提取并重命名
                    extracted_name = os.path.basename(name)
                    zip_ref.extract(name, '.')
                    if os.path.exists(extracted_name):
                        os.rename(extracted_name, font_file)
                        print(f"已提取: {extracted_name} -> {font_file}")
                        found = True
                        break
                elif name.endswith('.otf') and 'SC' in name:
                    extracted_name = os.path.basename(name)
                    zip_ref.extract(name, '.')
                    if os.path.exists(extracted_name):
                        os.rename(extracted_name, font_file)
                        print(f"已提取: {extracted_name} -> {font_file}")
                        found = True
                        break

            if not found:
                # 找不到简体，用第一个 otf
                for name in zip_ref.namelist():
                    if name.endswith('.otf'):
                        extracted_name = os.path.basename(name)
                        zip_ref.extract(name, '.')
                        if os.path.exists(extracted_name):
                            os.rename(extracted_name, font_file)
                            print(f"使用: {extracted_name} -> {font_file}")
                            found = True
                            break

        os.remove(zip_file)

        if not os.path.exists(font_file):
            raise Exception("提取字体文件失败")

        print(f"已下载: {font_file}")
        return font_file
    except Exception as e:
        print(f"下载失败: {e}")

    # 回退到系统字体
    font_paths = [
        "/System/Library/Fonts/STHeiti Light.ttf",       # macOS (单字体)
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",  # Linux
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttf",  # Linux
        "C:/Windows/Fonts/msyh.ttf",                     # Windows (微软雅黑)
        "C:/Windows/Fonts/simsun.ttf",                   # Windows (宋体)
    ]

    for path in font_paths:
        if os.path.exists(path):
            print(f"找到系统字体: {path}")
            return path

    print("错误: 未找到可用的中文字体!")
    print("请手动下载中文字体 (TTF/OTF) 并放到当前目录")
    return None

def generate_font_bin(font_path):
    """使用 lv_font_conv 生成 .bin 字体文件"""

    # 去重并排序字符
    unique_chars = sorted(set(ALL_CHARS))
    chars_str = ''.join(unique_chars)

    # 写入临时字符文件
    char_file = "font_chars.txt"
    with open(char_file, "w", encoding="utf-8") as f:
        f.write(chars_str)

    print(f"生成 {len(unique_chars)} 个字符的字体...")

    cmd = [
        "lv_font_conv",
        "--font", font_path,
        "--size", str(FONT_SIZE),
        "--bpp", str(BPP),
        "--format", "bin",
        "--no-compress",
        "--symbols", chars_str,  # 直接传递字符
        "--output", OUTPUT_BIN
    ]

    print("执行命令:")
    print(" ".join(cmd[:6]), "...")  # 只显示前几个参数，避免字符太多

    try:
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        print(result.stdout)
    except FileNotFoundError:
        print("\n错误: lv_font_conv 未安装!")
        print("请运行: npm install -g lv_font_conv")
        return False
    except subprocess.CalledProcessError as e:
        print(f"\n错误: 字体生成失败!")
        print(e.stderr)
        return False
    finally:
        # 清理临时文件
        if os.path.exists(char_file):
            os.remove(char_file)

    return True

def bin_to_c_array():
    """将 .bin 文件转换为 C 数组"""

    if not os.path.exists(OUTPUT_BIN):
        print(f"错误: {OUTPUT_BIN} 不存在!")
        return False

    with open(OUTPUT_BIN, "rb") as f:
        data = f.read()

    size = len(data)
    print(f"字体文件大小: {size} 字节 ({size/1024:.1f} KB)")

    # 生成 .c 文件
    with open(OUTPUT_C, "w") as f:
        f.write(f"""/*******************************************************
 * 内置中文字体 - 自动生成
 *
 * 字体大小: {FONT_SIZE}px
 * BPP: {BPP}
 * 字符数: {len(set(ALL_CHARS))}
 * 文件大小: {size} bytes
 *******************************************************/

#include "lvgl.h"
#include "chinese_font.h"

// 字体数据 (对齐到 4 字节边界)
__attribute__((aligned(4)))
static const uint8_t chinese_font_data[{size}] = {{
""")

        # 写入数据 (每行 16 字节)
        for i in range(0, size, 16):
            chunk = data[i:i+16]
            hex_bytes = " ".join(f"0x{b:02x}" for b in chunk)
            f.write(f"    {hex_bytes},\n")

        f.write("};\n")
        f.write("\n// 字体结构\n")
        f.write("extern const lv_font_t chinese_font_16;\n")

    # 生成 .h 文件
    with open(OUTPUT_H, "w") as f:
        f.write(f"""/*******************************************************
 * 内置中文字体头文件
 *******************************************************/

#ifndef CHINESE_FONT_H
#define CHINESE_FONT_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {{
#endif

/**
 * @brief 获取内置中文字体
 * @return 字体指针，失败返回 NULL
 */
const lv_font_t* chinese_font_get(void);

#ifdef __cplusplus
}}
#endif

#endif // CHINESE_FONT_H
""")

    print(f"已生成: {OUTPUT_C}, {OUTPUT_H}")
    return True

def main():
    print("=" * 60)
    print("LVGL 内置中文字体生成器")
    print("=" * 60)

    # 1. 获取字体
    font_path = get_system_font()
    if not font_path:
        return 1

    # 2. 生成 .bin
    if not generate_font_bin(font_path):
        return 1

    # 3. 转换为 C 数组
    if not bin_to_c_array():
        return 1

    print("\n" + "=" * 60)
    print("生成完成!")
    print("=" * 60)
    print("\n下一步:")
    print(f"1. 复制 {OUTPUT_C} 和 {OUTPUT_H} 到 main/ui/")
    print(f"2. 在 main/CMakeLists.txt 中添加 {OUTPUT_C}")
    print("3. 重新编译项目")

    return 0

if __name__ == "__main__":
    sys.exit(main())
