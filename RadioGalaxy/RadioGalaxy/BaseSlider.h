#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#import "MFANAqStream.h"
#import "MFANStreamPlayer.h"
#import "SignStation.h"
#import "ViewController.h"
#import "SignView.h"

@class BaseSlider;

@protocol BaseSliderInt
- (float) getMinForSlider: (BaseSlider *) slider;

- (float) getCurrentForSlider: (BaseSlider *) slider;

- (float) getMaxForSlider: (BaseSlider *) slider;

- (void) updatedSlider: (BaseSlider *) slider;
@end

@interface BaseSlider : UIView

@property uint64_t lastMusicSampleTime;

@property uint64_t lastTouchMs;

+ (NSString *) stringFromTime: (float) arg;

- (BaseSlider *) initWithFrame: (CGRect) frame
		      callback: (id<BaseSliderInt>) callback
		      viewCont: (ViewController *) vc;

- (void) finishInit;

- (void) setValue: (float) newPosition;

- (float) getValue;

- (void) shutdown;

@end
