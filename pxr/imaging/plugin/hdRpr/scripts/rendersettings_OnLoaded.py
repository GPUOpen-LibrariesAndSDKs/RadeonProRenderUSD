import hou, re
#hou.ui.displayMessage("I ran! I ran so far away!")

def is_old_format_settings_node(node):
    # check some parms, no need to check all
    return node.parm('renderQuality') \
        and node.parm('renderDevice') \
        and node.parm('minAdaptiveSamples') \
        and node.parm('maxRayDepthGlossyRefraction') \
        and node.parm('interactiveMaxRayDepth')


def replacer_name(stage, node):
    name = node.name() + '_'
    while stage.node(name):
        name += '_'
    return name


def parm_name_key_part(full_name):
    found = re.search('xn__(.+?)_', full_name)
    return found.group(1) if found else ''


def is_control(full_name, key_name):
    return key_name + '_control' in full_name


def copy_rpr_params(node, replacer):
    names = {
        "enableDenoising": "rprdenoisingenable",
        "maxSamples": "rprmaxSamples",
        "minAdaptiveSamples": "rpradaptiveSamplingminSamples",
        "varianceThreshold": "rpradaptiveSamplingnoiseTreshold",
        "maxRayDepth": "rprqualityrayDepth",
        "maxRayDepthDiffuse": "rprqualityrayDepthDiffuse",
        "maxRayDepthGlossy": "rprqualityrayDepthGlossy",
        "maxRayDepthRefraction": "rprqualityrayDepthRefraction",
        "maxRayDepthGlossyRefraction": "rprqualityrayDepthGlossyRefraction",
        "maxRayDepthShadow": "rprqualityrayDepthShadow",
        "raycastEpsilon": "rprqualityraycastEpsilon",
        "radianceClamping": "rprqualityradianceClamping",
        "interactiveMaxRayDepth": "rprqualityinteractiverayDepth"
    }
    parm_index = 0
    control_index = 1
    replacer_parms = {name: [None, None] for name in names.values()}
    for parm in replacer.parms():
        full_name = parm.name()
        key_name = parm_name_key_part(full_name)
        if key_name in replacer_parms:
            replacer_parms[key_name][control_index if is_control(full_name, key_name) else parm_index] = parm

    for src_name, dest_name in names.items():
        src = node.parm(src_name)
        src_control = node.parm(src_name + '_control')
        dest = replacer_parms[dest_name][parm_index]
        dest_control = replacer_parms[dest_name][control_index]
        if src and dest:
            try:
                dest.set(src.eval())
            except:
                print(src, '->', dest)
        if src_control and dest_control:
            try:
                dest_control.set(src_control.eval())
            except:
                print(src_control, '->', dest_control)
    
    
def copy_params(node, replacer):
    for src in node.parms():
        dest = replacer.parm(src.name())
        if dest:
            try:
                dest.set(src.eval())
            except:
                print('common', src, '->', dest)


node = kwargs["node"]
stage = hou.node("/stage")
if is_old_format_settings_node(node):
    replacer = stage.createNode('rendersettings', replacer_name(stage, node))
    replacer.setPosition(node.position() + hou.Vector2(0.1, -0.1))
    copy_params(node, replacer)
    copy_rpr_params(node, replacer)
    replacer.setInput(0, node.input(0))
    for out in node.outputs():
        for i in range(len(out.inputs())):
            if out.inputs()[i] == node:
                out.setInput(i, replacer)
    replacer.setDisplayFlag(True)

