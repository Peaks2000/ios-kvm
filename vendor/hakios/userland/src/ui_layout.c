/* Keep the iPhone X status-bar layout when its framebuffer uses two pixels per point. */
#include <CoreGraphics/CoreGraphics.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <string.h>

static Class (*original_provider)(id, SEL, id, id);

static Class provider_for_screen(id self, SEL selector, id screen, id info)
{
    Class selected = original_provider(self, selector, screen, info);
    if (selected != Nil || screen == nil) return selected;
    CGRect bounds = ((CGRect (*)(id, SEL))objc_msgSend)(screen, sel_registerName("bounds"));
    double scale = ((double (*)(id, SEL))objc_msgSend)(screen, sel_registerName("scale"));
    /* Limit this to our reduced-density iPhone X geometry; other displays use UIKit's choice. */
    if (bounds.size.width == 375 && bounds.size.height == 812 && scale == 2 &&
        (self == (id)objc_getClass("_UIStatusBarVisualProvider_Split") ||
         self == (id)objc_getClass("_UIStatusBarVisualProvider_Split1125")))
        return objc_getClass("_UIStatusBarVisualProvider_Split1125");
    return selected;
}

__attribute__((constructor)) static void configure_status_bar(void)
{
    Class cls = objc_getClass("_UIStatusBarVisualProvider_Split");
    Method method = class_getClassMethod(cls,
        sel_registerName("visualProviderSubclassForScreen:visualProviderInfo:"));
    if (!method || strcmp(method_getTypeEncoding(method), "#32@0:8@16@24")) return;
    original_provider = (void *)method_getImplementation(method);
    method_setImplementation(method, (IMP)provider_for_screen);
}
