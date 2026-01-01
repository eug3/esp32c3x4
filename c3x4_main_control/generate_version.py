#!/usr/bin/env python3
"""
自动生成版本文件
基于Git commit count自动递增版本号
"""
import subprocess
import os
import sys
from datetime import datetime

def get_git_info():
    """获取Git信息"""
    try:
        # 获取commit count (作为build number)
        build_num = subprocess.check_output(
            ['git', 'rev-list', '--count', 'HEAD'],
            stderr=subprocess.DEVNULL
        ).decode('utf-8').strip()
        
        # 获取short commit hash
        commit_hash = subprocess.check_output(
            ['git', 'rev-parse', '--short', 'HEAD'],
            stderr=subprocess.DEVNULL
        ).decode('utf-8').strip()
        
        # 检查是否有未提交的修改
        dirty = subprocess.call(
            ['git', 'diff-index', '--quiet', 'HEAD', '--'],
            stderr=subprocess.DEVNULL
        ) != 0
        
        return build_num, commit_hash, dirty
    except:
        return "0", "unknown", False

def generate_version_file(output_file):
    """生成版本头文件"""
    build_num, commit_hash, dirty = get_git_info()
    build_time = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    
    # 主版本号
    MAJOR = 1
    MINOR = 0
    PATCH = 0
    
    # 构建版本字符串
    version_string = f"v{MAJOR}.{MINOR}.{PATCH}.{build_num}"
    if dirty:
        version_string += "-dirty"
    
    full_version = f"{version_string} - Min Monster ({commit_hash})"
    
    # 生成C头文件
    header_content = f'''/**
 * @file version.h
 * @brief 自动生成的版本信息
 * @note  此文件由 generate_version.py 自动生成，请勿手动修改
 */

#ifndef VERSION_H
#define VERSION_H

#define VERSION_MAJOR       {MAJOR}
#define VERSION_MINOR       {MINOR}
#define VERSION_PATCH       {PATCH}
#define VERSION_BUILD       {build_num}
#define VERSION_STRING      "{version_string}"
#define VERSION_FULL        "{full_version}"
#define BUILD_TIME          "{build_time}"
#define GIT_COMMIT_HASH     "{commit_hash}"
#define GIT_DIRTY           {1 if dirty else 0}

#endif // VERSION_H
'''
    
    # 写入文件
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(header_content)
    
    print(f"Generated version: {full_version}")
    print(f"Build time: {build_time}")
    return 0

if __name__ == '__main__':
    if len(sys.argv) > 1:
        output_file = sys.argv[1]
    else:
        output_file = 'version.h'
    
    sys.exit(generate_version_file(output_file))
