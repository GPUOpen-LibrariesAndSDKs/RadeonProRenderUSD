import os
import re
import hou
import json
import shutil
from hutil.Qt import QtCore, QtGui, QtWidgets, QtUiTools

try:
  from pathlib import Path
except ImportError:
  from pathlib2 import Path

material_library=None

HOWTO_INSTALL_MD = '''
### How to install
1. Download MaterialX version of "Radeon ProRender Material Library" - [link](https://drive.google.com/file/d/1e2Qys1UMi9pu_x3wW5ctm9b8_FIohLKG/view?usp=sharing) (TODO: host somewhere)
2. Unzip to any directory - this will be our `INSTALL_DIR`.
3. Add `RPR_MTLX_MATERIAL_LIBRARY_PATH` environment variable value of which is `INSTALL_DIR`.
    This can be done in a few ways:
      * by modifying houdini.env file. [More info](https://www.sidefx.com/docs/houdini/basics/config_env.html) at "Setting environment variables"
      * by modifying `HOUDINI_USER_PREF_DIR/packages/RPR_for_Houdini.json` package file. [More info](https://www.sidefx.com/docs/houdini/ref/plugins.html)
      * by setting environment variable globally. [More info](https://superuser.com/questions/284342/what-are-path-and-other-environment-variables-and-how-can-i-set-or-use-them)
4. Restart Houdini
'''

# Generated from HOWTO_INSTALL_MD
HOWTO_INSTALL_HTML = '''
<h3 id="how-to-install">How to install</h3>
<ol>
<li>Download MaterialX version of &quot;Radeon ProRender Material Library&quot; - <a href="https://drive.google.com/file/d/1e2Qys1UMi9pu_x3wW5ctm9b8_FIohLKG/view?usp=sharing">link</a> (TODO: host somewhere)</li>
<li>Unzip to any directory - this will be our <code>INSTALL_DIR</code>.</li>
<li>Add <code>RPR_MTLX_MATERIAL_LIBRARY_PATH</code> environment variable value of which is <code>INSTALL_DIR</code>.
 This can be done in a few ways:<ul>
<li>by modifying houdini.env file. <a href="https://www.sidefx.com/docs/houdini/basics/config_env.html">More info</a> at &quot;Setting environment variables&quot;</li>
<li>by modifying <code>HOUDINI_USER_PREF_DIR/packages/RPR_for_Houdini.json</code> package file. <a href="https://www.sidefx.com/docs/houdini/ref/plugins.html">More info</a></li>
<li>by setting environment variable globally. <a href="https://superuser.com/questions/284342/what-are-path-and-other-environment-variables-and-how-can-i-set-or-use-them">More info</a></li>
</ul>
</li>
<li>Restart Houdini</li>
</ol>
'''

HELP_TEXT = '''
To import a material click on a corresponding swatch. The material is always imported as a separate "Material Library" LOP node.

The importer always auto-assigns an imported material to the last modified prims of the selected LOP nodes. In this mode, the importer replaces the previously imported material with the new one, thus allowing to rapidly change materials. 

Use the text input widget at the bottom of the window to filter displayed materials.
'''

