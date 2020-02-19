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
