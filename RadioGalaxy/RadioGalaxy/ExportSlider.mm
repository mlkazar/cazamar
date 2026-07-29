#import "ExportSlider.h"

#import "MFANCGUtil.h"
#import "MFANWarn.h"
#import "ViewController.h"
#import "MarqueeLabel.h"

#include "osp.h"

@implementation ExportSlider {
    UISlider *_slider;
    UILabel *_midLabel;
    uint64_t _lastMusicSampleTime;
    uint64_t _lastTouchMs;
    ViewController *_vc;
    MFANAqStreamBuffer *_buffer;
    NSTimer *_updateTimer;
    ExportSliderBlock _callbackBlock;
}

- (ExportSlider *) initWithFrame: (CGRect) frame
			  buffer: (MFANAqStreamBuffer *) buffer
			   apply: (ExportSliderBlock) block
			viewCont: (ViewController *) vc {
    float currentPosition;
    float currentEndPosition;
    CGRect sliderFrame;
    CGRect labelFrame;

    self = [super initWithFrame: frame];
    if (self != nil) {
	_vc = vc;
	_buffer = buffer;
	_callbackBlock = block;

	currentPosition = _buffer.firstPacketStartMs/1000.0;

	sliderFrame = frame;
	sliderFrame.origin.y = 0;
	sliderFrame.size.height = frame.size.height / 2;

	// 10 - 80 - 10 split
	sliderFrame.origin.x = frame.size.width * 0.075;
	sliderFrame.size.width = frame.size.width * 0.85;

	_slider = [[UISlider alloc] initWithFrame: sliderFrame];
	_slider.minimumValue = _buffer.firstPacketStartMs/1000.0;
	_slider.value = _buffer.firstPacketStartMs/1000.0;
	_slider.maximumValue = _buffer.lastPacketEndMs/1000.0;
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

	currentEndPosition = _buffer.lastPacketEndMs / 1000.0;

	_slider.maximumValue = currentEndPosition;
	_slider.value = currentPosition;

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

- (void) setValue: (float) newPosition  {
    _slider.value = newPosition;

    [self updateCallback];
}

- (float) getValue {
    return _slider.value;
}

- (NSString *) stringFromBufferTime {
    float currentEndPosition = _buffer.lastPacketEndMs / 1000.0;
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

    currentEndPosition = _buffer.lastPacketEndMs / 1000.0;
    currentStartPosition = _buffer.firstPacketStartMs / 1000.0;

    _midLabel.text = [self stringFromBufferTime];

    _slider.minimumValue = currentStartPosition;
    _slider.maximumValue = currentEndPosition;
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
	_callbackBlock(_slider.value);
	_lastMusicSampleTime = now;
    }
}

- (void) sliderValue:(UISlider *) slider {
    float currentEndPosition;
    float currentStartPosition;

    uint64_t now = osp_time_ms();
    _lastTouchMs = now;

    currentEndPosition = _buffer.lastPacketEndMs / 1000.0;
    currentStartPosition = _buffer.firstPacketStartMs / 1000.0;
    _slider.minimumValue = currentStartPosition;
    _slider.maximumValue = currentEndPosition;

    [self updateCallback];
}
@end
