#import "ManualStation.h"
#import "MFANCGUtil.h"
#import "MFANIconButton.h"
#import "ViewController.h"

@implementation ManualStation {
    ViewController *_vc;
    UITextField *_nameView;
    UITextField *_urlView;
    UILabel *_nameLabel;
    UILabel *_urlLabel;
    MFANIconButton *_doneButton;
    MFANIconButton *_cancelButton;

    // properties
    NSString *_stationName;
    NSString *_stationUrl;
    bool _canceled;

    id _callbackObj;
    SEL _callbackSel;
}

- (void) setCallback: (id) obj withSel: (SEL) sel {
    _callbackObj = obj;
    _callbackSel = sel;
}

- (ManualStation *) initWithViewCont: (ViewController *) vc {
    // we get the frame from the view controller
    _vc = vc;
    self.frame = vc.view.frame;
    CGRect frame = vc.view.frame;
    CGRect boxFrame;
    CGRect buttonFrame;
    CGRect labelFrame;

    self = [super initWithFrame: frame];
    if (self != nil) {
	float boxHeight = frame.size.height * 0.06;
	float boxWidth = frame.size.width * 0.65;
	float labelHeight = boxHeight;
	float labelWidth = frame.size.width * 0.25;
	// indent things so that we center the label and text box in
	//the frame.
	float indent = (frame.size.width - labelWidth - boxWidth) / 2;

	labelFrame = frame;
	labelFrame.origin.x = indent;
	labelFrame.origin.y += vc.topMargin;
	labelFrame.size.width = labelWidth;
	labelFrame.size.height = labelHeight;

	_nameLabel = [[UILabel alloc] initWithFrame:labelFrame];
	_nameLabel.backgroundColor = [UIColor clearColor];
	_nameLabel.text = @"Call letters";
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
	_nameView.borderStyle = UITextBorderStyleBezel;

	[_nameView setBackgroundColor: [UIColor clearColor]];
	[self addSubview: _nameView];

	labelFrame.origin.y += labelHeight + frame.size.height * 0.05;
	_urlLabel = [[UILabel alloc] initWithFrame:labelFrame];
	_urlLabel.backgroundColor = [UIColor clearColor];
	_urlLabel.text = @"Stream URL";
	_urlLabel.textColor = [UIColor blackColor];
	_urlLabel.textAlignment = NSTextAlignmentLeft;
	[self addSubview: _urlLabel];

	/* add second text box */
	boxFrame.origin.y += boxHeight + frame.size.height * 0.05;
	_urlView = [[UITextField alloc] initWithFrame: boxFrame];
	[_urlView setDelegate: self];
	[_urlView setReturnKeyType: UIReturnKeyDone];
	[_urlView setAutocapitalizationType: UITextAutocapitalizationTypeNone];
	[_urlView setAutocorrectionType: UITextAutocorrectionTypeNo];
	[_urlView setKeyboardType: UIKeyboardTypeURL];
	[_urlView setTextColor: [UIColor blackColor]];
	_urlView.borderStyle = UITextBorderStyleBezel;
	_urlView.text = @"http://";

	[_urlView setBackgroundColor: [UIColor clearColor]];
	[self addSubview: _urlView];

	/* now add Done button */
	float buttonWidth = frame.size.width/8;

	buttonFrame = frame;
	buttonFrame.origin.y = 0.9*frame.size.height;
	buttonFrame.size.height = buttonWidth;	// square buttons
	buttonFrame.origin.x = frame.size.width/3 - buttonWidth/2;
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
    
	/* and cancel button */
	buttonFrame.origin.x = frame.size.width*2/3 - buttonWidth/2;
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


	[self setBackgroundColor: [UIColor colorWithHue:0.0
					   saturation: 0.0
					   brightness: 0.0
					   alpha: 0.6]];
    }

    return self;
}

- (void) doNotify {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    if (_callbackObj != nil) {
	[_callbackObj  performSelector: _callbackSel withObject: nil];
    }
#pragma clang diagnostic pop
}

- (void) donePressed: (id) junk1 withData: (id) junk2 {
    _canceled = false;
    _stationName = _nameView.text;
    _stationUrl = _urlView.text;
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

@end
