from pxr import Plug
from time import sleep

import hou

usd_plugin = Plug.Registry().GetPluginWithName('RenderStudioResolver')
if not usd_plugin.isLoaded:
    usd_plugin.Load()

from rs import RenderStudioKit

shared_workspace_enabled = False

def get_shared_workspace_menu():
    global shared_workspace_enabled
    if shared_workspace_enabled:
        return ["disable", "Disconnect from Render Studio shared workspace"]
    else:
        return ["enable", "Connect to Render Studio shared workspace"]

def toggle_shared_workspace(command):
    global shared_workspace_enabled


    if command == "enable":
        try:
            with hou.InterruptableOperation("Connecting to shared workspace", open_interrupt_dialog=True) as op:
                RenderStudioKit.SharedWorkspaceConnect()
                shared_workspace_enabled = True
        except hou.OperationInterrupted:
            toggle_shared_workspace("disable")
        except:
            toggle_shared_workspace("disable")
            raise Exception('Shared workspace server is unavailable')
    else:
        try:
            with hou.InterruptableOperation("Disconnecting from shared workspace", open_interrupt_dialog=True) as op:
                RenderStudioKit.SharedWorkspaceDisconnect()
                shared_workspace_enabled = False
        except hou.OperationInterrupted:
            pass
        except:
            raise Exception('Error on shared workspace disconnecting')

