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
import zipfile
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


def create_houdini_material_graph(material_name, mtlx_file):
    MATERIAL_LIBRARY_TAG = 'hdrpr_material_library_generated'

    selected_nodes = hou.selectedNodes()

    if selected_nodes:
        matlib_parent = selected_nodes[0].parent()
    else:
        matlib_parent = hou.node('/stage')

    if matlib_parent.type().name() == "materiallibrary":  # call inside material library
        mtlx_node = matlib_parent.createNode('RPR::rpr_materialx_node')
        mtlx_node.setName(material_name, unique_name=True)
        mtlx_node.parm('file').set(mtlx_file)
        return

    matlib_node = matlib_parent.createNode('materiallibrary')
    matlib_node.setName(material_name, unique_name=True)
    matlib_node.setComment(MATERIAL_LIBRARY_TAG)

    mtlx_node = matlib_node.createNode('RPR::rpr_materialx_node')
    mtlx_node.setName(material_name, unique_name=True)
    mtlx_node.parm('file').set(mtlx_file)

    if len(selected_nodes) == 0:
        matlib_node.setSelected(True, clear_all_selected=True)

    material_assignment = []
    for node in selected_nodes:
        if isinstance(node, hou.LopNode):
            material_assignment.extend(map(str, node.lastModifiedPrims()))

    if material_assignment:
        material_assignment = ' '.join(material_assignment)

        matlib_node.parm('assign1').set(True)
        matlib_node.parm('geopath1').set(material_assignment)

        viewer_node = matlib_node.network().viewerNode()

        # Ideally, we want to connect our material library node directly to the selected one,
        if len(selected_nodes) == 1:
            connect_node = selected_nodes[0]
        else:
            # but in case more than one node selected, we need to traverse the whole graph and find out the deepest node.
            # There is a lot of corner cases. It's much safer and cleaner to connect our node to the viewer node (deepest node with display flag set on).
            # Though, this can be revisited later.
            connect_node = viewer_node

            # TODO: consider making this behavior optional: what if the user wants to create few material at once?
            # Remove previously created material - this activates rapid change of material when scrolling through the library widget
            if connect_node.comment() == MATERIAL_LIBRARY_TAG and \
                    connect_node.parm('geopath1').evalAsString() == material_assignment:
                connect_node.destroy()

                viewer_node = matlib_node.network().viewerNode()
                connect_node = viewer_node

        # Insert our new node into existing connections
        for connection in connect_node.outputConnections():
            if connection.outputNode().comment() == MATERIAL_LIBRARY_TAG:
                # TODO: consider making this behavior optional: what if the user wants to create few material at once?
                # Remove previously created material - this activates rapid change of material when scrolling through the library widget
                for subconnection in connection.outputNode().outputConnections():
                    subconnection.outputNode().setInput(subconnection.inputIndex(), matlib_node)
                connection.outputNode().destroy()
            else:
                connection.outputNode().setInput(connection.inputIndex(), matlib_node)

        matlib_node.setInput(0, connect_node)
        matlib_node.moveToGoodPosition()

        if connect_node == matlib_node.network().viewerNode():
            matlib_node.setDisplayFlag(True)


def add_mtlx_includes(materialx_file_path): # we need to insert include to downloaded file
    include_file_name = "standard_surface.mtlx"
    library_root = os.path.join(os.path.dirname(materialx_file_path), "..", "..") # local library structure is like RPRMaterialLibrary/Materials/some_material, so we need copy common include file to root if needed
    include_file_path = os.path.join(library_root, include_file_name)

    if not os.path.isfile(include_file_path):
        script_dir = os.path.realpath(os.path.dirname(__file__))
        shutil.copyfile(os.path.join(script_dir, include_file_name), include_file_path)

    with open(materialx_file_path, "r") as mtlx_file:
        lines = mtlx_file.readlines()
    lines.insert(2, "\t<xi:include href=\"" + os.path.join("..", "..", include_file_name) + "\" />\n") # we need to include relative path
    with open(materialx_file_path, "w") as mtlx_file:
        mtlx_file.write("".join(lines))



