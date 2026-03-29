#import "PopStatus.h"
#import "MFANCGUtil.h"
#import "MFANCoreButton.h"
#import "MFANIconButton.h"
#import "SignView.h"
#import "ViewController.h"
#import "MarqueeLabel.h"

#include "osp.h"

@implementation PopStatus {
    ViewController *_vc;
    MFANIconButton *_doneButton;
    MFANStreamPlayer *_player;
    MFANAqStream *_stream;
    SignStation *_station;
    SignView *_signView;

    uint64_t _startTimeMs;

    // tags
    UILabel *_stationNameLabel;
    UILabel *_stationDescrLable;
    UILabel *_searchUrlLabel;
    UILabel *_realUrlLabel;
    UILabel *_speedAndTypeLabel;
    UILabel *_publicUrlLabel;
    UILabel *_finalUrlLabel;

    // properties
    MarqueeLabel *_stationNameView;
    MarqueeLabel *_stationDescrView;
    MarqueeLabel *_searchUrlView;
    MarqueeLabel *_realUrlView;
    UILabel *_speedAndTypeView;
    MarqueeLabel *_publicUrlView;
    MarqueeLabel *_finalUrlView;

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
		     signView: (SignView *) signView
{
    // we get the frame from the view controller
    CGRect boxFrame;
    CGRect labelFrame;
    CGRect buttonFrame;

    self = [super initWithFrame: frame];
    if (self != nil) {
	_vc = vc;
	_signView = signView;
	_player = [signView getCurrentPlayer];
	_stream = [signView getCurrentStream];
	_station = [signView getCurrentStation];
	_vc = vc;
	
	UIColor *screenColor = [UIColor colorWithRed: 0.3
					       green: 0.3
						blue: 0.3
					       alpha: 0.75];

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
	float buttonWidth = frame.size.width * 0.80;
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
	[_stationNameView setText: _station.stationName];
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

	// third text box
	labelFrame.origin.y += labelHeight;
	_publicUrlLabel = [[UILabel alloc] initWithFrame: labelFrame];
	_publicUrlLabel.backgroundColor = labelColor;
	_publicUrlLabel.text = @"Public URL";
	_publicUrlLabel.textColor = [UIColor blackColor];
	_publicUrlLabel.textAlignment = NSTextAlignmentLeft;
	[self addSubview: _publicUrlLabel];

	boxFrame.origin.y += boxHeight;
	_publicUrlView = [[MarqueeLabel alloc] initWithFrame: boxFrame];
	[_publicUrlView setTextColor: textColor];
	[_publicUrlView setBackgroundColor: valueColor];
	[_publicUrlView setText: [_player getPublicUrl]];
	[self addSubview: _publicUrlView];

	// Align each button in the full width, assuming we remove
	// buttonIndent from each end
#if 0
	float buttonIndent = 0;
#endif

	buttonFrame = labelFrame;
	buttonFrame.origin.y += labelHeight + frame.size.height * 0.05;
	buttonFrame.origin.x = frame.size.width/2 - buttonWidth/2;
	buttonFrame.size.height = labelHeight;
	buttonFrame.size.width = buttonWidth;

	MFANCoreButton *recordButton;
	recordButton = [[MFANCoreButton alloc] initWithFrame: buttonFrame
						       title: @"Border"
						       color: [UIColor blackColor]
					     backgroundColor: [UIColor whiteColor]];
	[recordButton setFillColor: [UIColor clearColor]];
	if (_station.isRecording)
	    [recordButton setClearText: @"Stop recording"];
	else
	    [recordButton setClearText: @"Start recording"];
	[recordButton addCallback: self
		     withAction: @selector(recordPressed:withData:)];
	[self addSubview: recordButton];

	buttonFrame.origin.y += labelHeight + frame.size.height * 0.03;
	recordButton = [[MFANCoreButton alloc] initWithFrame: buttonFrame
						       title: @"Border"
						       color: [UIColor blackColor]
					     backgroundColor: [UIColor whiteColor]];
	[recordButton setFillColor: [UIColor clearColor]];
	[recordButton setClearText: @"Highlight"];
	[recordButton addCallback: self
		     withAction: @selector(highlightPressed:withData:)];
	[self addSubview: recordButton];

#if 0
	// fourth text box
	labelFrame.origin.y += labelHeight;
	_finalUrlLabel = [[UILabel alloc] initWithFrame: labelFrame];
	_finalUrlLabel.backgroundColor = labelColor;
	_finalUrlLabel.text = @"Final URL";
	_finalUrlLabel.textColor = [UIColor blackColor];
	_finalUrlLabel.textAlignment = NSTextAlignmentLeft;
	[self addSubview: _finalUrlLabel];

	boxFrame.origin.y += boxHeight;
	_finalUrlView = [[MarqueeLabel alloc] initWithFrame: boxFrame];
	[_finalUrlView setTextColor: textColor];
	[_finalUrlView setBackgroundColor: valueColor];
	[_finalUrlView setText: [_stream getFinalUrl]];
	[self addSubview: _finalUrlView];
#endif

	[self addTarget: self
	     action:@selector(donePressed:withEvent:)
	     forControlEvents: UIControlEventTouchUpInside];

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

    // have the status disappear on its own after a minute
    if (osp_time_ms() - _startTimeMs > 60000) {
	[self doNotify];
    }
}

- (void) shutdown {
    [_timer invalidate];
    _timer = nil;
}

- (void) highlightPressed: (id) junk1 withData:(id) junk2 {
    NSLog(@"highlight pressed");
    [self doNotify];
}

- (void) recordPressed: (id) junk1 withData:(id) junk2 {
    NSLog(@"record pressed");
    if (_station.isRecording)
	[_signView stopRecording];
    else
	[_signView startRecording];

    [self doNotify];
}

- (void) donePressed: (id) junk1 withEvent: (UIEvent *) junk2 {
    [self doNotify];
}

@end
