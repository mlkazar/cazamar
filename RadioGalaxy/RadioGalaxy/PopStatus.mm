#import "HelpLabel.h"
#import "PopStatus.h"
#import "MFANCGUtil.h"
#import "MFANCoreButton.h"
#import "MFANIconButton.h"
#import "MFANWarn.h"
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
    BufferSlider *_seekSlider;
    EditStation *_editStation;

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

    if (_callbackObj != nil) {
	[_callbackObj  performSelectorOnMainThread: _callbackSel
					withObject: nil
				     waitUntilDone: true];
    }
}

- (PopStatus *) initWithFrame: (CGRect) frame
		  editStation: (EditStation *) editStation
		      station: (SignStation *) station
		     viewCont: (ViewController *) vc
		     signView: (SignView *) signView
{
    // we get the frame from the view controller
    CGRect boxFrame;
    CGRect labelFrame;
    CGRect buttonFrame;

    self.frame = vc.activeFrame;
    frame = vc.activeFrame;

    self = [super initWithFrame: frame];
    if (self != nil) {
	_vc = vc;
	_editStation = editStation;
	_signView = signView;
	_player = [signView getCurrentPlayer];
	if (station != nil)
	    _station = station;
	else
	    _station = [signView getCurrentStation];

	if (_station.recordingStream != nil)
	    _stream = station.recordingStream;
	else if (_station == [signView getCurrentStation])
	    _stream = [signView getCurrentStream];
	else
	    _stream = nil;

	_vc = vc;
	
	UIColor *screenColor = [UIColor colorWithRed: 0.3
					       green: 0.3
						blue: 0.3
					       alpha: 1.0];

	UIColor *textColor = [UIColor blackColor];
	UIColor *labelColor = [UIColor colorWithRed: 0.9
					      green: 0.9
					       blue: 0.9
					      alpha: 1.0];
	UIColor *valueColor = labelColor;

	self.backgroundColor = screenColor;

	// layout the station name, descr, URL, rate+type
	float boxHeight = frame.size.height * 0.06;
	float boxWidth = frame.size.width * 0.60;
	float labelHeight = boxHeight;
	float labelWidth = frame.size.width * 0.35;
	float okButtonWidth = labelHeight;

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
	//_stationNameLabel.layer.borderWidth = 1.0;
	//_stationNameLabel.layer.borderColor = [UIColor blackColor].CGColor;
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
	//_stationNameView.layer.borderWidth = 1.0;
	//_stationNameView.layer.borderColor = [UIColor blackColor].CGColor;
	[self addSubview: _stationNameView];

	
	labelFrame.origin.y += labelHeight + frame.size.height * 0.05;
	_speedAndTypeLabel = [[UILabel alloc] initWithFrame:labelFrame];
	_speedAndTypeLabel.backgroundColor = labelColor;
	_speedAndTypeLabel.text = @"Stream rate";
	_speedAndTypeLabel.textColor = [UIColor blackColor];
	_speedAndTypeLabel.textAlignment = NSTextAlignmentLeft;
	//_speedAndTypeLabel.layer.borderWidth = 1.0;
	//_speedAndTypeLabel.layer.borderColor = [UIColor blackColor].CGColor;
	[self addSubview: _speedAndTypeLabel];

	/* add second text box */
	boxFrame.origin.y += boxHeight + frame.size.height * 0.05;
	_speedAndTypeView = [[UILabel alloc] initWithFrame: boxFrame];
	[_speedAndTypeView setTextColor: textColor];
	[_speedAndTypeView setBackgroundColor: valueColor];
	//_speedAndTypeView.layer.borderWidth = 1.0;
	//_speedAndTypeLabel.layer.borderColor = [UIColor blackColor].CGColor;
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
	//_publicUrlLabel.layer.borderWidth = 1.0;
	//_publicUrlLabel.layer.borderColor = [UIColor blackColor].CGColor;
	[self addSubview: _publicUrlLabel];

	boxFrame.origin.y += boxHeight;
	_publicUrlView = [[MarqueeLabel alloc] initWithFrame: boxFrame];
	[_publicUrlView setTextColor: textColor];
	[_publicUrlView setBackgroundColor: valueColor];
	[_publicUrlView setText: [_stream getPublicUrl]];
	//_publicUrlView.layer.borderWidth = 1.0;
	//_publicUrlView.layer.borderColor = [UIColor blackColor].CGColor;
	[self addSubview: _publicUrlView];

	// Align each button in the full width, assuming we remove
	// indent from each end
	float textLabelWidth = (2.0/3.0 * (frame.size.width - 2*indent));
	float switchWidth = (frame.size.width - 2*indent) - textLabelWidth;
	CGRect recordTextFrame;
	CGRect recordSwitchFrame;
	CGRect freezeLabelFrame;
	CGRect freezeSwitchFrame;
	HelpLabel *recordHelpLabel;
	HelpLabel *freezeHelpLabel;
	MFANIconButton *recordSwitch;
	UISwitch *freezeSwitch;

	// Stream in background button
	recordTextFrame = frame;
	recordTextFrame.origin.y += labelFrame.origin.y + labelHeight + frame.size.height * 0.03;
	recordTextFrame.origin.x = indent;
	recordTextFrame.size.height = labelHeight;
	recordTextFrame.size.width = textLabelWidth;

	recordHelpLabel = [[HelpLabel alloc]
			     initWithFrame: recordTextFrame
				    target: self
				  selector: @selector(recordHelp:)];
	[recordHelpLabel setTitle: @"Stop background streaming"
			forState: UIControlStateNormal];

	[self addSubview: recordHelpLabel];

	recordSwitchFrame = recordTextFrame;
	recordSwitchFrame.origin.x = recordTextFrame.origin.x + recordTextFrame.size.width;
	recordSwitchFrame.size.width = switchWidth;
	recordSwitch = [[MFANIconButton alloc] initWithFrame: recordSwitchFrame
						       title: @"Stop"
						       color: [UIColor clearColor]
							file: @"icon-button.png"];
	[recordSwitch addCallback: self
		       withAction: @selector(recordPressed:)];
	[self addSubview: recordSwitch];

	freezeLabelFrame.origin.x = indent;
	freezeLabelFrame.size.width = frame.size.width * 3 / 5;
	freezeLabelFrame.origin.y = recordTextFrame.origin.y +
	    labelHeight + frame.size.height * 0.03;
	freezeLabelFrame.size.height = labelHeight;
	freezeHelpLabel = [[HelpLabel alloc]
			      initWithFrame: freezeLabelFrame
				     target: self
				   selector: @selector(freezeHelp:)];
	[freezeHelpLabel setTitle: @"Freeze stream downloading"
			 forState: UIControlStateNormal];
	[self addSubview: freezeHelpLabel];
	// now the switch
	freezeSwitchFrame = freezeLabelFrame;
	freezeSwitchFrame.origin.x = freezeLabelFrame.origin.x + freezeLabelFrame.size.width;
	freezeSwitchFrame.size.width = switchWidth;
	freezeSwitch = [[UISwitch alloc] initWithFrame: freezeSwitchFrame];

	[freezeSwitch addTarget: self
			 action:@selector(freezeStream:)
	       forControlEvents:UIControlEventAllEvents];
	[self addSubview: freezeSwitch];
	[freezeSwitch setOn: _station.isFrozen animated: false];
	freezeSwitch.backgroundColor = [UIColor blackColor];
	freezeSwitch.layer.cornerRadius = 16.0;
	[self addSubview: freezeSwitch];

	buttonFrame.origin.x = frame.size.width / 5;
	buttonFrame.size.width = frame.size.width * 3 / 5;
	buttonFrame.origin.y = freezeLabelFrame.origin.y + labelHeight + frame.size.height * 0.03;
	buttonFrame.size.height = labelHeight;

	MFANCoreButton *recordButton;
	recordButton = [[MFANCoreButton alloc] initWithFrame: buttonFrame
						       title: @"Border"
						       color: [UIColor blackColor]
					     backgroundColor: labelColor];
	[recordButton setFillColor: labelColor];
	[recordButton setClearText: @"Edit station"];
	[recordButton addCallback: self
		       withAction: @selector(editPressed:withData:)];
	[self addSubview: recordButton];

	buttonFrame.origin.y = frame.size.height - labelHeight;
	buttonFrame.origin.x = frame.size.width/2 - okButtonWidth/2;
	buttonFrame.size.width = okButtonWidth;
	buttonFrame.size.height = okButtonWidth;
	_doneButton = [[MFANIconButton alloc] initWithFrame: buttonFrame
					      title: @"Done"
					      color: [UIColor colorWithHue: 0.3
							      saturation: 1.0
							      brightness: 1.0
							      alpha: 1.0]
					      file: @"icon-done.png"];
	[self addSubview: _doneButton];
	[_doneButton addCallback: self withAction:@selector(donePressed:)];

	_startTimeMs = osp_time_ms();

	_didNotify = false;

	[self setBackgroundColor: [UIColor whiteColor]];

	_timer = [NSTimer scheduledTimerWithTimeInterval: 1.0
						  target: self
						selector: @selector(updateStats:)
						userInfo: nil
						 repeats: YES];
    }

    return self;
}

