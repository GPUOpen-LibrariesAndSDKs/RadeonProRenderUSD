import hou
from hutil.Qt import QtCore
from rpr import RprUsd

def open_configuration_window():
    RprUsd.devicesConfiguration.open_window(hou.ui.mainQtWindow(), QtCore.Qt.Window)
