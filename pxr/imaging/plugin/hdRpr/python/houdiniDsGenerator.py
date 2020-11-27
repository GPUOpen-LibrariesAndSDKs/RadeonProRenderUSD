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

def _get_valid_houdini_param_name(name):
    if all(c.isalnum() or c == '_' for c in name):
        return name
    else:
        import hou
        return hou.encode(name)

def _get_houdini_hidewhen_string(conditions, settings):
    houdini_hidewhen_conditions = []
    for condition in conditions:
        if condition and callable(condition):
            condition = condition(settings)
        if condition:
            if isinstance(condition, str):
                houdini_hidewhen_conditions.append(condition)
            elif isinstance(condition, list):
                houdini_hidewhen_conditions.extend(condition);

    houdini_hidewhen = ''
    if houdini_hidewhen_conditions:
        houdini_hidewhen += 'hidewhen "'
        for condition in houdini_hidewhen_conditions:
            houdini_hidewhen += '{{ {} }} '.format(condition)
        houdini_hidewhen += '"'
    return houdini_hidewhen

def _generate_ds_setting(setting, spare_category, global_hidewhen, settings):
    if not 'ui_name' in setting:
        return ''

    houdini_settings = setting.get('houdini', {})
    houdini_hidewhen = _get_houdini_hidewhen_string((houdini_settings.get('hidewhen'), global_hidewhen), settings)

    def CreateHoudiniParam(name, label, htype, default, values=[], tags=[], disablewhen_conditions=[], size=None, valid_range=None, help_msg=None):
        param = 'parm {\n'
        param += '    name "{}"\n'.format(_get_valid_houdini_param_name(name))
        param += '    label "{}"\n'.format(label)
        param += '    type {}\n'.format(htype)
        if size: param += '    size {}\n'.format(size)
        param += '    default {{ {} }}\n'.format(default)
        for tag in tags:
            param += '    parmtag {{ {} }}\n'.format(tag)
        if values:
            param += '    menu {\n'
            param += '        ' + values + '\n'
            param += '    }\n'
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

    control_param_name = _get_valid_houdini_param_name(name + '_control')

    render_param_values = None
    default_value = setting['defaultValue']
    c_type_str = type(default_value).__name__
    controlled_type = c_type_str
    if c_type_str == 'str':
        c_type_str = 'string'
        controlled_type = 'string'
    render_param_type = c_type_str
    render_param_default = default_value
    if isinstance(default_value, bool):
        render_param_type = 'toggle'
        render_param_default = 1 if default_value else 0
    elif 'values' in setting:
        default_value = next(value for value in setting['values'] if value == default_value)
        render_param_default = '"{}"'.format(default_value.get_key())
        render_param_type = 'string'
        c_type_str = 'token'

        is_values_constant = True
        for value in setting['values']:
            if value.enable_py_condition:
                is_values_constant = False
                break

        render_param_values = ''
        if is_values_constant:
            for value in setting['values']:
                render_param_values += '"{}" "{}"\n'.format(value.get_key(), value.get_ui_name())
        else:
            render_param_values += '[ "import platform" ]\n'
            render_param_values += '[ "menu_values = []" ]\n'
            for value in setting['values']:
                expression = 'menu_values.extend([\\"{}\\", \\"{}\\"])'.format(value.get_key(), value.get_ui_name())

                if value.enable_py_condition:
                    enable_condition = value.enable_py_condition.replace('"', '\\"')
                    expression = 'if {}: {}'.format(enable_condition, expression)

                render_param_values += '[ "{}" ]\n'.format(expression)
            render_param_values += '[ "return menu_values" ]\n'.format(expression)
            render_param_values += 'language python\n'

    if 'type' in houdini_settings:
        render_param_type = houdini_settings['type']

    render_param_range = None
    if 'minValue' in setting and 'maxValue' in setting and not 'values' in setting:
        render_param_range = (setting['minValue'], setting['maxValue'])

    houdini_param_label = setting['ui_name']

    houdini_params = control_param_template.format(
        name=control_param_name,
        label=houdini_param_label,
        controlled_type=controlled_type,
        hidewhen=houdini_hidewhen)
    houdini_params += CreateHoudiniParam(name, houdini_param_label, render_param_type, render_param_default,
        values=render_param_values,
        tags=[
            '"spare_category" "{}"'.format(spare_category),
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

    return houdini_params

def generate_houdini_ds(install_path, ds_name, settings):
    houdini_params = ''

    for category in settings:
        category_name = category['name']
        category_hidewhen = None
        if 'houdini' in category:
            category_hidewhen = category['houdini'].get('hidewhen')

        for setting in category['settings']:
            if 'folder' in setting:
                houdini_params += 'groupcollapsible {\n'
                houdini_params += '  name "{}"\n'.format(setting['folder'].replace(' ', ''))
                houdini_params += '  label "{}"\n'.format(setting['folder'])

                houdini_settings = setting.get('houdini', {})
                houdini_hidewhen = _get_houdini_hidewhen_string((houdini_settings.get('hidewhen'), category_hidewhen), settings)
                if houdini_hidewhen:
                    houdini_params += '  {}\n'.format(houdini_hidewhen)


                for sub_setting in setting['settings']:
                    houdini_params += _generate_ds_setting(sub_setting, category_name, category_hidewhen, settings)
                houdini_params += '}\n'
            else:
                houdini_params += _generate_ds_setting(setting, category_name, category_hidewhen, settings)

    if houdini_params:
        houdini_ds_dst_path = os.path.join(install_path, 'HdRprPlugin_{}.ds'.format(ds_name))
        houdini_ds_file = open(houdini_ds_dst_path, 'w')
        houdini_ds_file.write(houdini_ds_template.format(houdini_params=houdini_params))