- (void) recordHelp: (id) junk {
    UIAlertController *alert =
	[UIAlertController
	    alertControllerWithTitle: @"RadioGalaxy"
			     message:@"Keep streaming data this station even after "
	    "switching to other stations."
		      preferredStyle: UIAlertControllerStyleAlert];
    UIAlertAction *action = [UIAlertAction
				actionWithTitle: @"OK"
					  style:UIAlertActionStyleDefault
					handler:^(UIAlertAction *act) {
	    NSLog(@"blah");
	}];
    [alert addAction: action];
    [_vc presentViewController: alert animated: YES completion: nil];
}

- (void) editPressed: (id) junk1 withData: (id) junk2 {
    [_vc pushTopView: _editStation];
}

- (void) freezeHelp: (id) junk {
    UIAlertController *alert =
	[UIAlertController
	    alertControllerWithTitle: @"RadioGalaxy"
			     message:@"Keep streaming data this station even after "
	    "switching to other stations."
		      preferredStyle: UIAlertControllerStyleAlert];
    UIAlertAction *action = [UIAlertAction
				actionWithTitle: @"OK"
					  style:UIAlertActionStyleDefault
					handler:^(UIAlertAction *act) {
	    NSLog(@"blah");
	}];
    [alert addAction: action];
    [_vc presentViewController: alert animated: YES completion: nil];
}

