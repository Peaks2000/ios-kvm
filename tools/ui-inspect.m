/* Inspect UIKit's screen configuration and status-bar provider contracts in the guest. */
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    @autoreleasepool {
        UIScreen *screen = UIScreen.mainScreen;
        printf("screen %s\n", screen.description.UTF8String);
        printf("bounds %.2f %.2f scale %.2f nativeScale %.2f\n",
               screen.bounds.size.width, screen.bounds.size.height, screen.scale, screen.nativeScale);
        for (NSString *name in @[@"_UIStatusBarVisualProvider_iOS", @"_UIStatusBarVisualProvider_Phone", @"_UIStatusBarVisualProvider_Split"]) {
            @try {
                Class cls = NSClassFromString(name);
                Class result = ((Class (*)(id, SEL, id, id))objc_msgSend)(cls,
                    sel_registerName("visualProviderSubclassForScreen:visualProviderInfo:"), screen, nil);
                printf("SELECT %s -> %s\n", name.UTF8String, result ? class_getName(result) : "nil");
            } @catch (NSException *e) { printf("SELECT %s exception %s\n", name.UTF8String, e.description.UTF8String); }
        }
        int count = objc_getClassList(NULL, 0);
        Class *classes = calloc(count, sizeof(Class));
        count = objc_getClassList(classes, count);
        for (int i = 0; i < count; ++i) {
            const char *name = class_getName(classes[i]);
            if (!strstr(name, "StatusBarVisualProvider")) continue;
            printf("CLASS %s\n", name);
            for (int meta = 0; meta < 2; ++meta) {
                unsigned n;
                Method *methods = class_copyMethodList(meta ? object_getClass(classes[i]) : classes[i], &n);
                for (unsigned j = 0; j < n; ++j)
                    printf("%c %s %s\n", meta ? '+' : '-', sel_getName(method_getName(methods[j])),
                           method_getTypeEncoding(methods[j]));
                free(methods);
            }
        }
        free(classes);
    }
}
