#import "EditStation.h"
#import "MFANCGUtil.h"
#import "MFANIconButton.h"
#import "MFANCoreButton.h"
#import "MFANWarn.h"
#import "SignView.h"
#import "ViewController.h"

@implementation EditStation {
    ViewController *_vc;
    UITextField *_nameView;
    UITextField *_descrView;
    UITextField *_urlView;
    UILabel *_nameLabel;
    UILabel *_descrLabel;
    UILabel *_urlLabel;
    SignStation *_station;
    SignView *_signView;

    MFANIconButton *_doneButton;
    MFANIconButton *_cancelButton;
    MFANCoreButton *_removeButton;
    MFANCoreButton *_recordButton;

    // properties
    NSString *_stationName;
    NSString *_streamUrl;
    NSString *_shortDescr;

    id _callbackObj;
    SEL _callbackSel;

    BOOL _canceled;
    BOOL _doRemove;
}

- (void) setCallback: (id) obj withSel: (SEL) sel {
    _callbackObj = obj;
    _callbackSel = sel;
}

- (EditStation *) initWithFrame: (CGRect) frame
			station: (SignStation *) station
		       signView: (SignView *) signView
		       viewCont: (ViewController *) vc {
    CGRect boxFrame;
    CGRect buttonFrame;
    CGRect labelFrame;

    self.frame = vc.view.frame;
    frame = vc.view.frame;

    self = [super initWithFrame: frame];
    if (self != nil) {
	float boxHeight = frame.size.height * 0.06;
	float boxWidth = frame.size.width * 0.65;
	float labelHeight = boxHeight;
	float labelWidth = frame.size.width * 0.25;
	// indent things so that we center the label and text box in
	//the frame.
	float indent = (frame.size.width - labelWidth - boxWidth) / 2;
	UIColor *labelColor = [UIColor colorWithRed: 0.9
					      green: 0.9
					       blue:0.9
					      alpha: 1.0];

	_vc = vc;
	_station = station;
	_signView = signView;
	_stationName = [NSString stringWithString: station.stationName];
	_shortDescr = [NSString stringWithString: station.shortDescr];
	_streamUrl = [NSString stringWithString: station.streamUrl];

	_canceled = NO;
	_doRemove = NO;

	labelFrame = frame;
	labelFrame.origin.x = indent;
	labelFrame.origin.y += vc.topMargin;
	labelFrame.size.width = labelWidth;
	labelFrame.size.height = labelHeight;

	_nameLabel = [[UILabel alloc] initWithFrame:labelFrame];
	_nameLabel.backgroundColor = [UIColor clearColor];
	_nameLabel.text = @"Name";
	_nameLabel.textColor = [UIColor blackColor];
	_nameLabel.textAlignment = NSTextAlignmentLeft;
	[self addSubview: _nameLabel];

	boxFrame = frame;
	boxFrame.origin.y += vc.topMargin;
	boxFrame.origin.x += indent + labelWidth;
	boxFrame.size.width = boxWidth;
	boxFrame.size.height = boxHeight;

	/* add first text box */
	_nameView = [[UITextField alloc] initWithFrame: boxFrame];
	[_nameView setDelegate: self];
	[_nameView setReturnKeyType: UIReturnKeyDone];
	[_nameView setTextColor: [UIColor blackColor]];
	[_nameView setAutocapitalizationType: UITextAutocapitalizationTypeNone];
	[_nameView setAutocorrectionType: UITextAutocorrectionTypeNo];
	_nameView.text = station.stationName;
	_nameView.borderStyle = UITextBorderStyleBezel;

	[_nameView setBackgroundColor: [UIColor clearColor]];
	[self addSubview: _nameView];

	// add secondlabel
	labelFrame.origin.y += labelHeight + frame.size.height * 0.05;
	_descrLabel = [[UILabel alloc] initWithFrame:labelFrame];
	_descrLabel.backgroundColor = [UIColor clearColor];
	_descrLabel.text = @"Description";
	_descrLabel.textColor = [UIColor blackColor];
	_descrLabel.textAlignment = NSTextAlignmentLeft;
	[self addSubview: _descrLabel];

	/* add second text box */
	boxFrame.origin.y += boxHeight + frame.size.height * 0.05;
	_descrView = [[UITextField alloc] initWithFrame: boxFrame];
	[_descrView setDelegate: self];
	[_descrView setReturnKeyType: UIReturnKeyDone];
	[_descrView setAutocapitalizationType: UITextAutocapitalizationTypeNone];
	[_descrView setAutocorrectionType: UITextAutocorrectionTypeNo];
	[_descrView setKeyboardType: UIKeyboardTypeURL];
	[_descrView setTextColor: [UIColor blackColor]];
	_descrView.borderStyle = UITextBorderStyleBezel;
	_descrView.text = station.shortDescr;

	[_descrView setBackgroundColor: [UIColor clearColor]];
	[self addSubview: _descrView];

	// add third label
	labelFrame.origin.y += labelHeight + frame.size.height * 0.05;
	_urlLabel = [[UILabel alloc] initWithFrame:labelFrame];
	_urlLabel.backgroundColor = [UIColor clearColor];
	_urlLabel.text = @"Stream URL";
	_urlLabel.textColor = [UIColor blackColor];
	_urlLabel.textAlignment = NSTextAlignmentLeft;
	[self addSubview: _urlLabel];

	/* add third text box */
	boxFrame.origin.y += boxHeight + frame.size.height * 0.05;
	_urlView = [[UITextField alloc] initWithFrame: boxFrame];
	[_urlView setDelegate: self];
	[_urlView setReturnKeyType: UIReturnKeyDone];
	[_urlView setAutocapitalizationType: UITextAutocapitalizationTypeNone];
	[_urlView setAutocorrectionType: UITextAutocorrectionTypeNo];
	[_urlView setKeyboardType: UIKeyboardTypeURL];
	[_urlView setTextColor: [UIColor blackColor]];
	_urlView.borderStyle = UITextBorderStyleBezel;
	_urlView.text = station.streamUrl;

	[_urlView setBackgroundColor: [UIColor clearColor]];
	[self addSubview: _urlView];

	boxFrame.origin.y += boxHeight + frame.size.height * 0.05;
	boxFrame.origin.x = indent;
	boxFrame.size.width = frame.size.width - 2*indent;
	MFANCoreButton *recordButton;
	recordButton = [[MFANCoreButton alloc] initWithFrame: boxFrame
						       title: @"Border"
						       color: [UIColor blackColor]
					     backgroundColor: labelColor];
	[recordButton setFillColor: labelColor];
	if (_station.isRecording)
	    [recordButton setClearText: @"Stream only while playing"];
	else
	    [recordButton setClearText: @"Stream always"];
	[recordButton addCallback: self
		     withAction: @selector(recordPressed:)];
	[self addSubview: recordButton];


	/* now add Done button */
	float buttonWidth = frame.size.width/8;
	float removeButtonWidth = frame.size.width/3;

	buttonFrame = frame;
	buttonFrame.origin.y = 0.9*frame.size.height;
	buttonFrame.size.height = buttonWidth;	// square buttons
	buttonFrame.origin.x = frame.size.width/6 - buttonWidth/2;
	buttonFrame.size.width = buttonWidth;
	_doneButton = [[MFANIconButton alloc] initWithFrame: buttonFrame
					      title: @"Done"
					      color: [UIColor colorWithHue: 0.4
							      saturation: 1.0
							      brightness: 0.56
							      alpha: 1.0]
					      file: @"icon-done.png"];
	[_doneButton addCallback: self
		     withAction: @selector(donePressed:withData:)];
	[self addSubview: _doneButton];
    
	buttonFrame.origin.x = frame.size.width*3/6 - removeButtonWidth/2;
	buttonFrame.size.width = removeButtonWidth;
	_removeButton = [[MFANCoreButton alloc] initWithFrame: buttonFrame
							title: @"Border"
							color: [UIColor redColor]
					      backgroundColor: [UIColor clearColor]];
	[_removeButton setFillColor: [UIColor clearColor]];
	[_removeButton setClearText: @"Remove"];
	[_removeButton addCallback: self
		     withAction: @selector(removePressed:withData:)];
	[self addSubview: _removeButton];

	/* and cancel button */
	buttonFrame.origin.x = frame.size.width*5/6 - buttonWidth/2;
	buttonFrame.size.width = buttonWidth;
	_cancelButton = [[MFANIconButton alloc] initWithFrame: buttonFrame
						title: @"Cancel"
							color: [UIColor colorWithHue: 0.02
									  saturation: 1.0
									  brightness: 0.56
									       alpha: 1.0]
						file: @"icon-cancel.png"];
	[_cancelButton addCallback: self
		       withAction: @selector(cancelPressed:withData:)];
	[self addSubview: _cancelButton];


	[vc pushTopView: self];

	[self setBackgroundColor: [UIColor whiteColor]];
    }

    return self;
}

