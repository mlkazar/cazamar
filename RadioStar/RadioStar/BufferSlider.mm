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
    float _lastReportedRatio;
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
	sliderFrame.origin.x = frame.size.width * 0.05;
	sliderFrame.size.width = frame.size.width * 0.90;

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

	labelFrame.origin.x = frame.size.width * 0.7;
	labelFrame.origin.y = frame.size.height / 2;
	labelFrame.size.width = frame.size.width * 0.25;
	labelFrame.size.height = frame.size.height / 2;

	currentEndPosition = [_signView getStationBufferDuration: _station];

	_sliderLabel = [[UILabel alloc] initWithFrame:labelFrame];
	_sliderLabel.text = [NSString stringWithFormat:@"%.f secs",
				      currentEndPosition];
	[_sliderLabel setTextColor: [UIColor blackColor]];
	[self addSubview: _sliderLabel];

	labelFrame.origin.x = frame.size.width * 0.45;
	_middleLabel = [[UILabel alloc] initWithFrame:labelFrame];
	_middleLabel.text = [NSString stringWithFormat:@"%.f secs",
				      currentPosition];
	[_middleLabel setTextColor: [UIColor blackColor]];
	[self addSubview: _middleLabel];

	_slider.maximumValue = currentEndPosition;
	_slider.value = currentPosition;

	_lastReportedRatio = 0.0;

	_updateTimer = [NSTimer scheduledTimerWithTimeInterval: 0.20
							target: self
						      selector: @selector(updateStats:)
						      userInfo: nil
						       repeats: YES];
    }

    return self;
}

- (void) updateStats: (id) junk {
    float currentPosition;
    float currentEndPosition;
    float currentRatio;

    _station = [_signView getCurrentStation];

    if (_station == nil)
	return;

    currentPosition = [_signView getCurrentBufferTimestamp];
    currentEndPosition = [_signView getStationBufferDuration: _station];

    _sliderLabel.text = [NSString stringWithFormat: @"%.f secs", currentEndPosition];
    _middleLabel.text = [NSString stringWithFormat: @"%.f secs", currentPosition];

    // we try to keep the cursor moving to the right in between manual
    // setting of of the slider.  Otherwise, it is very distracting,
    // since after a large buffer full of data arrives,
    // currentEndPosition goes up a lot and the ratio goes down (so
    // the cursor moves left).  Then it starts moving right again as
    // the music plays and currentPosition advances.  Then another
    // large buffer arrives and it all repeats.
    //
    // When a user actually moves the slider, we always update the
    // cursor, of course.
    currentRatio = currentPosition / currentEndPosition;
    if (currentRatio >= _lastReportedRatio) {
	_slider.maximumValue = currentEndPosition;
	_slider.value = currentPosition;
	_lastReportedRatio = currentRatio;

	NSLog(@"===timer last reported %f/%f=%f",
	      currentPosition, currentEndPosition, currentRatio);
    }
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
    float val = slider.value;
    uint64_t now = osp_time_ms();

    NSLog(@"slider value %f at %lld ms", val, now);

    // if we ever set it to NaN, comparison in updateStats fails
    if (_slider.maximumValue > 2.0) {
	_lastReportedRatio = val / _slider.maximumValue;
	NSLog(@"===movement last reported %f/%f=%f",
	      val, slider.maximumValue, val/slider.maximumValue);
    }

    if (now - _lastMusicSampleTime > 200) {
	[_signView seek: val relative: false];
    }
}
@end
