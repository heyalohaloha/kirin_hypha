#import <Cocoa/Cocoa.h>
#include <functional>
#include <stdexcept>

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

namespace hypha::tests
{
void withNativeWindowHost (const std::function<void()>& verify);
bool setNativeWindowMinimised (void* nativeView, bool minimised);
bool setNativeViewHidden (void* nativeView, bool hidden);
bool setNativeWindowHidden (void* nativeView, bool hidden);

void withNativeWindowHost (const std::function<void()>& verify)
{
    // A prohibited host has no Dock window lifecycle. Allow only this fixture's windows;
    // restore the original policy after they have closed, without activating another app.
    const auto previous = [NSApp activationPolicy];
    if (! [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular])
        throw std::runtime_error ("native window fixture policy unavailable");
    static bool launched = false;
    if (! launched) { [NSApp finishLaunching]; launched = true; }
    verify();
    [NSApp setActivationPolicy:previous];
}

bool setNativeWindowMinimised (void* nativeView, bool minimised)
{
    auto* window = [(NSView*) nativeView window];
    if (window == nil) return false;
    __block bool notified = false, waiting = false;
    const auto loop = CFRunLoopGetCurrent();
    const auto name = minimised ? NSWindowDidMiniaturizeNotification : NSWindowDidDeminiaturizeNotification;
    auto observer = [[NSNotificationCenter defaultCenter] addObserverForName:name object:window queue:nil
        usingBlock:^(NSNotification*) {
            notified = true;
            if (waiting) CFRunLoopStop (loop);
        }];
    if (minimised) [window miniaturize:nil]; else [window deminiaturize:nil];
    // Wait for the native completion event, not a guessed delay or a polling sleep.
    if (! notified) { waiting = true; CFRunLoopRunInMode (kCFRunLoopDefaultMode, 5.0, false); waiting = false; }
    [[NSNotificationCenter defaultCenter] removeObserver:observer];
    return notified && bool ([window isMiniaturized]) == minimised;
}

bool setNativeViewHidden (void* nativeView, bool hidden)
{
    auto* view = (NSView*) nativeView;
    if (view == nil) return false;
    [view setHidden:hidden];
    return bool ([view isHidden]) == hidden;
}

bool setNativeWindowHidden (void* nativeView, bool hidden)
{
    auto* window = [(NSView*) nativeView window];
    if (window == nil) return false;
    if (hidden) [window orderOut:nil]; else [window orderFront:nil];
    return bool ([window isVisible]) != hidden;
}
}
