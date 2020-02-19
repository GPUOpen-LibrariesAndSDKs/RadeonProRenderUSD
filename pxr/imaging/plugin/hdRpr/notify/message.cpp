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
