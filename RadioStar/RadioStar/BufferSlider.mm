#import "PopStatus.h"
#import "MFANCGUtil.h"
#import "MFANCoreButton.h"
#import "MFANIconButton.h"
#import "MFANWarn.h"
#import "SignView.h"
#import "ViewController.h"
#import "MarqueeLabel.h"

#include "osp.h"

@implementation BufferSlider {
    UISlider *_slider;
    UILabel *_sliderLabel;
    UILabel *_middleLabel;
    uint64_t _lastMusicSampleTime;
    ViewController *_vc;
    SignView *_signView;
    SignStation *_station;
    NSTimer *_updateTimer;
}
- (BufferSlider *) initWithFrame: (CGRect) frame
			viewCont: (ViewController *) vc
			signView: (SignView *) signView {
    float currentPosition;
    float currentEndPosition;
    CGRect sliderFrame;
    CGRect labelFrame;

    self = [super initWithFrame: frame];
    if (self != nil) {
	_signView = signView;
	_vc = vc;
	_station = [_signView getCurrentStation];

	currentPosition = [_signView getCurrentBufferTimestamp];

	sliderFrame = frame;
	sliderFrame.origin.y = 0;
	sliderFrame.size.height = frame.size.height / 2;

	// 10 - 80 - 10 split
	sliderFrame.origin.x = frame.size.width * 0.075;
	sliderFrame.size.width = frame.size.width * 0.85;

	_slider = [[UISlider alloc] initWithFrame: sliderFrame];
	_slider.minimumValue = 0;
	_slider.value = 0;
	_slider.maximumValue = 2.0;
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
	[self addSubview: _slider];

	// put in right half with a little extra space on right
	labelFrame.origin.x = frame.size.width * 0.50;
	labelFrame.origin.y = frame.size.height / 2;
	labelFrame.size.width = frame.size.width * 0.48;
	labelFrame.size.height = frame.size.height / 2;

	currentEndPosition = [_signView getStationBufferEnd: _station];

	_sliderLabel = [[UILabel alloc] initWithFrame:labelFrame];
	_sliderLabel.text = [NSString stringWithFormat:@"%.f secs streamed",
				      currentEndPosition];
	[_sliderLabel setTextColor: [UIColor blackColor]];
	_sliderLabel.textAlignment = NSTextAlignmentRight;
	[self addSubview: _sliderLabel];

	// first half, with a little extra space on left
	labelFrame.origin.x = frame.size.width * 0.02;
	_middleLabel = [[UILabel alloc] initWithFrame:labelFrame];
	_middleLabel.text = [NSString stringWithFormat:@"%.f secs played",
				      currentPosition];
	[_middleLabel setTextColor: [UIColor blackColor]];
	[self addSubview: _middleLabel];

	_slider.maximumValue = currentEndPosition;
	_slider.value = currentPosition;

	_updateTimer = [NSTimer scheduledTimerWithTimeInterval: 0.20
							target: self
						      selector: @selector(updateStats:)
						      userInfo: nil
						       repeats: YES];
    }

    return self;
}

- (NSString *) stringFromTime: (float) time text: (NSString *) text{
    NSString *rval;
    if (time >= 180) {
	rval = [NSString stringWithFormat: @"%.f mins %@", time / 60.0, text];
    } else {
	rval = [NSString stringWithFormat: @"%.f secs %@", time, text];
    }
    return rval;
}

- (void) updateStats: (id) junk {
    float currentPosition;
    float currentEndPosition;
    float currentStartPosition;

    _station = [_signView getCurrentStation];

    if (_station == nil)
	return;

    currentPosition = [_signView getCurrentBufferTimestamp];
    currentEndPosition = [_signView getStationBufferEnd: _station];
    currentStartPosition = [_signView getStationBufferStart: _station];

    _sliderLabel.text = [self stringFromTime: currentEndPosition text:@"streamed"];
    _middleLabel.text = [self stringFromTime: currentPosition text:@"played"];

    _slider.minimumValue = currentStartPosition;
    _slider.maximumValue = currentEndPosition;
    _slider.value = currentPosition;
}

- (void) shutdown {
    // clear out back references
    [_updateTimer invalidate];
    _updateTimer = nil;

    _vc = nil;
    _signView = nil;
    _station = nil;
}

- (void) sliderValue:(UISlider *) slider {
    float currentEndPosition;
    float currentStartPosition;

    uint64_t now = osp_time_ms();

    currentEndPosition = [_signView getStationBufferEnd: _station];
    currentStartPosition = [_signView getStationBufferStart: _station];
    _slider.minimumValue = currentStartPosition;
    _slider.maximumValue = currentEndPosition;

    float val = slider.value;
    NSLog(@"slider value %f at %lld ms", val, now);

    // if we ever set it to NaN, comparison in updateStats fails
    if (now - _lastMusicSampleTime > 200) {
	[_signView seek: val relative: false];
	_lastMusicSampleTime = now;
    }
}
@end
