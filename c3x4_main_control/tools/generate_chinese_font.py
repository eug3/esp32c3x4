#!/usr/bin/env python3
"""
生成 LVGL 常用中文字体库
使用方法: python generate_chinese_font.py [字体文件.ttf]
"""

import os
import sys
import struct
import subprocess
import argparse

# 常用汉字列表（约 3500 字，够日常使用）
COMMON_CHINESE = """的一是了我不在人有他这中大来上国个中
和们地到以说时要就出会可下而你年生能子多去对
也面着看能得于过自好时用想能把那吗怎看听开
呢吗啊哦吧么吧呢哦啊呀呵哈啰哇哪吧呢嘛哟哦呀
嗯哎唉喔哟呐嗯呗诶哟噢嗯哎哇哈嘻呢噢嘛呗
爱安八白百办帮包保北本比必边表别并博才参
草层曾查常场厂超车称吃传创春次从错达带大
但当得到灯登等低底弟电定东动都读段队对儿
发法番饭放非分复该干刚高告歌格给更工公共
顾挂关光贵国过海好号合很回会活或及己记纪
继价建见将叫较接教节解金经九久句决军开发
卡开看康可克口苦块快来蓝老乐理里力历连良
量了两领路乱吗马满慢毛每没门米面民明母内
你年鸟宁农排旁跑佩批平七期千强秦清请全群
然热人认日容如三色山社身生声时十事室手书
双谁水睡说思四诉算随所台态谈提体天条通同
头土外完晚王位文问乌无五西下线像小校笑写
心新行许选学言业一医院已意因永用有于玉员
再在早站张照真之只中住专子走最作做坐座
一二三四五六七八九十百千万亿兆京
日月年时分秒星期
今天明天昨天
春夏秋冬
东西南北
上下左右
前后中外
天地人间
年月日时分秒
北京时间
开机关机重启
确认返回上一页下一页
设置关于帮助
音量加减暂停播放
文件目录图片音乐视频
文档电子书
返回主页退出
上一页下一页上一页
上一页下一页
加载中请稍候
正在加载正在保存
成功失败错误
内存不足存储空间
电量低请充电
已连接已断开
配对成功连接失败
用户名密码
登录注册退出
刷新同步上传下载
文件已删除
无法打开
格式不支持
系统更新
版本检查
网络连接
WiFi蓝牙
选择确定取消
应用商店
我的收藏
最近打开
使用说明
联系我们
用户协议
隐私政策
免责声明
软件许可
技术支持
反馈建议
评分好评差评
下载安装更新
清理缓存释放空间
字体大小
屏幕亮度
护眼模式深色模式
省电模式
勿扰模式
飞行模式
移动数据
个人热点
开发者选项
USB调试
安全设置
账户管理
语言和时区
日期和时间
键盘和输入法
无障碍选项
备份和重置
系统信息
关于手机
状态栏导航栏
快捷设置
通知管理
应用权限
默认应用
存储管理
电池使用
内存使用
CPU使用
网络流量
设备信息
处理器内存存储
屏幕分辨率
系统版本
安全补丁
序列号IMEI
监管信息
法律信息
开源许可
第三方许可
产品名称型号
生产日期制造商
保修信息
客服电话在线客服
电子邮件官方网站
社交媒体
技术支持时间
服务范围
维修网点
预约服务
退换货政策
快递物流
支付方式
优惠券积分
会员等级
生日特权
新人礼包
限时特价
满减活动
秒杀专场
品牌特卖
海外直邮
保税仓发货
七天无理由
假一赔十
正品保障
极速发货
当日达次日达
智能推荐
热门榜单
新品上架
猜你喜欢
看了又看
相似推荐
销量排行
价格趋势
用户评价
晒单返现
问答社区
兴趣圈子
话题讨论
创作中心
发布内容
草稿箱已发布
审核中已通过
违规内容举报
侵权投诉
商务合作
广告投放
品牌入驻
供应商招募
人才培养
企业文化
社会责任
投资者关系
公司地址
联系电话
传真邮箱
招聘岗位
福利待遇
工作地点
简历投递
面试流程
入职培训
职业发展
员工故事
新闻动态
媒体报道
行业资讯
技术博客
开源项目
社区活动
线上线下
合作伙伴
战略合作
媒体关系
企业荣誉
资质认证
发展历程
创始人寄语
愿景使命价值观
"""

