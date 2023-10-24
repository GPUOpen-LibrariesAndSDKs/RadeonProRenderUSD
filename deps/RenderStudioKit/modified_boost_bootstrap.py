# This file is used ONLY with BUILD_RESOLVER flag

import os
import sys


# This is a hack to specify boost building toolset because boost version 1.76.0 can't support toolchain override
def modify_toolset(filename, toolchain):
    with open(filename) as f:
        lines = f.readlines()
    lines[14] = "call .\\build.bat " + toolchain + "\n"
    with open(filename, "w") as f:
        for l in lines:
            print(l, file=f, end="")


if __name__ == "__main__":
    if len(sys.argv) > 2:
        modify_toolset(sys.argv[1], sys.argv[2])
