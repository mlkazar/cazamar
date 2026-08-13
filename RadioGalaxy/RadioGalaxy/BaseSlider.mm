#import "BaseSlider.h"

#import "MFANCGUtil.h"
#import "MFANWarn.h"
#import "ViewController.h"
#import "MarqueeLabel.h"

#include "osp.h"

@implementation BaseSlider {
    UISlider *_slider;
    UILabel *_midLabel;
    uint64_t _lastMusicSampleTime;
    uint64_t _lastTouchMs;
    ViewController *_vc;
    NSTimer *_updateTimer;
    id<BaseSliderInt> _callback;
}

- (BaseSlider *) initWithFrame: (CGRect) frame
		      callback: (id<BaseSliderInt>) callback
		      viewCont: (ViewController *) vc {
    float currentPosition;
    CGRect sliderFrame;
    CGRect labelFrame;

    self = [super initWithFrame: frame];
    if (self != nil) {
	_vc = vc;
	_callback = callback;

	currentPosition = [callback getCurrentForSlider: self];

	sliderFrame = frame;
	sliderFrame.origin.y = 0;
	sliderFrame.size.height = frame.size.height / 2;

	// 10 - 80 - 10 horizontal split
	sliderFrame.origin.x = frame.size.width * 0.075;
	sliderFrame.size.width = frame.size.width * 0.85;

	_slider = [[UISlider alloc] initWithFrame: sliderFrame];
	_slider.minimumValue = [callback getMinForSlider: self];
	_slider.value = currentPosition;
	_slider.maximumValue = [callback getMaxForSlider: self];
	_slider.continuous = true;
	_slider.minimumTrackTintColor = [UIColor greenColor];
	_slider.maximumTrackTintColor = [UIColor blueColor];

	_slider.thumbTintColor = [UIColor colorWithRed: 0.0
						 green: 1.0
						  blue: 1.0
						 alpha: 0.8];
	_lastMusicSampleTime = 0;
	[_slider addTarget:self
		    action:@selector(sliderValue:)
	  forControlEvents:UIControlEventValueChanged];
	[_slider addTarget:self
		    action:@selector(sliderTouch:)
	  forControlEvents:UIControlEventTouchDown];
	[self addSubview: _slider];

	// put in right half with a little extra space on right
	labelFrame.origin.x = frame.size.width * 0.67;
	labelFrame.origin.y = frame.size.height / 2;
	labelFrame.size.width = frame.size.width * 0.33;
	labelFrame.size.height = frame.size.height / 2;

	labelFrame.origin.x = frame.size.width * 0.10;
	labelFrame.size.width = frame.size.width * 0.80;

	_midLabel = [[UILabel alloc] initWithFrame:labelFrame];
	_midLabel.text = [self stringFromBufferTime];
	[_midLabel setTextColor: [UIColor blackColor]];
	_midLabel.textAlignment = NSTextAlignmentCenter;
	[self addSubview: _midLabel];

	_updateTimer = [NSTimer scheduledTimerWithTimeInterval: 0.20
							target: self
						      selector: @selector(updateStats:)
						      userInfo: nil
						       repeats: YES];
    }

    return self;
}

- (void) finishInit {
    _slider.minimumValue = [_callback getMinForSlider: self];
    _slider.maximumValue = [_callback getMaxForSlider: self];
    _slider.value = _slider.minimumValue;

    _midLabel.text = [self stringFromBufferTime];
}

- (void) setValue: (float) newPosition  {
    _slider.value = newPosition;

    [self updateCallback];
}

- (float) getValue {
    return _slider.value;
}

- (NSString *) stringFromBufferTime {
    float currentEndPosition = [_callback getMaxForSlider: self];
    uint64_t sliderMins;
    uint64_t sliderSecs;
    uint64_t endMins;
    uint64_t endSecs;

    sliderMins = ((uint64_t) _slider.value) / 60;
    sliderSecs = ((uint64_t) _slider.value) % 60;
    endMins = ((uint64_t) currentEndPosition) / 60;
    endSecs = ((uint64_t) currentEndPosition) % 60;

    NSString *rval = [NSString stringWithFormat: @"%lld:%02lld / %lld:%02lld",
			       sliderMins, sliderSecs,
			       endMins, endSecs];
    return rval;
}

+ (NSString *) stringFromTime: (float) arg {
    uint64_t sliderMins;
    uint64_t sliderSecs;

    sliderMins = ((uint64_t) arg) / 60;
    sliderSecs = ((uint64_t) arg) % 60;

    NSString *rval = [NSString stringWithFormat: @"%lld:%02lld",
			       sliderMins, sliderSecs];
    return rval;
}

- (void) updateStats: (id) junk {
    float currentEndPosition;
    float currentStartPosition;
    float currentPosition;

    currentEndPosition = [_callback getMaxForSlider: self];
    currentStartPosition = [_callback getMinForSlider: self];
    currentPosition = [_callback getCurrentForSlider: self];

    _slider.minimumValue = currentStartPosition;
    _slider.maximumValue = currentEndPosition;
    _slider.value = currentPosition;

    _midLabel.text = [self stringFromBufferTime];
}

- (void) shutdown {
    // clear out back references
    [_updateTimer invalidate];
    _updateTimer = nil;

    _vc = nil;
}

- (void) sliderTouch: (UISlider *) slider {
    uint64_t now = osp_time_ms();
    _lastTouchMs = now;
}

- (void) updateCallback {
    uint64_t now = osp_time_ms();

    // if we ever set it to NaN, comparison in updateStats fails
    if (now - _lastMusicSampleTime > 200) {
	_lastMusicSampleTime = now;
	[_callback updatedSlider: self];
    }
}

// called when a slider value changes.
- (void) sliderValue:(UISlider *) slider {
    uint64_t now = osp_time_ms();
    _lastTouchMs = now;

    _slider.minimumValue = [_callback getMinForSlider: self];
    _slider.maximumValue = [_callback getMaxForSlider: self];

    [self updateCallback];
}
@end
