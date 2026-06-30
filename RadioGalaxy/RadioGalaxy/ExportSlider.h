#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#import "MFANAqStream.h"
#import "MFANStreamPlayer.h"
#import "SignStation.h"
#import "ViewController.h"
#import "SignView.h"

@interface ExportSlider : UIView

typedef void (^ExportSliderBlock)(float value);

@property uint64_t lastMusicSampleTime;

- (ExportSlider *) initWithFrame: (CGRect) frame
			  buffer: (MFANAqStreamBuffer *) buffer
			   apply: (ExportSliderBlock) block
			viewCont: (ViewController *) vc;
- (void) shutdown;
@end
