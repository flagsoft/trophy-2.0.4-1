/*
 * cl1_cocoa.mm - macOS event pump for the ClanLib 1.0 compatibility layer
 *
 * Trophy runs its own loops and only calls CL_System::keep_alive(). This
 * delivers all pending window/keyboard/mouse events to the application, which
 * is what a normal Cocoa run loop would do.
 */
#import <Cocoa/Cocoa.h>

void cl1_cocoa_pump_events()
{
    static bool activated = false;
    @autoreleasepool
    {
        if (!activated)
        {
            activated = true;
            // A program started from a terminal is not a regular app by
            // default: make it one, so it gets focus and keyboard input.
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            [NSApp activateIgnoringOtherApps:YES];
        }

        for (;;)
        {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate distantPast]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event == nil) break;
            [NSApp sendEvent:event];
        }
        [NSApp updateWindows];
    }
}
