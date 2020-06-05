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
import platform

houdini_ds_template = (
'''
#include "$HFS/houdini/soho/parameters/CommonMacros.ds"

{{
    name    "RPR"
    label   "RPR"
    parmtag {{ spare_opfilter    "!!SHOP/PROPERTIES!!" }}
    parmtag {{ spare_classtags   "render" }}

    {houdini_params}
}}

''').strip()

control_param_template = (
'''
parm {{
    name    "{name}"
    label   "{label}"
    type    string
    default {{ "none" }}
    {hidewhen}
    menujoin {{
        [ "import loputils" ]
        [ "return loputils.createEditPropertiesControlMenu(kwargs, '{controlled_type}[]')" ]
        language python
    }}
}}
'''
)

def generate_houdini_ds(install_path, ds_name, settings):
    import hou

    houdini_params = ''

    for category in settings:
        disabled_category = False
        if 'disabled_platform' in category:
            if platform.system() in category['disabled_platform']:
                disabled_category = True

        category_name = category['name']

        for setting in category['settings']:
            if not 'ui_name' in setting:
                continue

            houdini_hidewhen_conditions = []
            if 'houdini' in category:
                houdini_hidewhen_conditions.append(category['houdini'].get('hidewhen'))

            houdini_settings = setting.get('houdini', {})
            houdini_param_label = setting['ui_name']
            if 'hidewhen' in houdini_settings:
                houdini_hidewhen_conditions.append(houdini_settings.get('hidewhen'))
            houdini_hidewhen = ''
            if houdini_hidewhen_conditions:
                houdini_hidewhen += 'hidewhen "'
                for condition in houdini_hidewhen_conditions:
                    if condition:
                        if callable(condition):
                            condition = condition(settings)
                        houdini_hidewhen += '{{ {} }} '.format(condition)
                houdini_hidewhen += '"'

            def CreateHoudiniParam(name, label, htype, default, values=[], hints=[], tags=[], disablewhen_conditions=[], size=None, valid_range=None, help_msg=None):
                param = 'parm {\n'
                param += '    name "{}"\n'.format(hou.encode(name))
                param += '    label "{}"\n'.format(label)
                param += '    type {}\n'.format(htype)
                if size: param += '    size {}\n'.format(size)
                param += '    default {{ {} }}\n'.format(default)
                for tag in tags:
                    param += '    parmtag {{ {} }}\n'.format(tag)
                if values:
                    param += '    menu {\n'
                    for value in values:
                        param += '        "{}" "{}"\n'.format(value[0], value[1])
                    param += '    }\n'
                if hints:
                    param += '    menureplace {\n'
                    for hint in hints:
                        param += '        R"({})" "{}"\n'.format(hint[0], hint[1])
                    param += '    }\n'
                if disabled_category:
                    param += '    invisible\n'
                if houdini_hidewhen:
                    param += '    {}\n'.format(houdini_hidewhen)
                if disablewhen_conditions:
                    param += '    disablewhen "'
                    for condition in disablewhen_conditions:
                        param += '{{ {} }} '.format(condition)
                    param += '"\n'
                if valid_range:                    
                    param += '    range {{ {}! {} }}\n'.format(valid_range[0], valid_range[1])
                if help_msg:
                    param += '    help "{}"\n'.format(help_msg)
                param += '}\n'

                return param

            name = setting['name']

            control_param_name = hou.encode(name + '_control')

            render_param_values = []
            default_value = setting['defaultValue']
            c_type_str = type(default_value).__name__
            controlled_type = c_type_str
            if c_type_str == 'str':
                c_type_str = 'TfToken'
                controlled_type = 'string'
            render_param_type = c_type_str
            render_param_default = default_value
            if isinstance(default_value, bool):
                render_param_type = 'toggle'
                render_param_default = 1 if default_value else 0
            elif 'values' in setting:
                default_value = setting['values'].index(default_value)
                render_param_default = default_value
                c_type_str = type(default_value).__name__
                render_param_type = 'ordinal'
                for value in setting['values']:
                    render_param_values.append((len(render_param_values), value))

            render_param_range = None
            if 'minValue' in setting and 'maxValue' in setting and not 'values' in setting:
                render_param_range = (setting['minValue'], setting['maxValue'])

            houdini_params += control_param_template.format(
                name=control_param_name,
                label=houdini_param_label,
                controlled_type=controlled_type,
                hidewhen=houdini_hidewhen)
            houdini_params += CreateHoudiniParam(name, houdini_param_label, render_param_type, render_param_default,
                values=render_param_values,
                hints=setting['hints'] if 'hints' in setting else [],
                tags=[
                    '"spare_category" "{}"'.format(category_name),
                    '"uiscope" "viewport"',
                    '"usdvaluetype" "{}"'.format(c_type_str)
                ] + houdini_settings.get('custom_tags', []),
                disablewhen_conditions=[
                    control_param_name + ' == block',
                    control_param_name + ' == none',
                ],
                size=1,
                valid_range=render_param_range,
                help_msg=setting.get('help', None))

    if houdini_params:
        houdini_ds_dst_path = os.path.join(install_path, 'HdRprPlugin_{}.ds'.format(ds_name))
        houdini_ds_file = open(houdini_ds_dst_path, 'w')
        houdini_ds_file.write(houdini_ds_template.format(houdini_params=houdini_params))
