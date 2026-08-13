#import "ExportSlider.h"

#import "MFANCGUtil.h"
#import "MFANWarn.h"
#import "ViewController.h"
#import "MarqueeLabel.h"
#import "MFANStreamPlayer.h"

#include "osp.h"

@implementation ExportSlider {
    ViewController *_vc;
    MFANAqStreamBuffer *_buffer;
    ExportSliderBlock _callbackBlock;
    MFANStreamPlayer *_monitorPlayer;
}

// Says that these properties are implemented in the base class.
@dynamic lastMusicSampleTime;
@dynamic lastTouchMs;

- (ExportSlider *) initWithFrame: (CGRect) frame
			  buffer: (MFANAqStreamBuffer *) buffer
			   apply: (ExportSliderBlock) block
			viewCont: (ViewController *) vc {

    self = [super initWithFrame: frame
		       callback: self
		       viewCont: vc];
    if (self != nil) {
	_vc = vc;
	_buffer = buffer;
	_callbackBlock = block;
	_monitorPlayer = nil;

	// some parts of the superclass can't be initialized until we
	// finish our init, since the superclass calls back to us to
	// get some information from the callback object.
	[super finishInit];
    }

    return self;
}

- (void) monitor: (MFANStreamPlayer *) player {
    _monitorPlayer = player;
}

- (void) shutdown {
    [super shutdown];
    _vc = nil;
}

// Interfaces for BaseSlider to get values
- (float) getMinForSlider: (BaseSlider *) slider {
    return _buffer.firstPacketStartMs / 1000.0;
}

- (float) getMaxForSlider: (BaseSlider *) slider {
    return _buffer.lastPacketEndMs / 1000.0;
}

- (float) getCurrentForSlider: (BaseSlider *) slider {
    uint64_t currentMs;
    if (_monitorPlayer != nil) {
	currentMs = [_monitorPlayer getSeekTarget: 0.0];
	return currentMs / 1000.0;
    } else {
	return [super getValue];
    }
}

// called from base class when value changes
- (void) updatedSlider: (BaseSlider *) slider {
    _callbackBlock([slider getValue]);
}

@end
