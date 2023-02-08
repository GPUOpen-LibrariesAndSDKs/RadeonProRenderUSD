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
from PIL import Image, ImageQt
from . materialLibraryClient import MatlibClient

maxElementCount = 10000

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

def IsMouseOnWidget(widget):
    mouse_pos_global = QtGui.QCursor.pos()
    mouse_pos_local = widget.mapFromGlobal(mouse_pos_global)
    return widget.rect().contains(mouse_pos_local)

PREVIEW_SIZE = 200
class LibraryListWidget(QtWidgets.QListWidget):
    def __init__(self, parent):
        super(LibraryListWidget, self).__init__(parent)
        self.setFrameShape(QtWidgets.QFrame.NoFrame)
        self.setDropIndicatorShown(False)
        self.setIconSize(QtCore.QSize(PREVIEW_SIZE, PREVIEW_SIZE))
        self.setFlow(QtWidgets.QListView.LeftToRight)
        self.setResizeMode(QtWidgets.QListView.Adjust)
        self.setSpacing(5)
        self.setViewMode(QtWidgets.QListView.IconMode)
        self.setWordWrap(True)
        self.setSelectionRectVisible(False)
        self.setAttribute(QtCore.Qt.WA_Hover)

        #self._previewWindow = parent._fullPreviewWindow

    # def mouseMoveEvent(self, mouse_event):
    #     #if self._previewWindow.isVisible():
    #         # if not IsMouseOnWidget(self):
    #         #     self._previewWindow.hide()
    #         # else:
    #         #     self._previewWindow.mouseMoveEvent(mouse_event)
    #
    #     super(LibraryListWidget, self).mouseMoveEvent(mouse_event)

    # def leaveEvent(self, event):
    #     #if self._previewWindow.isVisible():
    #         #if not IsMouseOnWidget(self):
    #             # self._previewWindow.hide()
    #             pass
    #
    #     super(LibraryListWidget, self).leaveEvent(event)
    #     self._listWidget = LibraryListWidget(self)
    #     self._ui.verticalLayout_3.insertWidget(0, self._listWidget)

class ThumbnailLoader:
    def __init__(self, matlib_client):
        self._matlib_client = matlib_client
        script_dir = os.path.realpath(os.path.dirname(__file__))
        self._cache_dir = os.path.abspath(os.path.join(script_dir, "..", "..", "..", "..", "plugin", "usd", "rprUsd", "resources", "cache", "matlib", "thumbnails"))
        if not os.path.isdir(self._cache_dir):
            os.makedirs(self._cache_dir)

    def get_thumbnail_path(self, id):
        cached_thumbnail_path = str(os.path.join(self._cache_dir, id))
        if(os.path.isfile(cached_thumbnail_path+"_thumbnail.jpeg")):
            return cached_thumbnail_path+"_thumbnail.jpeg"
        elif(os.path.isfile(cached_thumbnail_path+"_thumbnail.jpg")):
            return cached_thumbnail_path + "_thumbnail.jpg"
        elif (os.path.isfile(cached_thumbnail_path + "_thumbnail.png")):
            return cached_thumbnail_path + "_thumbnail.png"
        render_info = self._matlib_client.renders.get(id)
        thumbnail_path = os.path.join(self._cache_dir, render_info["thumbnail"])
        self._matlib_client.renders.download_thumbnail(id, self._cache_dir)
        return thumbnail_path
        

class MaterialLibraryWidget(QtWidgets.QWidget):
    def __init__(self):
        super(MaterialLibraryWidget, self).__init__()

        self._matlib_client = MatlibClient()
        self._thumbnail_loader = ThumbnailLoader(self._matlib_client)
        self._prev_category = None

        script_dir = os.path.dirname(os.path.abspath(__file__))
        ui_filepath = os.path.join(script_dir, 'materialLibrary.ui')
        self._ui = QtUiTools.QUiLoader().load(ui_filepath, parentWidget=self)

        self._materialsView = LibraryListWidget(self)
        self._ui.verticalLayout_3.insertWidget(0, self._materialsView)

        self._ui.helpButton.clicked.connect(self._helpButtonClicked)

        self._initCategoryList()

        self._layout = QtWidgets.QVBoxLayout()
        self.setLayout(self._layout)
        self._layout.setContentsMargins(0, 0, 0, 0)
        self._layout.addWidget(self._ui)

    def _initCategoryList(self):
        categories = self._matlib_client.categories.get_list(limit=maxElementCount)
        for i in range(len(categories)):
            self._ui.categoryView.insertItem(i, categories[i]["title"])
        self._ui.categoryView.setCurrentItem(self._ui.categoryView.item(0))
        self._ui.categoryView.clicked.connect(self._updateMaterialList)
        self._updateMaterialList()

    def _updateMaterialList(self):
        category = self._ui.categoryView.currentItem().text()
        if(category == self._prev_category): # skip updating because click was on same category
            return
        self._prev_category = category
        params = {"category": category}
        materials = self._matlib_client.materials.get_list(limit=maxElementCount, params=params)

        self._materialsView.clear()
        for i in range(len(materials)):
            thumbnail_id = materials[i]["renders_order"][0]
            thumbnail_path = self._thumbnail_loader.get_thumbnail_path(thumbnail_id)
            icon = QtGui.QIcon(QtGui.QPixmap(thumbnail_path))
            material_item = QtWidgets.QListWidgetItem(materials[i]["title"], self._materialsView)
            material_item.setIcon(icon)
            self._materialsView.insertItem(i, material_item)

    def _helpButtonClicked(self):
        QtWidgets.QMessageBox.question(self, 'Help', HELP_TEXT, QtWidgets.QMessageBox.Ok)


class MaterialLibraryWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super(MaterialLibraryWindow, self).__init__()

        matLibWidget = MaterialLibraryWidget()

        layout = QtWidgets.QVBoxLayout()
        layout.addWidget(matLibWidget)

        central_widget = QtWidgets.QWidget()
        central_widget.setLayout(layout)

        self.setCentralWidget(central_widget)
        self.setParent(hou.qt.mainWindow(), QtCore.Qt.Window)
        self.setWindowTitle('RPR Material Library')

        desktop_widget = QtWidgets.QDesktopWidget()
        primary_screen = desktop_widget.screen(desktop_widget.primaryScreen())
        self.resize(primary_screen.width() // 3, int(primary_screen.height() * 0.8))


def import_material():
    window = MaterialLibraryWindow()
    window.show()
