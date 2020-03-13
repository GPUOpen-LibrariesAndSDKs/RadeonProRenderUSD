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
#include <Cocoa/Cocoa.h>

PXR_NAMESPACE_OPEN_SCOPE

bool HdRprShowMessage(std::string const& title, std::string const& message) {
    NSString* titleText = [NSString stringWithCString: title.c_str() encoding: [NSString defaultCStringEncoding]];
    NSString* msgText = [NSString stringWithCString: message.c_str() encoding: [NSString defaultCStringEncoding]];

    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText: titleText];
    [alert setInformativeText: msgText];
    [alert setAlertStyle:NSCriticalAlertStyle];
    [alert addButtonWithTitle:@"Yes"];
    [alert addButtonWithTitle:@"No"];

    NSModalResponse response = [alert runModal];

    [alert release];
    [msgText release];
    [titleText release];

    return response == NSAlertFirstButtonReturn;
}

PXR_NAMESPACE_CLOSE_SCOPE