class MaterialLibrary:
    class InitError(Exception):
        def __init__(self, brief_msg, full_msg):
            self.brief_msg = brief_msg
            self.full_msg = full_msg
            super(MaterialLibrary.InitError, self).__init__(brief_msg)


    class MaterialData:
        def __init__(self, dependencies, preview=None):
            self.dependencies = dependencies
            self.preview = preview


    def __init__(self):
        self._nopreview_icon = None
        self.material_groups = dict()
        self.material_datas = dict()

        self.path = os.environ.get('RPR_MTLX_MATERIAL_LIBRARY_PATH')
        if not self.path:
            raise MaterialLibrary.InitError(
                'Material Library is not installed.',
                HOWTO_INSTALL_HTML)

        materials_json_file = os.path.join(self.path, 'materials.json')
        try:
            with open(materials_json_file, 'r') as materials_json:
                materials = json.load(materials_json)
                for group, descriptors in materials.items():
                    material_names_per_group = list()
                    for desc in descriptors:
                        material_name = desc['name']

                        previewPath = os.path.join(self.path, 'Swatches', material_name)

                        # TODO: loading previews takes 99% of __init__ (1 sec in my case), delay loading until we really need to render it
                        preview = None
                        if os.path.exists(previewPath + '.jpg'):
                            preview = QtGui.QIcon(previewPath + '.jpg')
                        elif os.path.exists(previewPath + '.png'):
                            preview = QtGui.QIcon(previewPath + '.png')

                        if not preview:
                            if not self._nopreview_icon:
                                script_dir = os.path.dirname(os.path.realpath(__file__))
                                self._nopreview_icon = QtGui.QIcon(os.path.join(script_dir, 'na.png'))
                            preview = self._nopreview_icon

                        self.material_datas[material_name] = self.MaterialData(desc['dependencies'], preview)
                        material_names_per_group.append(material_name)

                    self.material_groups[group] = material_names_per_group

        except IOError as e:
            raise MaterialLibrary.InitError(
                'Corrupted Material Library.',
                'materials.json could not be read.')
        except ValueError:
            raise MaterialLibrary.InitError(
                'Corrupted Material Library',
                'Invalid materials.json.')

    MATERIAL_LIBRARY_TAG='hdrpr_material_library_generated'

    def create_houdini_material_graph(self, material_name):
        matlib_node = hou.node('/stage').createNode('materiallibrary')
        matlib_node.setName(material_name, unique_name=True)
        matlib_node.setComment(self.MATERIAL_LIBRARY_TAG)

        mtlx_node = matlib_node.createNode('RPR::rpr_materialx_node')
        mtlx_node.setName(material_name, unique_name=True)

        src_mtlx_dir = os.path.join(self.path, 'Materials')
        src_mtlx_file = os.path.join(src_mtlx_dir, material_name + '.mtlx')

        # TODO: should we allow to disable this behavior?
        # Copy material file and its dependencies
        if True:
            hip_dir = os.path.dirname(hou.hipFile.path())
            dst_mtlx_dir = os.path.join(hip_dir, 'RPRMaterialLibrary', 'Materials')
            Path(dst_mtlx_dir).mkdir(parents=True, exist_ok=True)

            dst_mtlx_file = os.path.join(dst_mtlx_dir, material_name + '.mtlx')

            shutil.copyfile(src_mtlx_file, dst_mtlx_file)

            material_data = self.material_datas[material_name]
            for dependency in material_data.dependencies:
                dst_dep_file = os.path.join(dst_mtlx_dir, dependency)
                Path(dst_dep_file).parent.mkdir(parents=True, exist_ok=True)

                src_dep_file = os.path.join(src_mtlx_dir, dependency)
                shutil.copyfile(src_dep_file, dst_dep_file)

            mtlx_node.parm('file').set(dst_mtlx_file)

        else:
            mtlx_node.parm('file').set(src_mtlx_file)

        selected_nodes = hou.selectedNodes()
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
                if connect_node.comment() == self.MATERIAL_LIBRARY_TAG and \
                    connect_node.parm('geopath1').evalAsString() == material_assignment:
                    connect_node.destroy()

                    viewer_node = matlib_node.network().viewerNode()
                    connect_node = viewer_node

            # Insert our new node into existing connections
            for connection in connect_node.outputConnections():
                if connection.outputNode().comment() == self.MATERIAL_LIBRARY_TAG:
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

###############################################################################

class StageMaterial:
    def __init__(self, name, preview):
        self.name = name
        self.preview = preview

class StageMaterialNode:
    def __init__(self, parent, name, preview):
        self._parent = parent
        self._material = StageMaterial(name, preview)


    def remove(self):
        self._parent = None


    def _generatePreview(self):
        pass


    def addMaterial(self):
        # we sould not get here!
        pass


    def parent(self):
        return self._parent


    def child(self, row):
        return None


    def children(self):
        return []


    def row(self):
        if self._parent is not None:
            return self._parent._children.index(self)
        return -1


    def childCount(self):
        return 0


    def name(self):
        return self._material.name


    def displayName(self):
        return self._material.name.replace('_', ' ')


    def getMaterials(self) -> [StageMaterial]:
        return [self._material]

