/************************************************************************
Copyright 2020 Advanced Micro Devices, Inc
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
************************************************************************/

#include "message.h"

#include "pxr/base/tf/stringUtils.h"

#if defined WIN32
#include <windows.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

bool HdRprShowMessage(std::string const& title, std::string const& message) {
#if defined WIN32
    return MessageBox(nullptr, message.c_str(), title.c_str(), MB_YESNO | MB_ICONEXCLAMATION) == IDYES;
#else
    auto command = TfStringPrintf("xmessage -nearmouse -buttons Yes:0,No:1 -title \"%s\" \"%s\"", title.c_str(), message.c_str());
    return system(command.c_str()) == 0;
#endif
}

PXR_NAMESPACE_CLOSE_SCOPE
