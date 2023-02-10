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
from time import sleep
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

class ThumbnailLoader(QtCore.QRunnable):
    
    class ThumbnailLoaderSignals(QtCore.QObject):
        finished = QtCore.Signal(object)

    def __init__(self, matlib_client, material):
        super(ThumbnailLoader, self).__init__()
        self._matlib_client = matlib_client
        self._material = material
        self.signals = ThumbnailLoader.ThumbnailLoaderSignals()
        script_dir = os.path.realpath(os.path.dirname(__file__))
        self._cache_dir = os.path.abspath(os.path.join(script_dir, "..", "..", "..", "..", "plugin", "usd", "rprUsd", "resources", "cache", "matlib", "thumbnails"))
        if not os.path.isdir(self._cache_dir):
            os.makedirs(self._cache_dir)

    def run(self):
        thumbnail_id = self._material["renders_order"][0]
        cached_thumbnail_path = str(os.path.join(self._cache_dir, thumbnail_id))
        thumbnail_path = ""
        if(os.path.isfile(cached_thumbnail_path+"_thumbnail.jpeg")):
            thumbnail_path = cached_thumbnail_path+"_thumbnail.jpeg"
        elif(os.path.isfile(cached_thumbnail_path+"_thumbnail.jpg")):
            thumbnail_path = cached_thumbnail_path + "_thumbnail.jpg"
        elif (os.path.isfile(cached_thumbnail_path + "_thumbnail.png")):
            thumbnail_path = cached_thumbnail_path + "_thumbnail.png"
        else:
            while thumbnail_path == "":
                try:
                    render_info = self._matlib_client.renders.get(thumbnail_id)
                    self._matlib_client.renders.download_thumbnail(thumbnail_id, self._cache_dir)
                    thumbnail_path = os.path.join(self._cache_dir, render_info["thumbnail"])
                except:
                    print("loading failed")
                    sleep(1)
        self.signals.finished.emit({"title": self._material["title"], "thumbnail": thumbnail_path})


class MaterialLibraryWidget(QtWidgets.QWidget):
    def __init__(self):
        super(MaterialLibraryWidget, self).__init__()

        self._matlib_client = MatlibClient()

        script_dir = os.path.dirname(os.path.abspath(__file__))
        ui_filepath = os.path.join(script_dir, 'materialLibrary.ui')
        self._ui = QtUiTools.QUiLoader().load(ui_filepath, parentWidget=self)

        self._materialsView = LibraryListWidget(self)
        self._ui.verticalLayout_3.insertWidget(0, self._materialsView)

        self._ui.helpButton.clicked.connect(self._helpButtonClicked)
        self._ui.filter.textChanged.connect(self._updateMaterialList)

        self._initCategoryList()

        self._layout = QtWidgets.QVBoxLayout()
        self.setLayout(self._layout)
        self._layout.setContentsMargins(0, 0, 0, 0)
        self._layout.addWidget(self._ui)

    def _initCategoryList(self):
        categories = self._matlib_client.categories.get_list(limit=maxElementCount)
        item = QtWidgets.QListWidgetItem("All (" + str(sum([category["materials"] for category in categories])) + ")")
        item.value = None
        self._ui.categoryView.addItem(item)
        for category in categories:
            item = QtWidgets.QListWidgetItem("    " + category["title"] + " (" + str(category["materials"]) + ")")
            item.value = category["title"]
            self._ui.categoryView.addItem(item)
        self._ui.categoryView.setCurrentItem(self._ui.categoryView.item(0))
        self._ui.categoryView.clicked.connect(self._updateMaterialList)
        self._updateMaterialList()

    def _updateMaterialList(self):
        category = self._ui.categoryView.currentItem().value
        search_string = self._ui.filter.text()
        params = {}
        if category is not None:
            params["category"] = category
        if search_string != "":
            params["search"] = search_string
        materials = self._matlib_client.materials.get_list(limit=maxElementCount, params=params)

        self._progress_dialog = QtWidgets.QProgressDialog('Loading thumbnails', None, 0, len(materials), self)
        self._progress = 0
        self._progress_dialog.setValue(0)
        self._materialsView.clear()

        for material in materials:
            loader = ThumbnailLoader(self._matlib_client, material)
            loader.signals.finished.connect(self._onThumbnailLoaded)
            QtCore.QThreadPool.globalInstance().start(loader)

    def _onThumbnailLoaded(self, result):
        icon = QtGui.QIcon(QtGui.QPixmap(result["thumbnail"]))
        material_item = QtWidgets.QListWidgetItem(result["title"], self._materialsView)
        material_item.setIcon(icon)
        self._materialsView.addItem(material_item)
        self._progress += 1
        self._progress_dialog.setValue(self._progress)

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
