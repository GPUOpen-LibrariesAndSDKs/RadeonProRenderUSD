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
import json

from . import _rprUsd as RprUsd

try:
    from hutil.Qt import QtCore, QtGui, QtWidgets
except:
    from pxr.Usdviewq.qt import QtCore, QtGui, QtWidgets

# from rpr import RprUsd

_devices_info = None
def _setup_devices_info():
    global _devices_info
    if _devices_info == None:
        _devices_info = dict()
        for plugin_type in RprUsd.PluginType.allValues[1:]:
            try:
                devices_info = RprUsd.GetDevicesInfo(plugin_type)
                if devices_info.isValid:
                    _devices_info[plugin_type] = devices_info
            except:
                pass


class _GpuConfiguration:
    def __init__(self, is_enabled, gpu_info):
        self.is_enabled = is_enabled
        self.gpu_info = gpu_info

    def serialize(self):
        return {
            'is_enabled': self.is_enabled,
            'gpu_info': {
                'index': self.gpu_info.index,
                'name': self.gpu_info.name
            }
        }

    @staticmethod
    def deserialize(data):
        return _GpuConfiguration(is_enabled=data['is_enabled'],
                                 gpu_info=RprUsd.GPUDeviceInfo(data['gpu_info']['index'], data['gpu_info']['name']))

    def __eq__(self, other):
        return self.is_enabled == other.is_enabled and \
               self.gpu_info == other.gpu_info

    def __ne__(self, other):
        return not self == other

    def deepcopy(self):
        return _GpuConfiguration(is_enabled=self.is_enabled,
                                 gpu_info=self.gpu_info)


class _CpuConfiguration:
    def __init__(self, num_active_threads, cpu_info):
        self.num_active_threads = num_active_threads
        self.cpu_info = cpu_info

    def serialize(self):
        return {
            'num_active_threads': self.num_active_threads,
            'cpu_info': {
                'num_threads': self.cpu_info.numThreads
            }
        }

    @staticmethod
    def deserialize(data):
        return _CpuConfiguration(num_active_threads=data['num_active_threads'],
                                 cpu_info=RprUsd.CPUDeviceInfo(data['cpu_info']['num_threads']))

    def __eq__(self, other):
        return self.num_active_threads == other.num_active_threads and \
               self.cpu_info == other.cpu_info

    def __ne__(self, other):
        return not self == other

    def deepcopy(self):
        return _CpuConfiguration(num_active_threads=self.num_active_threads,
                                 cpu_info=self.cpu_info)


class _PluginConfiguration:
    def __init__(self, plugin_type, cpu_config, gpu_configs):
        self.plugin_type = plugin_type
        self.cpu_config = cpu_config
        self.gpu_configs = gpu_configs

    @staticmethod
    def default(plugin_type, plugin_devices_info):
        gpu_configs = [_GpuConfiguration(is_enabled=idx==0, gpu_info=gpu_info) for idx, gpu_info in enumerate(plugin_devices_info.gpus)]
        cpu_config = _CpuConfiguration(num_active_threads=plugin_devices_info.cpu.num_threads if not gpu_configs else 0, cpu_info=plugin_devices_info.cpu)
        return _PluginConfiguration(plugin_type=plugin_type, cpu_config=cpu_config, gpu_configs=gpu_configs)

    def serialize(self):
        return {
            'plugin_type': self.plugin_type.name,
            'cpu_config': self.cpu_config.serialize(),
            'gpu_configs': [gpu.serialize() for gpu in self.gpu_configs]
        }

    @staticmethod
    def deserialize(data):
        return _PluginConfiguration(plugin_type=RprUsd.PluginType.GetValueFromName(data['plugin_type']),
                                    cpu_config=_CpuConfiguration.deserialize(data['cpu_config']),
                                    gpu_configs=[_GpuConfiguration.deserialize(gpu_data) for gpu_data in data['gpu_configs']])

    def __eq__(self, other):
        return self.plugin_type == other.plugin_type and \
               self.cpu_config == other.cpu_config and \
               self.gpu_configs == other.gpu_configs

    def __ne__(self, other):
        return not self == other

    def deepcopy(self):
        return _PluginConfiguration(plugin_type=self.plugin_type,
                                    cpu_config=self.cpu_config.deepcopy(),
                                    gpu_configs=[config.deepcopy() for config in self.gpu_configs])


