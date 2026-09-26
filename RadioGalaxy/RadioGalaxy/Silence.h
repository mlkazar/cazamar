#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#import "ViewController.h"

@interface Silence : NSObject<AVAudioPlayerDelegate>
- (Silence *) initWithViewController: (ViewController *) vc;

- (void) start;

- (void) stop;
@end
