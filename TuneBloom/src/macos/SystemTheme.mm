#import <Cocoa/Cocoa.h>
#include <theme/SystemTheme.h>

namespace systemtheme {

bool isDark()
{
    @autoreleasepool
    {
        NSString* style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
        return style != nil && [style isEqualToString:@"Dark"];
    }
}

}
