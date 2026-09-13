#import <Cocoa/Cocoa.h>

void initialiseBlindProductHostApplication();

// The real processor library is compiled as a plug-in and expects its host to own NSApp.
// Supply that host event loop without presenting a window or activating another application.
void initialiseBlindProductHostApplication()
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    }
}
