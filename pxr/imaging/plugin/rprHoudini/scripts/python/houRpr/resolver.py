from pxr import Plug

usd_plugin = Plug.Registry().GetPluginWithName('RenderStudioResolver')
if not usd_plugin.isLoaded:
    usd_plugin.Load()

from rs import RenderStudioKit


def SharedWorkspaceConnect():
    try:
        RenderStudioKit.SharedWorkspaceConnect()
    except:
        raise Exception('Can not connect to shared workspace server!')

def SharedWorkspaceDisconnect():
    try:
        RenderStudioKit.SharedWorkspaceDisconnect()
    except:
        raise Exception('Error on shared workspace disconnecting!')
