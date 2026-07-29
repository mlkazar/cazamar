#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

@interface Silence : NSObject<AVAudioPlayerDelegate>
- (Silence *) init;

- (void) start;

- (void) stop;
@end
