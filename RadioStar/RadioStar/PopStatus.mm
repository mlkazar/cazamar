#import "PopStatus.h"
#import "MFANCGUtil.h"
#import "MFANIconButton.h"
#import "ViewController.h"
#import "MarqueeLabel.h"

#include "osp.h"

@implementation PopStatus {
    ViewController *_vc;
    MFANIconButton *_doneButton;
    MFANStreamPlayer *_player;
    MFANAqStream *_stream;
    SignStation *_station;

    uint64_t _startTimeMs;

    // tags
    UILabel *_stationNameLabel;
    UILabel *_stationDescrLable;
    UILabel *_searchUrlLabel;
    UILabel *_realUrlLabel;
    UILabel *_speedAndTypeLabel;

    // properties
    MarqueeLabel *_stationNameView;
    MarqueeLabel *_stationDescrView;
    MarqueeLabel *_searchUrlView;
    MarqueeLabel *_realUrlView;
    UILabel *_speedAndTypeView;

    // so we can update the status a few times per second
    NSTimer *_timer;

    id _callbackObj;
    SEL _callbackSel;
    bool _didNotify;
}

- (void) setCallback: (id) obj withSel: (SEL) sel {
    _callbackObj = obj;
    _callbackSel = sel;
}

- (void) doNotify {
    if (_didNotify)
	return;
    _didNotify = true;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    if (_callbackObj != nil) {
	[_callbackObj  performSelector: _callbackSel withObject: nil];
    }
#pragma clang diagnostic pop
}

- (PopStatus *) initWithFrame: (CGRect) frame
		     viewCont: (ViewController *) vc
			  stream: (MFANAqStream *) stream
			  player: (MFANStreamPlayer *) player
			 station: (SignStation *) station
{
    // we get the frame from the view controller
    CGRect boxFrame;
    CGRect buttonFrame;
    CGRect labelFrame;

    UIColor *bkgColor;

    self = [super initWithFrame: frame];
    if (self != nil) {
	_vc = vc;
	_player = player;
	_stream = stream;
	_station = station;
	_vc = vc;
	
	UIColor *screenColor = [UIColor colorWithRed: 0.3
					       green: 0.3
						blue: 0.3
					       alpha: 0.5];

	UIColor *textColor = [UIColor blackColor];
	UIColor *labelColor = [UIColor colorWithRed: 0.9
					      green: 0.9
					       blue: 0.9
					      alpha: 1.0];
	UIColor *valueColor = [UIColor whiteColor];

	self.backgroundColor = screenColor;

	// layout the station name, descr, URL, rate+type
	float boxHeight = frame.size.height * 0.06;
	float boxWidth = frame.size.width * 0.60;
	float labelHeight = boxHeight;
	float labelWidth = frame.size.width * 0.35;
	// indent things so that we center the label and text box in
	//the frame.
	float indent = (frame.size.width - labelWidth - boxWidth) / 2;

	labelFrame = frame;
	labelFrame.origin.x = indent;
	labelFrame.origin.y += vc.topMargin;
	labelFrame.size.width = labelWidth;
	labelFrame.size.height = labelHeight;

	_stationNameLabel = [[UILabel alloc] initWithFrame:labelFrame];
	_stationNameLabel.backgroundColor = labelColor;
	_stationNameLabel.text = @"Station name";
	_stationNameLabel.textColor = [UIColor blackColor];
	_stationNameLabel.textAlignment = NSTextAlignmentLeft;
	[self addSubview: _stationNameLabel];

	boxFrame = frame;
	boxFrame.origin.y += vc.topMargin;
	boxFrame.origin.x += indent + labelWidth;
	boxFrame.size.width = boxWidth;
	boxFrame.size.height = boxHeight;

	/* add first text box */
	_stationNameView = [[MarqueeLabel alloc] initWithFrame: boxFrame];
	[_stationNameView setTextColor: textColor];
	[_stationNameView setBackgroundColor: valueColor];
	[_stationNameView setText: station.stationName];
	[self addSubview: _stationNameView];

	
	labelFrame.origin.y += labelHeight + frame.size.height * 0.05;
	_speedAndTypeLabel = [[UILabel alloc] initWithFrame:labelFrame];
	_speedAndTypeLabel.backgroundColor = labelColor;
	_speedAndTypeLabel.text = @"Stream rate";
	_speedAndTypeLabel.textColor = [UIColor blackColor];
	_speedAndTypeLabel.textAlignment = NSTextAlignmentLeft;
	[self addSubview: _speedAndTypeLabel];

	/* add second text box */
	boxFrame.origin.y += boxHeight + frame.size.height * 0.05;
	_speedAndTypeView = [[UILabel alloc] initWithFrame: boxFrame];
	[_speedAndTypeView setTextColor: textColor];
	[_speedAndTypeView setBackgroundColor: valueColor];
	NSString *satString;
	if (_player == nil)
	    satString = @"No data";
	else
	    satString = [NSString stringWithFormat: @"%8.0f kbps %@",
				  [_player getDataRate] / 1000,
				  [_player getEncodingType]];
	[_speedAndTypeView setText: satString];
	[self addSubview: _speedAndTypeView];

	/* now add Done button */
	float buttonWidth = frame.size.width/8;
	buttonFrame = frame;
	buttonFrame.origin.y = 0.9*frame.size.height;
	buttonFrame.size.height = buttonWidth;	// square buttons
	buttonFrame.origin.x = frame.size.width/2 - buttonWidth/2;
	buttonFrame.size.width = buttonWidth;
	_doneButton = [[MFANIconButton alloc] initWithFrame: buttonFrame
						      title: @"Done"
						      color: [UIColor colorWithHue: 0.4
									saturation: 1.0
									brightness: 1.0
									     alpha: 1.0]
						       file: @"icon-done.png"];
	[_doneButton addCallback: self
		      withAction: @selector(donePressed:withData:)];
	[self addSubview: _doneButton];

	_startTimeMs = osp_time_ms();

	_didNotify = false;

	_timer = [NSTimer scheduledTimerWithTimeInterval: 1.0
						  target: self
						selector: @selector(updateStats:)
						userInfo: nil
						 repeats: YES];
    }

    return self;
}

- (void) updateStats: (id) junk {
    NSString *satString;
    if (_player == nil)
	satString = @"No data";
    else
	satString = [NSString stringWithFormat: @"%8.0f kbps %@",
			      [_player getDataRate] / 1000,
			      [_player getEncodingType]];
    [_speedAndTypeView setText: satString];

    if (osp_time_ms() - _startTimeMs > 40000) {
	[self doNotify];
    }
}

- (void) shutdown {
    [_timer invalidate];
    _timer = nil;
}

- (void) donePressed: (id) junk1 withData: (id) junk2 {
    [self doNotify];
}

@end