class _Configuration:
    def __init__(self, context, plugin_configurations=list()):
        self.context = context
        self.plugin_configurations = plugin_configurations

    def is_outdated(self):
        for plugin_configuration in self.plugin_configurations:
            devices_info = _devices_info[plugin_configuration.plugin_type]

            config_gpu_infos = [gpu_config.gpu_info for gpu_config in plugin_configuration.gpu_configs]
            if plugin_configuration.cpu_config.cpu_info == devices_info.cpu and \
               config_gpu_infos == devices_info.gpus:
               continue
            return True
        return False

    @staticmethod
    def load(context, file):
        try:
            with open(file) as f:
                j = json.load(f)
                plugin_configurations = [_PluginConfiguration.deserialize(data) for data in j]
                configuration = _Configuration(context=context, plugin_configurations=plugin_configurations)
                if not configuration.is_outdated():
                    return configuration
                else:
                    print('Hardware change detected. Falling back to default device configuration.')
        except IOError:
            pass
        except (ValueError, KeyError) as e:
            context.show_error('Failed to load device configuration', 'Error: "{}". Falling back to default device configuration.\n'.format(e.msg))

        return _Configuration.default(context)

    def save(self, file):
        try:
            with open(file, 'w') as f:
                serialized_data = [config.serialize() for config in self.plugin_configurations]
                json.dump(serialized_data, f)
                return True
        except IOError as e:
            self.context.show_error('Failed to save device configuration', e.msg)
        return False

    @staticmethod
    def default(context):
        plugin_configurations = list()
        for plugin_type in [RprUsd.kPluginNorthstar, RprUsd.kPluginTahoe, RprUsd.kPluginHybrid]:
            if plugin_type in _devices_info:
                plugin_configurations.append(_PluginConfiguration.default(plugin_type, _devices_info[plugin_type]))
        return _Configuration(context=context, plugin_configurations=plugin_configurations)

    def __eq__(self, other):
        return self.plugin_configurations == other.plugin_configurations

    def __ne__(self, other):
        return not self == other

    def deepcopy(self):
        return _Configuration(context=self.context, plugin_configurations=[config.deepcopy() for config in self.plugin_configurations])


BORDERSIZE=2
BORDERRADIUS=10
BORDERCOLOR = QtGui.QColor(42, 42, 42)

class BorderWidget(QtWidgets.QFrame):
    def __init__(self, borderradius=BORDERRADIUS, bordersize=BORDERSIZE, bordercolor=BORDERCOLOR, parent=None):
        super(BorderWidget, self).__init__(parent)
        color = '{}, {}, {}'.format(bordercolor.red(), bordercolor.green(), bordercolor.blue())
        self.setObjectName('BorderWidget')
        self.setStyleSheet('QFrame#BorderWidget {{ border-radius: {radius}px; border: {size}px; border-style: solid; border-color: rgb({color}) }}'.format(radius=borderradius, size=bordersize, color=color))