def generate_font_bin(font_path, output_path, font_size=16, bpp=1):
    """使用 lv_font_conv 生成字体文件"""

    # 去重字符
    seen = set()
    chars_list = []
    for char in COMMON_CHINESE:
        if char not in seen and char.strip():
            seen.add(char)
            chars_list.append(char)

    # 使用 --symbols 参数，直接传递字符字符串
    symbols_str = ''.join(chars_list)

    # 构建命令
    cmd = [
        'lv_font_conv',
        '--font', font_path,
        '--size', str(font_size),
        '--bpp', str(bpp),
        '--symbols', symbols_str,
        '--format', 'bin',
        '-o', output_path
    ]

    print(f"Generating font: {output_path}")
    print(f"  Font: {font_path}")
    print(f"  Size: {font_size}px")
    print(f"  BPP: {bpp}")

    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"Error: {result.stderr}")
            return False
        print(f"Success! Output size: {os.path.getsize(output_path)} bytes")
        return True
    except FileNotFoundError:
        print("Error: lv_font_conv not found!")
        print("Install with: npm install -g lv_font_conv")
        return False


def bin_to_c_array(input_path, output_path, array_name="chinese_font"):
    """将 bin 文件转换为 C 数组"""

    with open(input_path, 'rb') as f:
        data = f.read()

    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(f"/**\n")
        f.write(f" * @file chinese_font.h\n")
        f.write(f" * @brief 内置中文字体库\n")
        f.write(f" *\n")
        f.write(f" * 自动生成，包含 {len(COMMON_CHINESE)} 个常用字符\n")
        f.write(f" * 字体大小: 16px, 1bpp\n")
        f.write(f" * 文件大小: {len(data)} bytes\n")
        f.write(f" */\n\n")
        f.write(f"#ifndef CHINESE_FONT_H\n")
        f.write(f"#define CHINESE_FONT_H\n\n")
        f.write(f"#include <stdint.h>\n\n")
        f.write(f"#define CHINESE_FONT_SIZE     {16}\n")
        f.write(f"#define CHINESE_FONT_BPP      {1}\n")
        f.write(f"#define CHINESE_FONT_DATA_LEN {len(data)}\n\n")
        f.write(f"// 字体数据数组\n")
        f.write(f"static const uint8_t {array_name}_data[] = {{\n")

        # 每行 16 个字节
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            hex_str = ', '.join(f'0x{b:02x}' for b in chunk)
            f.write(f"    {hex_str},\n")

        f.write(f"}};\n\n")
        f.write(f"#endif // CHINESE_FONT_H\n")

    print(f"C array saved: {output_path}")


def main():
    parser = argparse.ArgumentParser(description='Generate LVGL Chinese font')
    parser.add_argument('font', nargs='?', default='simsun.ttc',
                        help='Path to font file (default: simsun.ttc)')
    parser.add_argument('--size', '-s', type=int, default=16,
                        help='Font size in pixels (default: 16)')
    parser.add_argument('--bpp', '-b', type=int, default=1,
                        help='Bits per pixel (default: 1)')
    parser.add_argument('--output', '-o', default='chinese_font.bin',
                        help='Output bin file path')
    parser.add_argument('--no-c', action='store_true',
                        help='Skip C array generation')

    args = parser.parse_args()

    if not os.path.exists(args.font):
        print(f"Error: Font file not found: {args.font}")
        print("Please specify path to a Chinese TTF/TTC font file")
        sys.exit(1)

    # 生成 bin 文件
    if generate_font_bin(args.font, args.output, args.size, args.bpp):
        # 生成 C 数组
        if not args.no_c:
            c_file = args.output.replace('.bin', '.h')
            bin_to_c_array(args.output, c_file)
            print("\nDone! Copy the .h file to your project and use font_manager to load it.")

if __name__ == '__main__':
    main()
