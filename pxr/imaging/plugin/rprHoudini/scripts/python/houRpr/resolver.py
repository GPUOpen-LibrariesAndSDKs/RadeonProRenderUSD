from pxr import Plug, Tf
from time import sleep

import hou
import os
import json

usd_plugin = Plug.Registry().GetPluginWithName('RenderStudioResolver')
if not usd_plugin.isLoaded:
    usd_plugin.Load()

from rs import RenderStudioKit

shared_workspace_enabled = False
shared_workspace_enable_expected = False

RESOLVER_SETTINGS_PATH = usd_plugin.MakeResourcePath("settings")
WORKSPACE_CONFIG_PATH = os.path.join(RESOLVER_SETTINGS_PATH, "workspace_config.json")
if not os.path.isdir(RESOLVER_SETTINGS_PATH):
    os.makedirs(RESOLVER_SETTINGS_PATH, exist_ok=True)


def _get_saved_config():
    if os.path.exists(WORKSPACE_CONFIG_PATH):
        with open(WORKSPACE_CONFIG_PATH) as f:
            return json.load(f)
    return {"url": RenderStudioKit.GetWorkspaceUrl(), "workdir": RenderStudioKit.GetWorkspacePath()}


def _save_config(config):
    with open(WORKSPACE_CONFIG_PATH, "w") as f:
        json.dump(config, f, indent=1)


def _workspace_enabled_callback(notice, sender):
    global shared_workspace_enabled
    global shared_workspace_enable_expected
    shared_workspace_enabled = notice.IsConnected()
    if shared_workspace_enable_expected and not shared_workspace_enabled:
        shared_workspace_enable_expected = False
        raise Exception('Shared workspace server is unavailable')
    shared_workspace_enable_expected = shared_workspace_enabled


listener_1 = Tf.Notice.RegisterGlobally("RenderStudioNotice::WorkspaceConnectionChanged", _workspace_enabled_callback)

RenderStudioKit.SetWorkspacePath(hou.expandString(_get_saved_config()["workdir"]))


def get_shared_workspace_menu():
    global shared_workspace_enabled
    if shared_workspace_enabled:
        return ["disable", "Disconnect from Render Studio shared workspace"]
    else:
        return ["enable", "Connect to Render Studio shared workspace"]


def toggle_shared_workspace(command):
    global shared_workspace_enabled
    global shared_workspace_enable_expected

    if command == "enable":
        current_workspace_url = RenderStudioKit.GetWorkspaceUrl()
        config = _get_saved_config()
        if current_workspace_url == "":
            current_workspace_url = config["url"]
        code, server_url = hou.ui.readInput("Storage server URL", buttons=('OK',),
                                            severity=hou.severityType.Message, help=None, title=None,
                                            initial_contents=current_workspace_url)
        if code == -1 or server_url == "":
            return
        if not server_url.startswith("http"):
            hou.ui.displayMessage("The URL should start from 'http://' or 'https://'",
                                  severity=hou.severityType.Warning)
            toggle_shared_workspace("enable")
            return
        try:
            shared_workspace_enable_expected = True
            with hou.InterruptableOperation("Connecting to shared workspace", open_interrupt_dialog=True) as op:
                RenderStudioKit.SetWorkspaceUrl(server_url)
                RenderStudioKit.SharedWorkspaceConnect()
                # wait 30 seconds
                for i in range(300):
                    sleep(0.1)
                    # if connected or connection error - break
                    if shared_workspace_enabled or not shared_workspace_enable_expected:
                        break
                if not shared_workspace_enabled:
                    toggle_shared_workspace("disable")
                else:
                    config["url"] = server_url
                    _save_config(config)
        except:
            toggle_shared_workspace("disable")
            raise Exception('Shared workspace server is unavailable')
    else:
        try:
            shared_workspace_enable_expected = False
            with hou.InterruptableOperation("Disconnecting from shared workspace", open_interrupt_dialog=True) as op:
                RenderStudioKit.SharedWorkspaceDisconnect()
        except:
            raise Exception('Error on shared workspace disconnecting')


def set_workspace_directory():
    config = _get_saved_config()
    directory = hou.ui.selectFile(
        title='Render Studio Workspace Directory',
        start_directory=config["workdir"].replace('\\', '/'),
        file_type=hou.fileType.Directory,
        chooser_mode=hou.fileChooserMode.Write)
    if directory:
        RenderStudioKit.SetWorkspacePath(os.path.normpath(directory))
        config["workdir"] = os.path.normpath(RenderStudioKit.GetWorkspacePath())
        _save_config(config)


def get_workspace_directory():
    return RenderStudioKit.GetWorkspacePath().replace('\\', '/')


def open_workspace_directory():
    os.system("explorer " + RenderStudioKit.GetWorkspacePath())
