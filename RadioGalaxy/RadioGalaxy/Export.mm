#import "Export.h"

#import "ExportSlider.h"
#import "HelpLabel.h"
#import "MFANAqStreamBuffer.h"
#import "MFANCGUtil.h"
#import "MFANCoreButton.h"
#import "MFANIconButton.h"
#import "MFANWarn.h"
#import "SignView.h"
#import "ViewController.h"

#include "osp.h"

@implementation ExportEntry {
    // all properties
}

- (ExportEntry *) initWithStartTime: (float) start end: (float) end {
    self = [super init];
    if (self != nil) {
	self.start = start;
	self.end = end;
    }

    return self;
}

- (void) setSong: (NSString *) song  alt: (NSString *) alt {
    _song = song;
    if (alt != nil) {
	_alt = alt;
    }
}

@end

@implementation Export {
    ViewController *_vc;
    SignStation *_station;
    MFANAqStreamBuffer *_buffer;

    ExportSlider *_startSlider;
    ExportSlider *_endSlider;
    UITableView *_songTable;
    UIStepper *_stepper;

    // useful buttons
    MFANIconButton *_exportButton;
    MFANIconButton *_cancelButton;
    MFANIconButton *_populateSwitch;
    MFANIconButton *_doneButton;

    MFANStreamPlayer *_samplePlayer;
    NSTimer *_sampleTimer;

    float _lastStepperValue;

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

- (Export *) initWithStation: (SignStation *) station
		    viewCont: (ViewController *) vc
{
    // we get the frame from the view controller
    CGRect tableFrame;
    CGRect buttonFrame;
    CGRect startSliderFrame;
    CGRect endSliderFrame;
    CGRect startLabelFrame;
    CGRect endLabelFrame;
    CGRect frame;

    self.frame = vc.activeFrame;
    frame = vc.activeFrame;

    self = [super initWithFrame: frame];
    if (self != nil) {
	_vc = vc;
	_station = station;

	_buffer = _station.recordingBuffer;
	osp_assert(_buffer != nil);

	_vc = vc;
	
	UIColor *labelColor = [UIColor colorWithRed: 0.8
					      green: 0.8
					       blue: 0.8
					      alpha: 1.0];
	// layout the station name, descr, URL, rate+type
	float boxHeight = frame.size.height * 0.06;
	float labelHeight = boxHeight;
	float okButtonWidth = labelHeight;
	float buttonWidth = frame.size.width * 0.8;
	float sliderLabelPct = 0.12;	// fraction used by the each slider's label

	// 60% for the table
	// 6% for start slider
	// 6% for end slider
	// 6% for populate button
	// spare space
	// 6% for the cancel / export buttons

	// indent things so that we center the label and text box in
	//the frame.

	float viewOffset = 0.0;
	float viewHeight = 0.45 * frame.size.height;

	tableFrame = frame;
	tableFrame.size.height = viewHeight;
	tableFrame.origin.y = viewOffset;

	_songTable = [[UITableView alloc] initWithFrame: tableFrame
						  style:UITableViewStylePlain];
	[_songTable setAllowsMultipleSelection: YES];
	[_songTable setDataSource: self];
	[_songTable setDelegate: self];
	[_songTable setRowHeight: 1.2 * labelHeight];
	[_songTable setSectionIndexMinimumDisplayRowCount: 20];
	[_songTable setBackgroundColor: [UIColor whiteColor]];
	_songTable.sectionIndexBackgroundColor = [UIColor clearColor];
	[_songTable setSeparatorStyle: UITableViewCellSeparatorStyleNone];
	[self addSubview: _songTable];

	viewOffset += viewHeight;
	viewHeight = frame.size.height * .06;
	startSliderFrame = frame;
	startSliderFrame.origin.y = viewOffset;
	startSliderFrame.size.height = viewHeight;

	// and shrink to allow label
	startSliderFrame.origin.x = sliderLabelPct * frame.size.width;
	startSliderFrame.size.width = (1.0 - sliderLabelPct) * frame.size.width;

	_startSlider = [[ExportSlider alloc] initWithFrame: startSliderFrame
						    buffer: _buffer
						     apply: ^(float value) {
		[self playFrom: value];
	    }
						  viewCont: _vc];
	[self addSubview: _startSlider];

	startLabelFrame = startSliderFrame;
	startLabelFrame.origin.x = 0;
	startLabelFrame.size.width = sliderLabelPct * frame.size.width;
	HelpLabel *startHelpLabel = [[HelpLabel alloc]
					   initWithFrame: startLabelFrame
						  target: self
						selector: @selector(startHelp:)];
	[startHelpLabel setTitle: @"Start"
			forState: UIControlStateNormal];
	[self addSubview: startHelpLabel];

	viewOffset += 1.5 * viewHeight;
	viewHeight = labelHeight;
	endSliderFrame = frame;
	endSliderFrame.origin.y = viewOffset;
	endSliderFrame.size.height = viewHeight;

	// and shrink to allow label
	endSliderFrame.origin.x = sliderLabelPct * frame.size.width;
	endSliderFrame.size.width = (1.0 - sliderLabelPct) * frame.size.width;

	_endSlider = [[ExportSlider alloc] initWithFrame: endSliderFrame
						  buffer: _buffer
						   apply: ^(float value) {
		[self playTo: value];
	    }
						viewCont: _vc];
	[self addSubview: _endSlider];

	endLabelFrame = endSliderFrame;
	endLabelFrame.origin.x = 0;
	endLabelFrame.size.width = sliderLabelPct * frame.size.width;
	HelpLabel *endHelpLabel = [[HelpLabel alloc]
					   initWithFrame: endLabelFrame
						  target: self
						selector: @selector(endHelp:)];
	[endHelpLabel setTitle: @"End"
		      forState: UIControlStateNormal];
	[self addSubview: endHelpLabel];

	float currentEndPosition = _buffer.lastPacketEndMs / 1000.0;
	float currentStartPosition = _buffer.firstPacketStartMs / 1000.0;

	// add stepper button
	CGRect stepperFrame;
	_lastStepperValue = 0.0;
	viewOffset += 1.5 * viewHeight;
	viewHeight = labelHeight;
	stepperFrame = frame;
	stepperFrame.origin.x = (frame.size.width - 100) / 2.0;
	stepperFrame.size.width = 100;
	stepperFrame.origin.y = viewOffset;
	stepperFrame.size.height = labelHeight;
	_stepper = [[UIStepper alloc] initWithFrame: stepperFrame];
	_stepper.minimumValue = -1000000.0;
	_stepper.maximumValue = 1000000.0;
	_stepper.stepValue = 1.0;
	_stepper.tintColor = [UIColor blueColor];
	_stepper.backgroundColor = [UIColor colorWithRed: 0.8
						   green: 0.8
						    blue: 0.8
						   alpha: 1.0];
	_stepper.layer.cornerRadius = 16.0;
	
	[_stepper addTarget: self
		     action:@selector(stepperChanged:)
	   forControlEvents:UIControlEventAllEvents];
	[self addSubview: _stepper];
	_stepper.value = 0.0;

	CGRect addButtonFrame;
	viewOffset += 1.5 * viewHeight;
	viewHeight = labelHeight;
	addButtonFrame.origin.x = (frame.size.width - buttonWidth)/2.0;
	addButtonFrame.origin.y = viewOffset;
	addButtonFrame.size.height = labelHeight;
	addButtonFrame.size.width = buttonWidth;

	MFANCoreButton *addButton;
	addButton = [[MFANCoreButton alloc] initWithFrame: addButtonFrame
							 title: @"Border"
							 color: [UIColor blackColor]
					       backgroundColor: labelColor];
	[addButton setFillColor: [UIColor whiteColor]];
	[addButton setClearText: @"Add from range"];
	[addButton addCallback: self
			 withAction: @selector(addRangePressed:)];
	[self addSubview: addButton];

	viewOffset += 1.5 * viewHeight;
	viewHeight = labelHeight;

	CGRect populateButtonFrame;
	populateButtonFrame.origin.x = (frame.size.width - buttonWidth)/2.0;
	populateButtonFrame.origin.y = viewOffset;
	populateButtonFrame.size.height = labelHeight;
	populateButtonFrame.size.width = buttonWidth;

	MFANCoreButton *populateButton;
	populateButton = [[MFANCoreButton alloc] initWithFrame: populateButtonFrame
							 title: @"Border"
							 color: [UIColor blackColor]
					       backgroundColor: labelColor];
	[populateButton setFillColor: [UIColor whiteColor]];
	[populateButton setClearText: @"Fill from song names"];
	[populateButton addCallback: self
			 withAction: @selector(populatePressed:)];
	[self addSubview: populateButton];

	// OK button
	buttonFrame.origin.y = frame.size.height - labelHeight;
	buttonFrame.origin.x = 2*frame.size.width/3 - okButtonWidth/2;
	buttonFrame.size.width = okButtonWidth;
	buttonFrame.size.height = labelHeight;
	_doneButton = [[MFANIconButton alloc] initWithFrame: buttonFrame
					      title: @"OK"
					      color: [UIColor colorWithHue: 0.3
							      saturation: 1.0
							      brightness: 1.0
							      alpha: 1.0]
					      file: @"icon-done.png"];
	[self addSubview: _doneButton];
	[_doneButton addCallback: self withAction:@selector(donePressed:)];

	// Cancel button
	buttonFrame.origin.x = frame.size.width/3 - okButtonWidth/2;
	_cancelButton = [[MFANIconButton alloc] initWithFrame: buttonFrame
					      title: @"Cancel"
					      color: [UIColor colorWithHue: 0.3
							      saturation: 1.0
							      brightness: 1.0
							      alpha: 1.0]
					      file: @"icon-cancel.png"];
	[self addSubview: _cancelButton];
	[_cancelButton addCallback: self withAction:@selector(donePressed:)];

	_didNotify = false;

	[self setBackgroundColor: [UIColor whiteColor]];

	[vc pushTopView: self];
    }

    return self;
}

- (void) stepperChanged: (UIStepper *) stepper {
    NSLog(@"%f value", stepper.value);

    float diff = stepper.value - _lastStepperValue;
    _lastStepperValue = stepper.value;

    if (_startSlider.lastTouchMs >= _endSlider.lastTouchMs) {
	[ _startSlider setValue: [_startSlider getValue] + diff];
    } else {
	[ _endSlider setValue: [_endSlider getValue] + diff];
    }
}

- (void) playTo: (float) value {
    static const float playDuration = 3.0;
    float playTarget;
    NSLog(@"playto %f", value);
    [self stopSample];

    if (value < playDuration)
	playTarget = 0.0;
    else
	playTarget = value - playDuration;

    NSLog(@"starting player");
    _samplePlayer = [[MFANStreamPlayer alloc]
			initWithStreamBuffer: _buffer
					  ms: (uint64_t) (playTarget * 1000)];
    _sampleTimer = [NSTimer scheduledTimerWithTimeInterval: playDuration
						    target: self
						  selector: @selector(stopSampleTimer:)
						  userInfo: nil
						   repeats: NO];
}

- (void) stopSample {
    NSLog(@"in stopsample");
    if (_sampleTimer != nil) {
	[_sampleTimer invalidate];
	_sampleTimer = nil;
    }
    if (_samplePlayer != nil) {
	[_samplePlayer shutdown];
	_samplePlayer = nil;
    }
}

- (void) playFrom: (float) value {
    // make sure we stop anything already playing
    [self stopSample];

    NSLog(@"starting player");
    _samplePlayer = [[MFANStreamPlayer alloc]
			initWithStreamBuffer: _buffer
					  ms: (uint64_t) (value*1000)];
    _sampleTimer = [NSTimer scheduledTimerWithTimeInterval: 3.0
						    target:self
						  selector:@selector(stopSampleTimer:)
						  userInfo:nil
						   repeats: NO];
}

- (void) stopSampleTimer: (id) junk{
    NSLog(@"in stopsampletimer");
    [self stopSample];
}

- (void) donePressed: (id) junk1 {
    [_vc popTopView];
}

- (BOOL)tableView:(UITableView *)tableView 
canEditRowAtIndexPath:(NSIndexPath *)indexPath {
    return true;
}

- (void) tableView: (UITableView *) tview
commitEditingStyle: (UITableViewCellEditingStyle) style
 forRowAtIndexPath: (NSIndexPath *) path {
    long row;

    if (style == UITableViewCellEditingStyleDelete) {
	row = [path row];
	NSLog(@"**remove item at row %d", (int) row);
    }
}

- (NSInteger) numberOfSectionsInTableView:(UITableView *) tview {
    return 1;
}

- (NSInteger) tableView: (UITableView *)tview numberOfRowsInSection: (NSInteger) section {
    // return count of # of rows of data we have
    return 2;
}

- (NSArray *) sectionIndexTitlesForTableView:(UITableView *) tview {
    return nil;
}

/* return unmap index into uitableview's data */
- (unsigned int) indexBySection: (int) section row: (int) row {
    return row;
}

- (void) tableView: (UITableView *) tview
accessoryButtonTappedForRowWithIndexPath: (NSIndexPath *) path {
    NSLog(@"in tapped accessory for row %ld", (long) [path row]);
}

- (UITableViewCell *) tableView: (UITableView *) tview cellForRowAtIndexPath: (NSIndexPath *)path
{
    unsigned int row;
    unsigned int section;
    UITableViewCell *cell;
    UIView *backgroundView;

    /* lookup section and row within section, all zero-based.  We
     * compute ix as the total depth into the combined array.  The
     * variable section gives the # of complete sections we have.
     */
    section = (int) [path section];
    row = (int) [path row];

    // index data by row

    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
				     reuseIdentifier: nil];
    backgroundView = [[UIView alloc] init];
    backgroundView.backgroundColor = [UIColor clearColor];
    cell.multipleSelectionBackgroundView = backgroundView;
    cell.textLabel.text = @"textLabel";
    cell.textLabel.textColor = [UIColor blueColor];
    cell.textLabel.font = [UIFont fontWithName: @"Arial-BoldMT" size: 20];
    cell.textLabel.adjustsFontSizeToFitWidth = YES;

    cell.detailTextLabel.text = @"Detailed info";
    cell.detailTextLabel.font = [UIFont fontWithName: @"Arial-BoldMT" size: 10];
    cell.detailTextLabel.textColor = [UIColor colorWithRed: 0.0
						     green: 0.5
						      blue: 0.0
						     alpha: 1.0];
    cell.detailTextLabel.adjustsFontSizeToFitWidth = YES;

    if (true)
	cell.accessoryType = UITableViewCellAccessoryCheckmark;
    else
	cell.accessoryType = UITableViewCellAccessoryNone;

    // can set cell.imageView if necessary

    /* make cell clear */
    cell.contentView.backgroundColor = [UIColor clearColor];
    cell.backgroundView.backgroundColor = [UIColor clearColor];
    cell.multipleSelectionBackgroundView.backgroundColor = [UIColor clearColor];
    cell.selectedBackgroundView.backgroundColor = [UIColor clearColor];
    cell.backgroundColor = [UIColor clearColor];

    return cell;
}

