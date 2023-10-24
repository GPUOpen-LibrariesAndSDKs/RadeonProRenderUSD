# This file is used ONLY with BUILD_RESOLVER flag

import os
import sys

# this is a hack to specify boost building toolset
def modify_toolset(filename, generator):
    with open(filename) as f:
        lines = f.readlines()
    lines[14] = "call .\\build.bat " + generator + "\n"
    with open(filename, "w") as f:
        for l in lines:
            print(l, file=f, end="")

def launch_bootstrap(filename):
    os.system(filename)

if __name__ == "__main__":
    modify_toolset(sys.argv[1], sys.argv[2])
    launch_bootstrap(sys.argv[1])