#!/usr/bin/env python3
# -*- coding:utf-8 -*-

import os
import re
import json


compile_commands_path = 'build/compile_commands.json'

project_name = 'esp32c3'
intellisense = 'linux-gcc-arm'
compiler_path = None

force_include = [
    'build/config/sdkconfig.h'
]

# 默认使用的define
defs_default = [

]

# 默认使用的include
incs_default = [

]

# 无用的define
defs_filter_out = [

]

# 无用的include
incs_filter_out = [

]

# 不需要检索的目录
path_filter_out = [
    '.vscode',
    'hrs_server',
]

# 不需要检索的目录特征字
keyw_filter_out = [
    '.git',
]

# 默认exclude path，列表所包含的目录不会在vscode中展示
epath_default = [
    '**/.git',
    '**/.svn',
    '**/.hg',
    '**/CVS',
    '**/.DS_Store',
    '**/Thumbs.db',
    '**/*.cmd',
    '**/*.d',
    '**/*.o',
    '**/*.tmp',
]

def __beautiful_defs(x):
    return x.replace(r'\"', r'"').replace(r'""', r'"')

def __beautiful_incs(x):
    return os.path.relpath(x)

def __start_within_list(s, l):
    for x in l:
        if s.startswith(x):
            return True
    return False

def __contain_within_list(s, l):
    for x in l:
        if x in s:
            return True
    return False

def __start_within_str(l, s):
    for x in l:
        if x.startswith(s):
            return True
    return False

def get_compiler_files_defs_incs_from_bear(bear):
    with open(bear, 'r') as f:
        json_strs = f.read()
    j = json.loads(json_strs)

    compiler = compiler_path
    files, defs, incs = [], [], []

    files += force_include
    defs += defs_default
    incs += incs_default
    incs += [os.path.split(x)[0] for x in force_include]

    for x in j:
        root = x['directory']
        f = os.path.relpath(os.path.join(root, x['file']))
        files.append(f)
        print(f)

        args = x.get('arguments')
        if args == None:
            args = x.get('command').split(' ')
        # 自动识别编译器
        if f.endswith('.c') and compiler == None:
            compiler = os.path.abspath(os.path.join(root, args[0]))
        for p in args:
            if p.startswith('-D') and len(p) > 2: defs.append(p[2:])
            if p.startswith('-I') and len(p) > 2: incs.append(p[2:])

        defs = list(set(defs))
        incs = list(set(incs))

    defs = [__beautiful_defs(x) for x in defs]
    defs = [x for x in defs if len(x) > 0 and not __start_within_list(x, defs_filter_out)]
    defs = list(set(defs))

    incs = [__beautiful_incs(os.path.join(root, x)) for x in incs]
    incs = [x for x in incs if len(x) > 0 and os.path.exists(x) and not __start_within_list(x, incs_filter_out)]
    incs = list(set(incs))

    files.sort()
    defs.sort()
    incs.sort()

    return (compiler, files, defs, incs)

def get_exclude_paths(files, incs):
    ipaths = [os.path.split(f)[0] for f in files]
    ipaths += incs
    ipaths = list(set(ipaths))
    ipaths.sort()

    apaths = []
    for root, _, files, in os.walk('.'):
        apaths.append(root[2:])

    tpath = []
    for p in apaths:
        if __start_within_list(p, path_filter_out):
            continue
        if __contain_within_list(p, keyw_filter_out):
            continue
        if __start_within_str(ipaths, p):
            continue
        print(p)
        tpath.append(p)

    epath = []
    for n in range(0, len(tpath)):
        if __start_within_list(tpath[n], epath[:n]):
            continue
        epath.append(tpath[n])

    return epath

def generate_c_cpp_priorities(compiler, defs, incs):
    j = dict()
    j['configurations'] = list()
    j['configurations'].append(dict())
    j['configurations'][0]['name'] = project_name
    j['configurations'][0]['includePath'] = incs
    j['configurations'][0]['defines'] = defs
    j['configurations'][0]['compilerPath'] = compiler
    j['configurations'][0]['intelliSenseMode'] = intellisense
    j['configurations'][0]['browse'] = {"limitSymbolsToIncludedHeaders": True}
    j['configurations'][0]['cStandard'] = "c99"
    j['configurations'][0]['cppStandard'] = "c++11"
    j['configurations'][0]['forcedInclude'] = force_include
    j['version'] = 4

    with open('.vscode/c_cpp_properties.json', 'w') as f:
        json_str = json.dumps(j, sort_keys=True, indent=4, separators=(',', ': '))
        f.write(json_str)

def generate_settings(epaths):
    j = dict()
    j['git.ignoredRepositories'] = ['esp-idf']
    j['files.associations'] = dict()
    j['files.associations']['*.h'] = 'c'
    j['files.exclude'] = dict(zip(epaths, [True] * len(epaths)))

    with open('.vscode/settings.json', 'w') as f:
        json_str = json.dumps(j, sort_keys=True, indent=4, separators=(',', ': '))
        f.write(json_str)

if __name__ == '__main__':
    compiler, files, defs, incs = get_compiler_files_defs_incs_from_bear(compile_commands_path)
    files = [os.path.relpath(x) for x in files]
    epaths = epath_default + get_exclude_paths(files, incs)
    # with open('files.txt', 'w') as f:
    #     f.writelines([x + '\n' for x in files])
    # with open('defs.txt', 'w') as f:
    #     f.writelines([x + '\n' for x in defs])
    # with open('incs.txt', 'w') as f:
    #     f.writelines([x + '\n' for x in incs])
    # with open('epaths.txt', 'w') as f:
    #     f.writelines([x + '\n' for x in epaths])
    if not os.path.exists('.vscode'):
        os.mkdir('.vscode')
    generate_c_cpp_priorities(compiler, defs, incs)
    generate_settings(epaths)