class _DeviceWidget(BorderWidget):
    on_change = QtCore.Signal()

    def __init__(self, cpu_config=None, gpu_config=None, parent=None):
        super(_DeviceWidget, self).__init__(parent=parent)

        self.cpu_config = cpu_config
        self.gpu_config = gpu_config

        if gpu_config:
            self.device_name_label = QtWidgets.QLabel(self)
            self.device_name_label.setText('GPU "{}"'.format(gpu_config.gpu_info.name))

            self.main_layout = QtWidgets.QHBoxLayout(self)
            self.main_layout.addWidget(self.device_name_label)

            self.main_layout.addStretch()

            self.is_enabled_check_box = QtWidgets.QCheckBox(self)
            self.is_enabled_check_box.setChecked(gpu_config.is_enabled)
            self.is_enabled_check_box.stateChanged.connect(self.on_gpu_update)
            self.main_layout.addWidget(self.is_enabled_check_box)

        elif cpu_config:
            self.name_container_widget = QtWidgets.QWidget(self)
            self.name_container_layout = QtWidgets.QHBoxLayout(self.name_container_widget)
            self.name_container_layout.setContentsMargins(0, 0, 0, 0)

            self.name_label = QtWidgets.QLabel(self.name_container_widget)
            self.name_label.setText('CPU')
            self.name_container_layout.addWidget(self.name_label)

            self.name_container_layout.addStretch()

            is_cpu_enabled = cpu_config.num_active_threads > 0

            self.is_enabled_check_box = QtWidgets.QCheckBox(self.name_container_widget)
            self.is_enabled_check_box.setChecked(is_cpu_enabled)
            self.is_enabled_check_box.stateChanged.connect(self.on_cpu_enabled_update)
            self.name_container_layout.addWidget(self.is_enabled_check_box)

            self.num_threads_container_widget = QtWidgets.QWidget(self)
            self.num_threads_container_layout = QtWidgets.QHBoxLayout(self.num_threads_container_widget)
            self.num_threads_container_layout.setContentsMargins(0, 0, 0, 0)

            self.num_threads_label = QtWidgets.QLabel(self.num_threads_container_widget)
            self.num_threads_label.setText('Number of Threads')
            self.num_threads_container_layout.addWidget(self.num_threads_label)

            self.num_threads_container_layout.addStretch()

            self.num_threads_spin_box = QtWidgets.QSpinBox(self.num_threads_container_widget)
            self.num_threads_spin_box.setValue(cpu_config.num_active_threads)
            self.num_threads_spin_box.setRange(1, cpu_config.cpu_info.numThreads)
            self.num_threads_spin_box.valueChanged.connect(self.on_cpu_num_threads_update)
            if not is_cpu_enabled:
                self.num_threads_container_widget.hide()
            self.num_threads_container_layout.addWidget(self.num_threads_spin_box)

            self.main_layout = QtWidgets.QVBoxLayout(self)
            self.main_layout.addWidget(self.name_container_widget)
            self.main_layout.addWidget(self.num_threads_container_widget)

        self.main_layout.setContentsMargins(
            self.main_layout.contentsMargins().left() // 2,
            self.main_layout.contentsMargins().top() // 4,
            self.main_layout.contentsMargins().right() // 2,
            self.main_layout.contentsMargins().bottom() // 4)

    def on_gpu_update(self, is_enabled):
        self.gpu_config.is_enabled = bool(is_enabled)
        self.on_change.emit()

    def on_cpu_enabled_update(self, is_enabled):
        if is_enabled:
            self.cpu_config.num_active_threads = self.cpu_config.cpu_info.numThreads
            self.num_threads_spin_box.setValue(self.cpu_config.num_active_threads)
            self.num_threads_container_widget.show()
        else:
            self.cpu_config.num_active_threads = 0
            self.num_threads_container_widget.hide()
        self.on_change.emit()

    def on_cpu_num_threads_update(self, num_threads):
        self.cpu_config.num_active_threads = num_threads
        self.on_change.emit()


class _PluginConfigurationWidget(BorderWidget):
    on_change = QtCore.Signal()

    def __init__(self, plugin_configuration, parent):
        super(_PluginConfigurationWidget, self).__init__(parent=parent)
        self.plugin_configuration = plugin_configuration

        self.main_layout = QtWidgets.QVBoxLayout(self)
        self.main_layout.setContentsMargins(
            self.main_layout.contentsMargins().left() // 2,
            self.main_layout.contentsMargins().top() // 2,
            self.main_layout.contentsMargins().right() // 2,
            self.main_layout.contentsMargins().bottom() // 2)

        self.plugin_type = plugin_configuration.plugin_type
        if self.plugin_type == RprUsd.kPluginHybrid:
            plugin_qualities = 'Low-High Qualities'
        elif self.plugin_type == RprUsd.kPluginTahoe:
            plugin_qualities = 'Full (Legacy) Quality'
        elif self.plugin_type == RprUsd.kPluginNorthstar:
            plugin_qualities = 'Full Quality'

        self.labels_widget = QtWidgets.QWidget(self)
        self.main_layout.addWidget(self.labels_widget)

        self.labels_widget_layout = QtWidgets.QHBoxLayout(self.labels_widget)
        self.labels_widget_layout.setContentsMargins(0, 0, 0, 0)

        self.plugin_qualities_label = QtWidgets.QLabel(self.labels_widget)
        self.plugin_qualities_label.setText(plugin_qualities)
        self.labels_widget_layout.addWidget(self.plugin_qualities_label)

        self.incomplete_config_label = QtWidgets.QLabel(self.labels_widget)
        self.incomplete_config_label.setStyleSheet('color: rgb(255,204,0)')
        self.incomplete_config_label.setText('Configuration is incomplete: no devices')
        self.labels_widget_layout.addWidget(self.incomplete_config_label)
        self.incomplete_config_label.hide()

        if plugin_configuration.cpu_config.cpu_info.numThreads:
            cpu_widget = _DeviceWidget(parent=self, cpu_config=plugin_configuration.cpu_config)
            cpu_widget.on_change.connect(self.on_device_change)
            self.main_layout.addWidget(cpu_widget)

        for gpu_config in plugin_configuration.gpu_configs:
            gpu_widget = _DeviceWidget(parent=self, gpu_config=gpu_config)
            gpu_widget.on_change.connect(self.on_device_change)
            self.main_layout.addWidget(gpu_widget)

        self.is_complete = True

    def on_device_change(self):
        self.is_complete = self.plugin_configuration.cpu_config.num_active_threads > 0 or \
                           any([gpu_config.is_enabled for gpu_config in self.plugin_configuration.gpu_configs])

        if self.is_complete:
            self.incomplete_config_label.hide()
        else:
            self.incomplete_config_label.show()

        self.on_change.emit()


class _DevicesConfigurationDialog(QtWidgets.QDialog):
    def __init__(self, configuration, show_restart_warning):
        super(_DevicesConfigurationDialog, self).__init__()

        self.configuration = configuration
        self.initial_configuration = configuration.deepcopy()

        if self.configuration.context.parent:
            self.setParent(self.configuration.context.parent, self.configuration.context.parent_flags)
        self.setSizePolicy(QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Fixed)
        self.setWindowTitle('RPR Render Devices')

        self.main_layout = QtWidgets.QVBoxLayout(self)
        self.main_layout.setSizeConstraint(QtWidgets.QLayout.SetFixedSize)

        self.plugin_configuration_widgets = list()
        for config in configuration.plugin_configurations:
            widget = _PluginConfigurationWidget(config, self)
            widget.on_change.connect(self.on_plugin_configuration_change)
            self.main_layout.addWidget(widget)
            self.plugin_configuration_widgets.append(widget)

        if show_restart_warning:
            self.restart_warning_label = QtWidgets.QLabel(self)
            self.restart_warning_label.setText('Changes will not take effect until the RPR renderer restarts.')
            self.restart_warning_label.setStyleSheet('color: rgb(255,204,0)')
            self.restart_warning_label.hide()
            self.main_layout.addWidget(self.restart_warning_label)
        else:
            self.restart_warning_label = None

        self.button_box = QtWidgets.QDialogButtonBox(self)
        self.save_button = self.button_box.addButton("Save", QtWidgets.QDialogButtonBox.AcceptRole)
        self.save_button.setEnabled(False)
        self.button_box.addButton("Cancel", QtWidgets.QDialogButtonBox.RejectRole)
        self.button_box.accepted.connect(self.on_accept)
        self.button_box.rejected.connect(self.on_reject)
        self.main_layout.addWidget(self.button_box)

        self.show()

        self.should_update_configuration = False

    def on_reject(self):
        self.close()

    def on_accept(self):
        self.should_update_configuration = True
        self.close()

    def on_plugin_configuration_change(self):
        is_save_enabled = all([widget.is_complete for widget in self.plugin_configuration_widgets]) and \
                          self.initial_configuration != self.configuration
        self.save_button.setEnabled(is_save_enabled)

        if self.restart_warning_label:
            if is_save_enabled:
                self.restart_warning_label.show()
            else:
                self.restart_warning_label.hide()


def open_window(parent=None, parent_flags=QtCore.Qt.Widget, show_restart_warning=True):
    _setup_devices_info()

    class Context:
        def __init__(self, parent, parent_flags):
            self.parent = parent
            self.parent_flags = parent_flags

        def show_error(title, text):
            msg = QtWidgets.QMessageBox()
            msg.setParent(self.parent, self.parent_flags)
            msg.setIcon(QtWidgets.QMessageBox.Critical)
            msg.setText(text)
            msg.setWindowTitle(title)
            msg.show()

    configuration_filepath = RprUsd.Config.GetDeviceConfigurationFilepath()
    configuration = _Configuration.load(Context(parent, parent_flags), configuration_filepath)

    dialog = _DevicesConfigurationDialog(configuration, show_restart_warning)
    dialog.exec_()

    if dialog.should_update_configuration:
        return configuration.save(configuration_filepath)

    return False
