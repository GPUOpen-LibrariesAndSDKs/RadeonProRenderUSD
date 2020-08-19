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

#include "util.h"

#include "pxr/base/tf/staticTokens.h"

#include <cstring>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    RprUsdUDIMTags,
    ((arnoldTag, "<UDIM>"))
    ((sidefxTag, "%(UDIM)d"))
);

bool RprUsdGetUDIMFormatString(std::string const& filepath, std::string* out_formatString) {
    for (auto& udimTag : RprUsdUDIMTags->allTokens) {
        auto idx = filepath.rfind(udimTag.GetString());
        if (idx != std::string::npos) {
            *out_formatString = filepath;
            out_formatString->replace(idx, udimTag.size(), "%i");
            return true;
        }
    }

    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