###############################################################################

class StageMaterialGroupNode:
    def __init__(self, parent, name:str):
        self._parent = parent
        self._name = name
        self._children = []


    def remove(self):
        self._removeChildren()
        self._parent = None


    def _removeChildren(self):
        for c in self._children:
            c.remove()
        self._children = []


    def addMaterial(self, pathParts:[str], preview):
        if len(pathParts) == 0:
            return
        elif len(pathParts) == 1:
            node = StageMaterialNode(self, pathParts[0], preview)
            self._children = self._children + [node]
        else:
            childName = pathParts[0]
            for c in self._children:
                if c.name() == childName:
                    c.addMaterial(pathParts[1:], preview)
                    return

            child = StageMaterialGroupNode(self, childName)
            child.addMaterial(pathParts[1:], preview)

            self._children = self._children + [child]


    def parent(self):
        return self._parent


    def child(self, row):
        return self._children[row]


    def children(self):
        return self._children


    def row(self):
        if self._parent is not None:
            return self._parent._children.index(self)
        return -1


    def childCount(self):
        return len(self._children)


    def name(self):
        return self._name


    def displayName(self):
        return self._name.replace('_', ' ')


    def getMaterials(self) -> [StageMaterial]:
        materials = []
        for c in self._children:
            materials = materials + c.getMaterials()
        return materials

###############################################################################

PREVIEW_SIZE = 200
MIN_TREE_ITEM_HEIGHT = 25

class LibraryTreeModel(QtCore.QAbstractItemModel):
    def __init__(self, parent, minRowHeight=MIN_TREE_ITEM_HEIGHT):
        QtCore.QAbstractItemModel.__init__(self, parent)
        self._root = StageMaterialGroupNode(None, 'All')
        self._minRowHeight = minRowHeight


    def __del__(self):
        if self._root:
            self._root.remove()


    def clear(self):
        self.beginResetModel()
        self._root._removeChildren()
        self.endResetModel()


    def addItem(self, pathParts:[str], preview):
        self._root.addMaterial(pathParts, preview)


    def reset(self):
        self.beginResetModel()
        self.endResetModel()


    def data(self, index: QtCore.QModelIndex, role=QtCore.Qt.DisplayRole):
        if index.isValid():
            node = index.internalPointer()
            if role in (QtCore.Qt.DisplayRole, QtCore.Qt.EditRole):
                return node.displayName()
            elif role in (QtCore.Qt.DecorationRole, ):
                pass
            elif role in (QtCore.Qt.SizeHintRole, ):
                return QtCore.QSize(100, max(MIN_TREE_ITEM_HEIGHT, self._minRowHeight))


    def flags(self, index: QtCore.QModelIndex):
        return QtCore.Qt.ItemIsEnabled | QtCore.Qt.ItemIsSelectable
            

    def index(self, row, column, parent):
        if not self.hasIndex(row, column, parent):
            return QtCore.QModelIndex()
        node = parent.internalPointer() if parent.isValid() else self._root
        if node.children:
            return self.createIndex(row, column, node.child(row))
        else:
            return QtCore.QModelIndex()


    def parent(self, child):
        if not child.isValid():
            return QtCore.QModelIndex()
        node = child.internalPointer()
        if node.row() >= 0:
            return self.createIndex(node.row(), 0, node.parent())
        return QtCore.QModelIndex()


    def rowCount(self, parent=QtCore.QModelIndex()):
        node = parent.internalPointer() if parent.isValid() else self._root
        if node is not None:
            return node.childCount()
        else:
            return 0


    def columnCount(self, parent=QtCore.QModelIndex()):
        return 1


    def getSubItems(self, index) -> [StageMaterial]:
        node = index.internalPointer()
        if node:
            return node.getMaterials()


###############################################################################

