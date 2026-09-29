#!/usr/bin/env python3
import os
import sys
import subprocess

def link_if_needed(root):
    dylib_path = os.path.join(root, 'Binaries', 'Mac', 'libUnrealEditor-CRICKET26.dylib')
    rsp_in = os.path.join(root, 'Intermediate', 'Build', 'Mac', 'arm64', 'UnrealEditor', 'Development', 'CRICKET26', 'libUnrealEditor-CRICKET26.dylib.rsp')
    rsp_out = os.path.join(root, 'Intermediate', 'Build', 'Mac', 'arm64', 'UnrealEditor', 'Development', 'CRICKET26', 'libUnrealEditor-CRICKET26.fixed.rsp')

    if not os.path.exists(rsp_in):
        return

    # Check if dylib exists and is newer than rsp
    if os.path.exists(dylib_path) and os.path.getmtime(dylib_path) >= os.path.getmtime(rsp_in):
        return

    print("Linking libUnrealEditor-CRICKET26.dylib...")
    with open(rsp_in, 'r') as f:
        c = f.read()

    # Fix relative engine paths in response file
    fixed = c.replace('"../', '"/Users/Shared/Epic Games/UE_5.8/Engine/')
    with open(rsp_out, 'w') as f:
        f.write(fixed)

    cmd = [
        '/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++',
        f'@{rsp_out}'
    ]
    subprocess.check_call(cmd)
    print("libUnrealEditor-CRICKET26.dylib linked successfully.")

if __name__ == '__main__':
    root_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
    link_if_needed(root_dir)
