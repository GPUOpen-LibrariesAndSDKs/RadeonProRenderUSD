# Copyright 2020 Advanced Micro Devices, Inc
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#     http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# 
import os
import re
import hou
import json
import errno
import shutil
from hutil.Qt import QtCore, QtGui, QtWidgets, QtUiTools
from . materialLibraryClient import MatlibClient

client=None
num = 0

HELP_TEXT = '''
To import a material click on a corresponding swatch. The material is always imported as a separate "Material Library" LOP node.

The importer always auto-assigns an imported material to the last modified prims of the selected LOP nodes. In this mode, the importer replaces the previously imported material with the new one, thus allowing to rapidly change materials. 

Use the text input widget at the bottom of the window to filter displayed materials.
'''

def recursive_mkdir(path):
    try:
        os.makedirs(path)
    except OSError as e:
        if errno.EEXIST != e.errno:
            raise

def import_material():
    global client
    global num
    if not client:
        client = MatlibClient()
    categories = client.categories.get_list(client.categories.count())
    params = {"category":categories[num]["title"]}
    materials = client.materials.get_list(limit=client.materials.count(params=params), params=params)
    num +=1 
    for material in materials:
        print(material["title"])