- (UISwipeActionsConfiguration *) tableView: (UITableView *) tview
trailingSwipeActionsConfigurationForRowAtIndexPath: (NSIndexPath *) path
{
    // long row = [path row];

    NSString *playString;

    if (true) {
	playString = @"Stop Sampling";
    } else {
	playString = @"Sample";
    }

    UIContextualAction *playAction =
	[UIContextualAction contextualActionWithStyle:UIContextualActionStyleNormal
						title:playString
					      handler:^(UIContextualAction *action,
							UIView *sourceView,
							void (^complete)(BOOL)) {
		// do the work for the action
		NSLog(@"action run");
		complete(true);
	    }];
    playAction.backgroundColor = [UIColor blueColor];

    return [UISwipeActionsConfiguration configurationWithActions: @[playAction]];
}

- (void) activateTopView {
    // if the edit command did a remove, don't stay on the status
    // page, since the station doesn't exist anymore.
    return;
}

- (void) populateHelp: (id) junk {
    NSLog(@"write populate help");
}

- (void) startHelp: (id) junk {
}

- (void) endHelp: (id) junk {
}

- (void) populatePressed: (id) junk {
    NSLog(@"write populate from file code");
}

- (void) addRangePressed: (id) junk {
    NSLog(@"addRange pressed");
}

- (void) deactivateTopView {
    [_startSlider shutdown];
    _startSlider = nil;
    [_endSlider shutdown];
    _endSlider = nil;
    return;
}

@end