PREVIEW_SIZE = 200
class LibraryListWidget(QtWidgets.QListWidget):
    def __init__(self, parent):
        super(LibraryListWidget, self).__init__(parent)
        self.setFrameShape(QtWidgets.QFrame.NoFrame)
        self.setDropIndicatorShown(False)
        self.setAcceptDrops(True)
        self.setIconSize(QtCore.QSize(PREVIEW_SIZE, PREVIEW_SIZE))
        self.setFlow(QtWidgets.QListView.LeftToRight)
        self.setResizeMode(QtWidgets.QListView.Adjust)
        self.setSpacing(5)
        self.setViewMode(QtWidgets.QListView.IconMode)
        self.setWordWrap(True)
        self.setSelectionRectVisible(False)
        self.setAttribute(QtCore.Qt.WA_Hover)

        self._previewWindow = parent._fullPreviewWindow

    def mouseMoveEvent(self, mouse_event):
        if self._previewWindow.isVisible():
            if not IsMouseOnWidget(self):
                self._previewWindow.hide()
            else:
                self._previewWindow.mouseMoveEvent(mouse_event)

        super(LibraryListWidget, self).mouseMoveEvent(mouse_event)

    def leaveEvent(self, event):
        if self._previewWindow.isVisible():
            if not IsMouseOnWidget(self):
                self._previewWindow.hide()

        super(LibraryListWidget, self).leaveEvent(event)

    def dragEnterEvent(self, e):
        e.accept()

    def dropEvent(self, e):
        e.accept()


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
            for attempt in range(5):
                try:
                    render_info = self._matlib_client.renders.get(thumbnail_id)
                    self._matlib_client.renders.download_thumbnail(thumbnail_id, self._cache_dir)
                    thumbnail_path = os.path.join(self._cache_dir, render_info["thumbnail"])
                    break
                except:
                    sleep(1) # pause thread and retry
        if thumbnail_path != "":
            self.signals.finished.emit({"material": self._material, "thumbnail": thumbnail_path})


class MaterialLoader(QtCore.QRunnable):

    class MaterialLoaderSignals(QtCore.QObject):
        finished = QtCore.Signal()

    def __init__(self, matlib_client, material, package):
        super(MaterialLoader, self).__init__()
        self._matlib_client = matlib_client
        self._material = material
        self._package = package
        self.signals = MaterialLoader.MaterialLoaderSignals()

    def run(self):
        hip_dir = os.path.dirname(hou.hipFile.path())
        dst_mtlx_dir = os.path.join(hip_dir, 'RPRMaterialLibrary', 'Materials')
        package_dir = os.path.join(dst_mtlx_dir, self._package["file"][:-4])

        downloaded_now = False
        if not os.path.isdir(package_dir):  # check if package is already loaded
            if not os.path.isdir(dst_mtlx_dir):
                os.makedirs(dst_mtlx_dir)
            self._matlib_client.packages.download(self._package["id"], dst_mtlx_dir)
            self._unpackZip(self._package["file"], dst_mtlx_dir)
            downloaded_now = True
        mtlx_file = self._findMtlx(package_dir)
        if mtlx_file == '':
            raise Exception("MaterialX file loading error")
        if downloaded_now:
            add_mtlx_includes(mtlx_file)
        create_houdini_material_graph(self._material["title"].replace(" ", "_").replace(":", "_"), mtlx_file)
        self.signals.finished.emit()

    def _unpackZip(self, filename, dst_dir):
        if filename.endswith('.zip'):
            zippath = os.path.join(dst_dir, filename)
            if not os.path.exists(zippath):
                raise Exception("Zip file not found")
            with zipfile.ZipFile(zippath, 'r') as zip_ref:
                packageDir = os.path.join(dst_dir, self._package["file"][:-4])
                zip_ref.extractall(packageDir)
            os.remove(zippath)
            return True

    def _findMtlx(self, dir):
        if not os.path.exists(dir):
            return ''
        for f in os.listdir(dir):
            fullname = os.path.join(dir, f)
            if os.path.isfile(fullname) and f.endswith('.mtlx'):
                return fullname
        return ''


class FullPreviewWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super(FullPreviewWindow, self).__init__()
        self.setWindowFlags(QtCore.Qt.FramelessWindowHint | QtCore.Qt.WindowStaysOnTopHint)

        icon_label = QtWidgets.QLabel()
        icon_label.setAlignment(QtCore.Qt.AlignCenter)
        icon_label.setFrameShape(QtWidgets.QFrame.Box)
        icon_label.setSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Expanding)
        icon_label.setBackgroundRole(QtGui.QPalette.Base)
        icon_label.setAttribute(QtCore.Qt.WA_TransparentForMouseEvents)
        self.setCentralWidget(icon_label)

        self.setMouseTracking(True)

        self._currentIconName = ''
        self._requiredWidget = None

    def setIcon(self, icon_name, icon):
        mouse_pos = QtGui.QCursor.pos()
        self.move(mouse_pos.x() + 1, mouse_pos.y() + 1)

        if self._currentIconName != icon_name:
            icon_size = icon.availableSizes()[0]
            pixmap = icon.pixmap(icon_size)
            self.centralWidget().setPixmap(pixmap)

        if not self.isVisible():
            self.show()

    def mouseMoveEvent(self, mouse_event):
        if self.isVisible():
            if not IsMouseOnWidget(self._requiredWidget):
                self.hide()
            else:
                self.move(mouse_event.globalX() + 1, mouse_event.globalY() + 1)


