#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#import "BaseSlider.h"
#import "MFANAqStream.h"
#import "MFANStreamPlayer.h"
#import "SignStation.h"
#import "ViewController.h"
#import "SignView.h"

typedef void (^AudioSliderBlock)(float value);

@interface AudioSlider : BaseSlider<BaseSliderInt>

- (AudioSlider *) initWithFrame: (CGRect) frame
			  apply: (AudioSliderBlock) block
		       viewCont: (ViewController *) vc;

- (void) monitor: (AVAudioPlayer *) player;

@end