- (void) doNotify {
    if (_callbackObj != nil) {
	[_callbackObj performSelectorOnMainThread: _callbackSel
				       withObject: nil
				    waitUntilDone: true];
    }
}

- (void) removePressed: (id) junk1 withData: (id) junk2 {
    UIAlertController *alert = [UIAlertController
				   alertControllerWithTitle: @"RadioStar"
						    message: @"Are you sure?"
					     preferredStyle: UIAlertControllerStyleAlert];

    UIAlertAction *action = [UIAlertAction actionWithTitle:@"Remove station"
                                                     style: UIAlertActionStyleDefault
                                                   handler:^(UIAlertAction *act) {
	    self->_doRemove = YES;
	    [self doNotify];
	}];
    [alert addAction: action];

    action = [UIAlertAction actionWithTitle:@"Cancel remove"
				      style:UIAlertActionStyleDefault
				    handler:^(UIAlertAction *act) {
	    self->_canceled = YES;
	    [self doNotify];
	}];
    [alert addAction: action];

    [_vc presentViewController: alert animated:YES completion: nil];
}

- (void) recordPressed: (id) junk1 {
    NSString *notice;
    NSLog(@"record pressed");
    if (_station.isRecording) {
	[_signView stopRecording: _station];
	notice = @"Will stop recording upon switching stations";
    } else {
	[_signView startRecording: _station];
	notice = @"Will keep recording even after switching stations";
    }

    (void) [[MFANWarn alloc] initWithTitle: @"Streaming" message: notice secs: 1.2];

    [self doNotify];
}

- (void) donePressed: (id) junk1 withData: (id) junk2 {
    _canceled = false;
    _stationName = _nameView.text;
    _shortDescr = _descrView.text;
    _streamUrl = _urlView.text;
    [self doNotify];
}

- (void) cancelPressed: (id) junk1 withData: (id) junk2 {
    _canceled = true;
    [self doNotify];
}

// make keyboard disappear when return is pressed
- (BOOL) textFieldShouldReturn:(UITextField *) textField {
    [textField resignFirstResponder];
    return YES;
}

- (void) activateTopView {
    return;
}

- (void) deactivateTopView {
    return;
}

@end