def IsMouseOnWidget(widget):
    mouse_pos_global = QtGui.QCursor.pos()
    mouse_pos_local = widget.mapFromGlobal(mouse_pos_global)
    return widget.rect().contains(mouse_pos_local)

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
    def __init__(self, material_library):
        super(MaterialLibraryWidget, self).__init__()

        self._material_library = material_library
        self._fullPreviewWindow = FullPreviewWindow()

        script_dir = os.path.dirname(os.path.abspath(__file__))
        ui_filepath = os.path.join(script_dir, 'materialLibrary.ui')
        self._ui = QtUiTools.QUiLoader().load(ui_filepath, parentWidget=self)

        self._layout = QtWidgets.QVBoxLayout()
        self.setLayout(self._layout)
        self._layout.setContentsMargins(0, 0, 0, 0)
        self._layout.addWidget(self._ui)

        self._ui.filter.textChanged.connect(self._filterChanged)

        self._listWidget = LibraryListWidget(self)
        self._fullPreviewWindow._requiredWidget = self._listWidget
        self._ui.verticalLayout_3.insertWidget(0, self._listWidget)

        self._ui.treeView.clicked.connect(self._treeItemClicked)
        self._listWidget.clicked.connect(self._listItemClicked)
        self._listWidget.itemEntered.connect(self._listItemEntered)
        self._listWidget.viewportEntered.connect(self._listViewportEntered)
        self._listWidget.setMouseTracking(True)

        self._ui.splitter.setSizes([150, 150])

        self._libraryTreeModel = LibraryTreeModel(self)      # tree item height must not be less than line edit height
        self._ui.treeView.setModel(self._libraryTreeModel)

        for group, materials in material_library.material_groups.items():
            for name in materials:
                self._libraryTreeModel.addItem(['All', group, name], material_library.material_datas[name].preview)

        self._libraryTreeModel.reset()
        self._ui.treeView.setIndentation(self._ui.treeView.indentation() // 2)
        rootIndex = self._libraryTreeModel.index(0, 0, QtCore.QModelIndex())
        self._treeItemClicked(rootIndex)
        self._ui.treeView.expand(rootIndex)

        self._ui.helpButton.clicked.connect(self._helpButtonClicked)

        self._dataModel = None
        self._stateLoaded = False

    def closeEvent(self, event):
        self.setParent(None)

    def _treeItemClicked(self, index):
        items = self._libraryTreeModel.getSubItems(index)
        self._listWidget.clear()
        for item in items:
            listItem = QtWidgets.QListWidgetItem(item.preview, item.name.replace('_', ' '), self._listWidget)
            listItem.setSizeHint(QtCore.QSize(PREVIEW_SIZE + 10, PREVIEW_SIZE + 40))
            self._setItemHidden(listItem)
            self._listWidget.addItem(listItem)

    def _listItemClicked(self, index):
        item = self._listWidget.item(index.row())
        self._material_library.create_houdini_material_graph(item.text().replace(' ', '_'))

    def _listItemEntered(self, item):
        self._fullPreviewWindow.setIcon(item.text(), item.icon())

    def _listViewportEntered(self):
        self._fullPreviewWindow.hide()

    def _helpButtonClicked(self):
        QtWidgets.QMessageBox.question(self, 'Help', HELP_TEXT, QtWidgets.QMessageBox.Ok)

    def _filterChanged(self):
        self._filterItems()

    def _filterItems(self):
        for i in range(self._listWidget.count()):
            self._setItemHidden(self._listWidget.item(i))

    def _setItemHidden(self, item):
        pattern = self._ui.filter.text().lower()
        if pattern == '':
            item.setHidden(False)
        else:
            item.setHidden(not pattern in item.text().lower())

class MaterialLibraryWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super(MaterialLibraryWindow, self).__init__()

        matLibWidget = MaterialLibraryWidget(material_library)

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
    global material_library
    if not material_library:
        try:
            material_library = MaterialLibrary()
        except MaterialLibrary.InitError as e:
            msg = QtWidgets.QMessageBox(hou.qt.mainWindow())
            msg.setIcon(QtWidgets.QMessageBox.Critical)
            msg.setText(e.brief_msg)
            msg.setInformativeText(e.full_msg)
            msg.setWindowTitle("Error")
            msg.show()
            return

    window = MaterialLibraryWindow()
    window.show()
