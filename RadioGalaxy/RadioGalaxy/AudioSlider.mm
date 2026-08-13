#import "AudioSlider.h"

#import "MFANCGUtil.h"
#import "MFANWarn.h"
#import "ViewController.h"
#import "MarqueeLabel.h"
#import "MFANStreamPlayer.h"

#include "osp.h"

@implementation AudioSlider {
    ViewController *_vc;
    AudioSliderBlock _callbackBlock;
    AVAudioPlayer *_monitorPlayer;
}

// Says that these properties are implemented in the base class.
@dynamic lastMusicSampleTime;
@dynamic lastTouchMs;

- (AudioSlider *) initWithFrame: (CGRect) frame
			  apply: (AudioSliderBlock) block
		       viewCont: (ViewController *) vc {

    self = [super initWithFrame: frame
		       callback: self
		       viewCont: vc];
    if (self != nil) {
	_vc = vc;
	_callbackBlock = block;
	_monitorPlayer = nil;

	// some parts of the superclass can't be initialized until we
	// finish our init, since the superclass calls back to us to
	// get some information from the callback object.
	[super finishInit];
    }

    return self;
}

- (void) monitor: (AVAudioPlayer *) player {
    _monitorPlayer = player;
}

- (void) shutdown {
    [super shutdown];
    _vc = nil;
}

// Interfaces for BaseSlider to get values
- (float) getMinForSlider: (BaseSlider *) slider {
    return 0.0;
}

- (float) getMaxForSlider: (BaseSlider *) slider {
    if (_monitorPlayer != nil)
	return _monitorPlayer.duration;
    else
	return 0;
}

- (float) getCurrentForSlider: (BaseSlider *) slider {
    if (_monitorPlayer != nil) {
	return _monitorPlayer.currentTime;
    } else
	return 0;
}

- (void) updatedSlider: (BaseSlider *) slider {
    float updatedPosition = [slider getValue];

    if (_monitorPlayer != nil)
	_monitorPlayer.currentTime = updatedPosition;

    _callbackBlock(updatedPosition);
}

@end