class MaterialLibraryWidget(QtWidgets.QWidget):
    def __init__(self):
        super(MaterialLibraryWidget, self).__init__()

        self._matlib_client = MatlibClient()
        self._fullPreviewWindow = FullPreviewWindow()

        self._materialIsLoading = False

        script_dir = os.path.dirname(os.path.abspath(__file__))
        ui_filepath = os.path.join(script_dir, 'materialLibrary.ui')
        self._ui = QtUiTools.QUiLoader().load(ui_filepath, parentWidget=self)

        self._materialsView = LibraryListWidget(self)
        self._fullPreviewWindow._requiredWidget = self._materialsView
        self._ui.verticalLayout_3.insertWidget(0, self._materialsView)

        self._ui.helpButton.clicked.connect(self._helpButtonClicked)
        self._ui.filter.textChanged.connect(self._filterChanged)
        self._materialsView.itemEntered.connect(self._materialItemEntered)
        self._materialsView.clicked.connect(self._materialItemClicked)
        self._materialsView.viewportEntered.connect(self._materialViewportEntered)
        self._materialsView.setMouseTracking(True)

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
        params = {}
        if category is not None:
            params["category"] = category
        materials = self._matlib_client.materials.get_list(limit=maxElementCount, params=params)

        self._thumbnail_progress_dialog = QtWidgets.QProgressDialog('Loading thumbnails', None, 0, len(materials), self)
        self._thumbnail_progress = 0
        self._thumbnail_progress_dialog.setValue(0)
        self._materialsView.clear()

        for material in materials:
            loader = ThumbnailLoader(self._matlib_client, material)
            loader.signals.finished.connect(self._onThumbnailLoaded)
            QtCore.QThreadPool.globalInstance().start(loader)

    def _onThumbnailLoaded(self, result):
        icon = QtGui.QIcon(QtGui.QPixmap(result["thumbnail"]))
        material_item = QtWidgets.QListWidgetItem(result["material"]["title"], self._materialsView)
        material_item.setIcon(icon)
        material_item.value = result["material"] # store material id in list item
        self._materialsView.addItem(material_item)
        self._thumbnail_progress += 1
        self._thumbnail_progress_dialog.setValue(self._thumbnail_progress)
        self._filterItems()

    def _onMaterialLoaded(self):
        self._material_progress_dialog.setMaximum(100)
        self._material_progress_dialog.setValue(100)
        self._materialIsLoading = False

    def _materialItemClicked(self, index):
        if(self._materialIsLoading):
            print("Another material is loading now")
            return
        self._materialIsLoading = True  # block while current material is loading
        self._fullPreviewWindow.hide()
        item = self._materialsView.item(index.row())
        quality_box = QtWidgets.QMessageBox()
        quality_box.setWindowTitle("Textures quality")
        quality_box.setText("Choose quality of textures")
        quality_box.setIcon(QtWidgets.QMessageBox.Icon.Question)
        packages = self._matlib_client.packages.get_list(params={"material": item.value["id"]})
        packages.sort(key=lambda x: x["label"])
        for p in packages:
            button = QtWidgets.QPushButton(p["label"], self)
            quality_box.addButton(button, QtWidgets.QMessageBox.ButtonRole.AcceptRole)
        quality_box.addButton(QtWidgets.QPushButton("Cancel", self), QtWidgets.QMessageBox.ButtonRole.RejectRole)
        button_number = quality_box.exec()
        if button_number >= len(packages):  # cancel button had been pressed
            self._materialIsLoading = False
            return
        self._material_progress_dialog = QtWidgets.QProgressDialog('Loading material', None, 0, 0, self)
        self._material_progress_dialog.setValue(0)
        loader = MaterialLoader(self._matlib_client, item.value, packages[button_number])
        loader.signals.finished.connect(self._onMaterialLoaded)
        QtCore.QThreadPool.globalInstance().start(loader)


    def _materialItemEntered(self, item):
        self._fullPreviewWindow.setIcon(item.text(), item.icon())

    def _materialViewportEntered(self):
        self._fullPreviewWindow.hide()

    def _helpButtonClicked(self):
        QtWidgets.QMessageBox.question(self, 'Help', HELP_TEXT, QtWidgets.QMessageBox.Ok)

    def _filterChanged(self):
        self._filterItems()

    def _filterItems(self):
        for i in range(self._materialsView.count()):
            self._setItemHidden(self._materialsView.item(i))

    def _setItemHidden(self, item):
        pattern = self._ui.filter.text().lower()
        if pattern == '':
            item.setHidden(False)
        else:
            item.setHidden(not pattern in item.text().lower())


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
