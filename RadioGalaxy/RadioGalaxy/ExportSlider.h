#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#import "BaseSlider.h"
#import "MFANAqStream.h"
#import "MFANStreamPlayer.h"
#import "SignStation.h"
#import "ViewController.h"
#import "SignView.h"

typedef void (^ExportSliderBlock)(float value);

@interface ExportSlider : BaseSlider<BaseSliderInt>

- (ExportSlider *) initWithFrame: (CGRect) frame
			  buffer: (MFANAqStreamBuffer *) buffer
			   apply: (ExportSliderBlock) block
			viewCont: (ViewController *) vc;

- (void) monitor: (MFANStreamPlayer *) player;

@end
