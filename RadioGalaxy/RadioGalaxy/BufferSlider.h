#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#import "MFANAqStream.h"
#import "MFANStreamPlayer.h"
#import "SignStation.h"
#import "ViewController.h"
#import "SignView.h"

@interface BufferSlider : UIView

@property uint64_t lastMusicSampleTime;

- (BufferSlider *) initWithFrame: (CGRect) frame
			viewCont: (ViewController *) vc
			signView: (SignView *) signView;
- (void) shutdown;
@end