- (void) freezeStream: (UISwitch *) swp {
    [_signView freezeStation: _station frozen: swp.on];
}

- (void) highlightHelp: (id) junk {
    UIAlertController *alert =
	[UIAlertController
	    alertControllerWithTitle: @"RadioGalaxy"
			     message:@"Toggle whether the latest song for this station"
	    @" is highlighted in the history"
		      preferredStyle: UIAlertControllerStyleAlert];
    UIAlertAction *action = [UIAlertAction
				actionWithTitle: @"OK"
					  style:UIAlertActionStyleDefault
					handler:^(UIAlertAction *act) {
	    NSLog(@"blah");
	}];
    [alert addAction: action];
    [_vc presentViewController: alert animated: YES completion: nil];
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

- (void) highlightPressed: (id) junk1 {
    NSString *notice;

    NSLog(@"highlight pressed");

    [_signView.history toggleHighlightInStation:_station.stationName];

    if ([_signView.history isHighlightedInStation:_station.stationName]) {
	notice = @"Highlighted last song for later";
    } else {
	notice = @"Removed highlighting for current song";
    }
}

- (void) recordPressed: (UIButton *) s {
    [_signView stopRecording: _station];
}

#if 0
// Is this really useful?
- (void) mutePressed: (id) junk1 withData:(id) junk2 {
    NSString *notice;
    NSLog(@"mutepressed");
    if (_player == nil)
	return;

    if (_player.muted) {
	[_player unmute];
	notice = @"Mute off";
    } else {
	[_player mute];
	notice = @"Muted";
    }
}
#endif

- (void) donePressed: (id) junk1 {
    [_timer invalidate];
    _timer = nil;

    [self doNotify];
}

- (void) activateTopView {
    // if the edit command did a remove, don't stay on the status
    // page, since the station doesn't exist anymore.
    if (_editStation.doRemove) {
	[self donePressed: nil];
    }
    return;
}

- (void) deactivateTopView {
    [_seekSlider shutdown];
    _seekSlider = nil;
    return;
}

@end
