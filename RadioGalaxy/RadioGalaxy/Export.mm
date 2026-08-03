#import "Export.h"

#import "ExportEntry.h"
#import "ExportSlider.h"
#import "HelpLabel.h"
#import "MFANAqStreamBuffer.h"
#import "MFANCGUtil.h"
#import "MFANCoreButton.h"
#import "MFANIconButton.h"
#import "MFANWarn.h"
#import "MarqueeLabel.h"
#import "SignView.h"
#import "TopView.h"
#import "ViewController.h"

#include "osp.h"
#include <pthread.h>

@implementation Export {
    ViewController *_vc;
    SignStation *_station;
    MFANAqStreamBuffer *_buffer;

    ExportSlider *_startSlider;
    ExportSlider *_endSlider;
    UITableView *_songTable;
    UIStepper *_stepper;
    long _selectedRow;
    HelpLabel *_startHelpLabel;
    HelpLabel *_endHelpLabel;

    // useful buttons
    MFANIconButton *_exportButton;
    MFANIconButton *_cancelButton;
    MFANIconButton *_populateSwitch;
    MFANIconButton *_doneButton;

    MarqueeLabel *_marquee;

    MFANStreamPlayer *_samplePlayer;
    NSTimer *_sampleTimer;
    int32_t _sampleIndex;

    float _lastStepperValue;

    id _callbackObj;
    SEL _callbackSel;
    bool _didNotify;

    NSThread *_scanThread;

    UIAlertController *_alert;

    NSMutableArray *_recordings;	// of ExportEntry objects
    bool _populateDone;
    bool _populateCanceled;
    uint32_t _populatePct;		// % through the downloaded music scanner is
    NSString *_populateSong;		// name of last song populated
    pthread_mutex_t _populateLock;
    NSTimer *_populateTimer;		// timer for probing

    // We have a different UI while playing a full entry -- in playing
    // mode, only the start slider is visible, and current playing
    // time is reflected in the slider.
    bool _playingMode;			// true if we're in playing mode
}

static const float _kPlayDuration = 4.0;

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
	pthread_mutex_init(&_populateLock, nullptr);

	_recordings = station.exportRecordings;
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
	float labelHeightFactor = 1.25;

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

	_selectedRow = -1;
	_populateDone = false;
	_populateCanceled = false;

	// this value is a non-negative integer if we're playing a
	// song from the songTable, or -1 if we're playing something
	// else (like when using the position sliders).
	_sampleIndex = -1;

	viewOffset += viewHeight + frame.size.height * .02;;
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
		if (self->_playingMode) {
		    [self playSeek: value];
		} else {
		    [self playFrom: value];
		}
	    }
						  viewCont: _vc];
	[self addSubview: _startSlider];

	startLabelFrame = startSliderFrame;
	startLabelFrame.origin.x = 0;
	startLabelFrame.size.width = sliderLabelPct * frame.size.width;
	_startHelpLabel = [[HelpLabel alloc]
			      initWithFrame: startLabelFrame
				     target: self
				   selector: @selector(startHelp:)];
	[_startHelpLabel setTitle: @"Start"
			 forState: UIControlStateNormal];
	[self addSubview: _startHelpLabel];

	viewOffset += labelHeightFactor * viewHeight;
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
	_endHelpLabel = [[HelpLabel alloc]
			    initWithFrame: endLabelFrame
				   target: self
				 selector: @selector(endHelp:)];
	[_endHelpLabel setTitle: @"End"
		      forState: UIControlStateNormal];
	[self addSubview: _endHelpLabel];

	CGRect marqueeFrame;
	viewOffset += labelHeightFactor * viewHeight;
	viewHeight = labelHeight;

	marqueeFrame = frame;
	marqueeFrame.origin.y = viewOffset;
	marqueeFrame.size.height = viewHeight;

	_marquee = [[MarqueeLabel alloc] initWithFrame: marqueeFrame];
	[_marquee setTextColor: [UIColor blackColor]];
	[_marquee setTextAlignment: NSTextAlignmentCenter];
	[_marquee setFont: [UIFont fontWithName: @"Arial-BoldMT" size: 30]];
	[_marquee setText: @"[Unknown]"];
	[self addSubview: _marquee];

	// add stepper button
	CGRect stepperFrame;
	_lastStepperValue = 0.0;
	viewOffset += labelHeightFactor * viewHeight;
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
	viewOffset += labelHeightFactor * viewHeight;
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

	viewOffset += labelHeightFactor * viewHeight;
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
	[populateButton setClearText: @"Add all recorded songs"];
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
	_playingMode = false;

	[self setBackgroundColor: [UIColor whiteColor]];

	[vc pushTopView: self];
    }

    return self;
}

- (void) enterPlayingMode {
    if (_playingMode)
	return;

    _playingMode = true;
    [_endSlider removeFromSuperview];
    [_startHelpLabel removeFromSuperview];
    [_endHelpLabel removeFromSuperview];
}

- (void) leavePlayingMode {
    if (!_playingMode)
	return;

    _playingMode = false;
    [self addSubview: _endSlider];
    [self addSubview: _startHelpLabel];
    [self addSubview: _endHelpLabel];
}

- (void) stepperChanged: (UIStepper *) stepper {
    NSLog(@"%f value", stepper.value);

    float diff = stepper.value - _lastStepperValue;
    _lastStepperValue = stepper.value;

    if (_playingMode) {
	if (_samplePlayer != nil) {
	    uint64_t  currentTimestamp = [_samplePlayer getSeekTarget: 0.0];
	    // Note that a single tap will come in with a diff of 0.
	    if (diff >= 0)
		[ _startSlider setValue: (currentTimestamp / 1000.0)  + diff + 2.0];
	    else
		[ _startSlider setValue: (currentTimestamp / 1000.0)  + diff  - 2.0];
	}
    } else {
	if (_startSlider.lastTouchMs >= _endSlider.lastTouchMs) {
	    [ _startSlider setValue: [_startSlider getValue] + diff];
	} else {
	    [ _endSlider setValue: [_endSlider getValue] + diff];
	}
    }
}

- (void) retrieveNameAt: (float) time {
    uint64_t ms = (uint64_t) (time * 1000);
    NSString *song;

    song = [_buffer nameAt: ms];
    [_marquee setText: song];
}

- (void) playTo: (float) value {
    float playTarget;
    NSLog(@"playto %f", value);
    [self stopSample];
    [self retrieveNameAt: value];

    if (value < _kPlayDuration)
	playTarget = 0.0;
    else
	playTarget = value - _kPlayDuration;

    NSLog(@"starting player");
    _sampleIndex = -1;
    _samplePlayer = [[MFANStreamPlayer alloc]
			initWithStreamBuffer: _buffer
					  ms: (uint64_t) (playTarget * 1000)];
    _sampleTimer = [NSTimer scheduledTimerWithTimeInterval: _kPlayDuration
						    target: self
						  selector: @selector(stopSampleTimer:)
						  userInfo: nil
						   repeats: NO];
}

- (void) playIndex: (uint64_t) ix {
    ExportEntry *ep;

    [self stopSample];

    [self enterPlayingMode];

    // remember what we're playing
    _sampleIndex = (uint32_t) ix;

    ep = _recordings[ix];
    float duration = ep.end - ep.start;
    NSLog(@"playing entryIndex=%lld with duration=%f at start=%f",
	  ix, duration, ep.start);
    _samplePlayer = [[MFANStreamPlayer alloc]
			initWithStreamBuffer: _buffer
					  ms: (uint64_t) (ep.start * 1000.0)];

    _startSlider.value = ep.start;

    if (_sampleTimer != nil) {
	[_sampleTimer invalidate];
	_sampleTimer = nil;
    }
}

- (void) playSeek: (float) target {
    [self stopSample];

    _samplePlayer = [[MFANStreamPlayer alloc]
			initWithStreamBuffer: _buffer
					  ms: (uint64_t) target * 1000.0];
    if (_sampleTimer != nil) {
	[_sampleTimer invalidate];
	_sampleTimer = nil;
    }
}

- (void) stopSample {
    NSLog(@"in stopsample");

    _sampleIndex = -1;		// only set while playing an entry
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
    [self retrieveNameAt: value];

    NSLog(@"starting player");
    _sampleIndex = -1;
    _samplePlayer = [[MFANStreamPlayer alloc]
			initWithStreamBuffer: _buffer
					  ms: (uint64_t) (value*1000)];
    _sampleTimer = [NSTimer scheduledTimerWithTimeInterval: _kPlayDuration
						    target:self
						  selector:@selector(stopSampleTimer:)
						  userInfo:nil
						   repeats: NO];
}

- (void) stopSampleTimer: (id) junk{
    NSLog(@"in stopsampletimer");
    [self leavePlayingMode];
    [self stopSample];
}

- (void) donePressed: (id) junk1 {
    [self stopSample];
    [self leavePlayingMode];
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
    return [_recordings count];
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
    ExportEntry *ep;
    NSString *details;

    /* lookup section and row within section, all zero-based.  We
     * compute ix as the total depth into the combined array.  The
     * variable section gives the # of complete sections we have.
     */
    section = (int) [path section];
    row = (int) [path row];

    ep = _recordings[row];

    // index data by row

    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
				     reuseIdentifier: nil];
    backgroundView = [[UIView alloc] init];
    backgroundView.backgroundColor = [UIColor clearColor];
    cell.multipleSelectionBackgroundView = backgroundView;
    cell.textLabel.text = ep.label;
    cell.textLabel.textColor = [UIColor blueColor];
    cell.textLabel.font = [UIFont fontWithName: @"Arial-BoldMT" size: 20];
    cell.textLabel.adjustsFontSizeToFitWidth = YES;

    details = [ExportSlider stringFromTime: ep.end - ep.start];
    cell.detailTextLabel.text = details;
    cell.detailTextLabel.font = [UIFont fontWithName: @"Arial-BoldMT" size: 16];
    cell.detailTextLabel.textColor = [UIColor colorWithRed: 0.0
						     green: 0.5
						      blue: 0.0
						     alpha: 1.0];
    cell.detailTextLabel.adjustsFontSizeToFitWidth = YES;

    cell.accessoryType = UITableViewCellAccessoryNone;

    // can set cell.imageView if necessary

    /* make cell clear */
    UIColor *selectedColor = [UIColor colorWithRed: 1.0
					     green: 1.0
					      blue: 0.7
					     alpha: 1.0];
    if (_selectedRow == row) {
	cell.contentView.backgroundColor = selectedColor;
    } else if (ep.damaged) {
	cell.contentView.backgroundColor = [UIColor colorWithRed: 1.0
							   green: 0.7
							    blue: 0.7
							   alpha: 1.0];
    } else {
	cell.contentView.backgroundColor = [UIColor colorWithRed: 0.7
							   green: 1.0
							    blue: 0.7
							   alpha: 1.0];
    }
    cell.backgroundView.backgroundColor = [UIColor clearColor];
    cell.multipleSelectionBackgroundView.backgroundColor = [UIColor clearColor];
    cell.selectedBackgroundView.backgroundColor = [UIColor clearColor];
    cell.backgroundColor = [UIColor clearColor];

    return cell;
}

- (void) tableView: (UITableView *) tview
didSelectRowAtIndexPath:(NSIndexPath *) path {
    long row = [path row];
    if (row == _selectedRow)
	_selectedRow = -1;
    else
	_selectedRow = row;
    [_songTable reloadData];

    ExportEntry *ep = _recordings[row];

    _startSlider.value = ep.start;
    _endSlider.value = ep.end;
    NSLog(@"did selection row=%ld", row);
}

- (UISwipeActionsConfiguration *) tableView: (UITableView *) tview
trailingSwipeActionsConfigurationForRowAtIndexPath: (NSIndexPath *) path
{
    long row = [path row];
    ExportEntry *ep = self->_recordings[row];

    UIContextualAction *exportAction;
    if (!ep.saved) {
	exportAction =
	    [UIContextualAction contextualActionWithStyle:UIContextualActionStyleNormal
						    title:@"Export"
						  handler:^(UIContextualAction *action,
							    UIView *sourceView,
							    void (^complete)(BOOL)) {
		    // do the work for the action
		    NSLog(@"performe export work");
		    [self saveFile2: ep];
		    complete(true);
		}];
	exportAction.backgroundColor = [UIColor colorWithRed: 0.0
						       green: 0.5
							blue: 0.0
						       alpha: 1.0];
    } else {
	exportAction =
	    [UIContextualAction contextualActionWithStyle:UIContextualActionStyleNormal
						    title:@"Delete"
						  handler:^(UIContextualAction *action,
							    UIView *sourceView,
							    void (^complete)(BOOL)) {
		    // do the work for the action
		    NSLog(@"performe delete work");
		    [self removeFile: ep];
		    [self->_songTable reloadData];
		    complete(true);
		}];
	exportAction.backgroundColor = [UIColor colorWithRed: 0.5
						       green: 0.0
							blue: 0.0
						       alpha: 1.0];
    }

    UIContextualAction *playAction;
    if (_samplePlayer == nil || row != _sampleIndex) {
	playAction = [UIContextualAction
			 contextualActionWithStyle:UIContextualActionStyleNormal
					     title:@"Play"
					   handler:^(UIContextualAction *action,
						     UIView *sourceView,
						     void (^complete)(BOOL)) {
		// do the work for the action
		NSLog(@"performe play work");
		[self playIndex: row];
		self->_sampleIndex = (int32_t) row;
		complete(true);
	    }];
    } else {
	// stop the player
	playAction = [UIContextualAction
			 contextualActionWithStyle:UIContextualActionStyleNormal
					     title:@"Stop player"
					   handler:^(UIContextualAction *action,
						     UIView *sourceView,
						     void (^complete)(BOOL)) {
		// do the work for the action
		NSLog(@"performe play work");
		[self stopSample];
		[self leavePlayingMode];
		complete(true);
	    }];
    }
    playAction.backgroundColor = [UIColor colorWithRed: 0.5
						 green: 0.0
						  blue: 0.0
						 alpha: 1.0];

    UIContextualAction *updateAction;
    updateAction = [UIContextualAction
		       contextualActionWithStyle:UIContextualActionStyleNormal
					   title:@"Update times"
					 handler:^(UIContextualAction *action,
						   UIView *sourceView,
						   void (^complete)(BOOL)) {
	    // do the work for the action
	    NSLog(@"performe update work");
	    [self updateTimesForRow: row];
	    complete(true);
	}];

    updateAction.backgroundColor = [UIColor colorWithRed: 0.0
						   green: 0.0
						    blue: 0.5
						   alpha: 1.0];

    UISwipeActionsConfiguration *config =
	[UISwipeActionsConfiguration
	    configurationWithActions: @[exportAction, playAction, updateAction]];
    config.performsFirstActionWithFullSwipe = false;
    return config;
}

- (void) updateTimesForRow: (long) row {
    float startTime;
    float endTime;

    ExportEntry *ep = _recordings[row];
    startTime = [_startSlider getValue];
    endTime = [_endSlider getValue];
    if (startTime >= endTime) {
	(void) [[TopAlert alloc] initWithMessage: @"Start slider time must precede end time"
					duration: 1.5
					viewCont: _vc];
	return;
    }

    ep.start = startTime;
    ep.end = endTime;
}

- (void) activateTopView {
    // if the edit command did a remove, don't stay on the status
    // page, since the station doesn't exist anymore.
    [self leavePlayingMode];
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
    [_recordings removeAllObjects];
    _populateDone = false;
    _populateCanceled = false;
    _scanThread = [[NSThread alloc] initWithTarget: self
					  selector: @selector(scanAsync:)
					    object: nil];
    [_scanThread start];

    [self monitorScan];
}

- (void) promptFor: (NSString *) prompt
	   default:(NSString *) def
	   handler: (PromptContinuation) continueAt {
    UIAlertController *alert;
    alert = [UIAlertController alertControllerWithTitle: @"RadioGalaxy"
						message: prompt
					 preferredStyle: UIAlertControllerStyleAlert];

    [alert  addTextFieldWithConfigurationHandler:^(UITextField *textField) {
	    textField.text = def;
	    textField.placeholder = @"";
	    textField.secureTextEntry = NO; // Set to YES for passwords
	    NSLog(@"=6= setup %@", textField.text);
	}];

    UIAlertAction *action = [UIAlertAction actionWithTitle:@"Save"
						     style: UIAlertActionStyleDefault
						   handler:^(UIAlertAction *act) {
	    UITextField *field = alert.textFields.firstObject;
	    NSLog(@"=6=  %@ TODO call saveFile with right ep", field.text);
	    continueAt(field.text);
	    }];

    [alert addAction: action];

    action = [UIAlertAction actionWithTitle: @"Cancel"
				      style: UIAlertActionStyleDefault
				    handler:^(UIAlertAction *action) {
	}];
    [alert addAction: action];

    [_vc presentViewController: alert animated:YES completion: nil];
}

- (void) addRangePressed: (id) junk {
    ExportEntry *ep;
    float startTime;
    float endTime;
    float midTime;
    NSString *song;

    startTime = [_startSlider getValue];
    endTime = [_endSlider getValue];
    midTime = (startTime + endTime) / 2;
    if (startTime >= endTime) {
	(void) [[TopAlert alloc]
		   initWithMessage: @"Start time (slider) must be earlier than end"
			  duration: 10.0
			  viewCont: _vc];
	return;
    }

    song = [_buffer nameAt: (uint64_t)(midTime * 1000)];

    ep = [[ExportEntry alloc] initWithStartTime: startTime end: endTime];
    if ([song length] == 0 || [song isEqualToString:@"[Unknown]"]) {
	song = @"";
    }

    NSString *prompt;
    bool damaged = [self damagedEntry: ep];
    prompt = (damaged? @"Song name (damaged)" : @"Song name");

    // I guess this is easier than building an entire screen to push
    // into the viewcontroller's stack, but I'm not sure.
    [self promptFor: prompt default: song handler:^(NSString *value) {
	    NSLog(@"=6= in addrangepart2 with %@", value);
	    if ([value length] == 0)
		return;
	    ep.label = value;
	    [self->_recordings addObject: ep];
	    [self->_songTable reloadData];
	    [self saveFile2: ep];
	}];
}

- (void) deactivateTopView {
    [_startSlider shutdown];
    _startSlider = nil;
    [_endSlider shutdown];
    _endSlider = nil;
    return;
}

- (void) splitLabel: (NSString *) label
	      group: (NSString **) group
	       song: (NSString **) song
	      album: (NSString **) album {

    NSArray<NSString *> *parsed = [label componentsSeparatedByString: @"-"];
    uint64_t parsedCount = [parsed count];
    if (parsedCount == 0) {
	*group = @"Unknown";
	*song = @"Unknown";
	*album = @"Unknown";
    } else if (parsedCount == 1) {
	*group = @"Unknown";
	*song = [parsed[0] stringByTrimmingCharactersInSet:
			   NSCharacterSet.whitespaceCharacterSet];
	*album = @"";

    } else if (parsedCount == 2) {
	*group = [parsed[0] stringByTrimmingCharactersInSet:
			   NSCharacterSet.whitespaceCharacterSet];
	*song = [parsed[1] stringByTrimmingCharactersInSet:
			   NSCharacterSet.whitespaceCharacterSet];
	*album = @"";

    } else {
	*group = [parsed[0] stringByTrimmingCharactersInSet:
			    NSCharacterSet.whitespaceCharacterSet];
	*song = [parsed[1] stringByTrimmingCharactersInSet:
			   NSCharacterSet.whitespaceCharacterSet];
	*album = [parsed[2] stringByTrimmingCharactersInSet:
			    NSCharacterSet.whitespaceCharacterSet];
    }
}

- (NSString *) generateName: (ExportEntry *) ep isMp3: (bool) isMp3 {
    NSString *entryName;
    NSArray<NSString *> *parsed = [ep.label componentsSeparatedByString: @"-"];
    uint64_t parsedCount = [parsed count];

    if (parsedCount == 0)
	return (isMp3? @"Unknown.mp3" : @"Unknown.aac");
    else if (parsedCount == 1) {
	entryName = [parsed[0] stringByTrimmingCharactersInSet:
			       NSCharacterSet.whitespaceCharacterSet];
    } else {
	// in 2 element, it is group-song, in 3 element it is
	// group-song-album.
	entryName = [parsed[1] stringByTrimmingCharactersInSet:
			       NSCharacterSet.whitespaceCharacterSet];
    }
    entryName = [entryName stringByAppendingString: (isMp3? @".mp3" : @".aac")];
    entryName = [entryName stringByReplacingOccurrencesOfString: @"/" withString: @"-"];
    return fileNameForDoc(entryName);
}

- (int32_t) writeId3V2ToFile: (FILE *) filep
		    entry: (ExportEntry *) ep {

    NSString *groupName;
    NSString *songName;
    NSString *albumName;

    [self splitLabel: ep.label
	       group: &groupName
		song: &songName
	       album: &albumName];

    ExportId3V2 *id3Writer = [[ExportId3V2 alloc] initWithGroup: groupName
							   song: songName
							  album: albumName];
    NSData *idData = [id3Writer getId];
    uint64_t count = [idData length];
    const char *datap = (const char *) idData.bytes;
    int64_t code = fwrite(datap, 1, count, filep);
    if (code == count)
	return 0;
    else
	return -1;
}

- (void) finishMp3File: (FILE *) filep
		 entry: (ExportEntry *) ep {
    char tbuffer[130];
    char *tp;
    NSString *groupName;
    NSString *songName;
    NSString *albumName;

    [self splitLabel: ep.label
	       group: &groupName
		song: &songName
	       album: &albumName];

    /* put out old style MP3 trailer (easiest to do) */
    memset(tbuffer, 0, sizeof(tbuffer));
    tp = tbuffer;

    *tp++ = 'T';	/* id3v1 tag */
    *tp++ = 'A';
    *tp++ = 'G';

    strncpy(tp, [songName cStringUsingEncoding: NSUTF8StringEncoding], 30);
    tp += 30;
    strncpy(tp, [groupName cStringUsingEncoding: NSUTF8StringEncoding], 30);
    tp += 30;
    strncpy(tp, [albumName cStringUsingEncoding: NSUTF8StringEncoding], 30);
    tp += 30;

    /* write out recordingYear */
    uint32_t recordingYear = 2026;
    *tp++ = (recordingYear/1000) + '0';	/* fails at year 10000 */
    *tp++ = ((recordingYear/100) % 10) + '0';
    *tp++ = ((recordingYear/10) % 10) + '0';
    *tp++ = (recordingYear % 10) + '0';

    /* comment */
    strcpy(tp, "Saved by RadioGalaxy");
    tp += 30;

    *tp++ = 92;	// prog rock

    /* write out MP3 v1 tag */
    fwrite(tbuffer, 1, 128, filep);
}

- (int32_t) removeFile: (ExportEntry *) ep {
    NSString *fileName;
    AudioStreamBasicDescription dataFormat;
    bool isMp3;

    [_buffer getDataFormat: &dataFormat];
    isMp3 = (dataFormat.mFormatID == '.mp3');

    fileName = [self generateName: ep isMp3: isMp3];

    bool success = [[NSFileManager defaultManager] removeItemAtPath: fileName
							      error: nil];
    if (success) {
	ep.saved = false;
	return 0;
    } else {
	return -1;
    }
}

- (void) monitorScan {
    _alert = [UIAlertController
		 alertControllerWithTitle: @"Scanning recordings"
				  message: @"Starting"
			   preferredStyle: UIAlertControllerStyleAlert];

    UIAlertAction *action = [UIAlertAction actionWithTitle:@"Cancel"
                                                     style: UIAlertActionStyleCancel
                                                   handler:^(UIAlertAction *act) {
	    self->_populateCanceled = true;
	}];

    [_alert addAction: action];

    [_vc presentViewController: _alert animated:YES completion: nil];

    _populateTimer = [NSTimer scheduledTimerWithTimeInterval: 0.5
						      target:self
						    selector:@selector(monitorUpdate:)
						    userInfo:nil
						     repeats: YES];
}

- (void) monitorUpdate: (id) junk {
    NSString *updatedMessage;

    if (_populateCanceled) {
	updatedMessage = [NSString stringWithFormat: @"Canceling at %d%% done", _populatePct];
    } else {
	updatedMessage = [NSString stringWithFormat: @"Added song %@, %d%% done",
				   _populateSong, _populatePct];
    }
    _alert.message = updatedMessage;

    pthread_mutex_lock(&_populateLock);
    [_songTable reloadData];
    pthread_mutex_unlock(&_populateLock);

    if (_populateDone) {
	[_alert dismissViewControllerAnimated: YES completion:nil];
	[_populateTimer invalidate];
    }
}

- (void) scanAsync: (id) junk {
    NSString *startLabel;
    uint64_t startMs = 0;
    uint64_t endMs = 0;
    bool first = true;
    bool bad;
    ExportEntry *ep;
    MFANAqStreamReader *reader;
    MFANAqStreamPacket *p;
    uint64_t populateStartMs;
    uint64_t populateEndMs;

    reader = [[MFANAqStreamReader alloc]
		  initWithBuffer: _buffer];
    reader.noWait = true;

    [reader seek: 0  whence: 0];

    bad = false;
    while(true) {
	p = [reader read];
	if (p == nil) {
	    break;
	}

	// canceled, stop early
	if (_populateCanceled)
	    break;

	if (first) {
	    startMs = p.startMs;
	    endMs = startMs + p.durationMs;
	    startLabel = p.playingSong;
	    first = false;
	    bad = (p.flags & [MFANAqStreamPacket kMagicFlagError]);
	    continue;
	}

	if ( [startLabel isEqualToString: p.playingSong] ||
	     [p.playingSong length] == 0) {
	    endMs = p.startMs + p.durationMs;
	    if (p.flags & [MFANAqStreamPacket kMagicFlagError])
		bad = true;
	    continue;
	}

	// new song
	ep = [[ExportEntry alloc] initWithStartTime: startMs/1000.0
						end: endMs/1000.0];
	ep.label = startLabel;
	if (bad)
	    ep.damaged = true;

	pthread_mutex_lock(&_populateLock);
	_populateSong = startLabel;
	[_recordings addObject: ep];
	pthread_mutex_unlock(&_populateLock);

	populateStartMs = _buffer.firstPacketStartMs;
	populateEndMs = _buffer.lastPacketEndMs;

	startLabel = p.playingSong;
	startMs = p.startMs;
	endMs = p.startMs + p.durationMs;
	bad = (p.flags & [MFANAqStreamPacket kMagicFlagError]);

	_populatePct = (uint32_t) (100 * (endMs - populateStartMs) /
				   (populateEndMs - populateStartMs));
    }

    pthread_mutex_lock(&_populateLock);
    _populateDone = true;
    pthread_mutex_unlock(&_populateLock);

    pthread_exit(nullptr);
}

#if 0
- (int32_t) saveFile: (ExportEntry *) ep {
    const char *fileNamep;
    NSString *fileName;
    FILE *filep = nullptr;
    MFANAqStreamReader *reader;
    MFANAqStreamPacket *p;
    AudioStreamBasicDescription dataFormat;
    bool isMp3;
    uint64_t bytesWritten;
    uint64_t byteCount;
    uint64_t packetSize;
    uint64_t endMs;
    int64_t code;
    NSMutableData *adtsHeader;

    [_buffer getDataFormat: &dataFormat];
    isMp3 = (dataFormat.mFormatID == '.mp3');

    reader = [[MFANAqStreamReader alloc]
		  initWithBuffer: _buffer];
    [reader seek: (uint64_t) (ep.start * 1000) whence: 0];
    reader.noWait = true;

    fileName = [self generateName: ep isMp3: isMp3];
    fileNamep = [fileName cStringUsingEncoding: NSUTF8StringEncoding];

    filep = fopen(fileNamep, "w");
    if (filep == nullptr) {
	return -1;
    }

    endMs = (uint64_t)(ep.end * 1000);
    while(true) {
	p = [reader read];
	if (p == nil)
	    break;
	if (p.startMs >= endMs)
	    break;
	packetSize = [p getLength];

	// if AAC file, write out the ADTS header, which we retrieve from
	// the buffer.
	if (!isMp3) {
	    // must be aac
	    adtsHeader = [_buffer getAdtsHeaderForLength: (int32_t) packetSize];
	    byteCount = adtsHeader.length;
	    bytesWritten = fwrite([adtsHeader bytes], 1, byteCount, filep);
	    if (bytesWritten != byteCount)
		return -1;
	}

	bytesWritten = fwrite([p getData], 1, packetSize, filep);
	if (bytesWritten != packetSize) {
	    [reader close];
	    return -1;
	}
    }

    if (isMp3) {
	[self finishMp3File: filep entry: ep];
    }

    code = fflush(filep);
    fsync(fileno(filep));
    code = fclose(filep);

    ep.saved = true;

    return 0;
}
#endif

- (int32_t) saveFile2: (ExportEntry *) ep {
    const char *fileNamep;
    NSString *fileName;
    FILE *filep = nullptr;
    MFANAqStreamReader *reader;
    MFANAqStreamPacket *p;
    AudioStreamBasicDescription dataFormat;
    bool isMp3;
    uint64_t bytesWritten;
    uint64_t byteCount;
    uint64_t packetSize;
    uint64_t endMs;
    int64_t code;
    NSMutableData *adtsHeader;

    [_buffer getDataFormat: &dataFormat];
    isMp3 = (dataFormat.mFormatID == '.mp3');

    reader = [[MFANAqStreamReader alloc]
		  initWithBuffer: _buffer];
    [reader seek: (uint64_t) (ep.start * 1000) whence: 0];
    reader.noWait = true;

    fileName = [self generateName: ep isMp3: isMp3];
    fileNamep = [fileName cStringUsingEncoding: NSUTF8StringEncoding];

    filep = fopen(fileNamep, "w");
    if (filep == nullptr) {
	return -1;
    }

    endMs = (uint64_t)(ep.end * 1000);

    // officially .aac files aren't support to have ID3 tags.
    [self writeId3V2ToFile: filep entry:ep];

    if (!isMp3 && !_station.warnedAac) {
	_station.warnedAac = true;
	(void) [[TopAlert alloc]
		   initWithMessage: @"Saving .aac file.  Can convert to .m4a with:\n"
		   @"ffmpeg -i foo.aac -c:a copy foo.m4a"
			  duration: 10.0
			  viewCont: _vc];
    }

    while(true) {
	p = [reader read];
	if (p == nil)
	    break;
	if (p.startMs >= endMs)
	    break;
	packetSize = [p getLength];

	// if AAC file, write out the ADTS header, which we retrieve from
	// the buffer.
	if (!isMp3) {
	    // must be aac
	    adtsHeader = [_buffer getAdtsHeaderForLength: (int32_t) packetSize];
	    byteCount = adtsHeader.length;
	    bytesWritten = fwrite([adtsHeader bytes], 1, byteCount, filep);
	    if (bytesWritten != byteCount)
		return -1;
	}

	bytesWritten = fwrite([p getData], 1, packetSize, filep);
	if (bytesWritten != packetSize) {
	    [reader close];
	    return -1;
	}
    }

#if 0
    if (isMp3) {
	[self finishMp3File: filep entry: ep];
    }
#endif

    code = fflush(filep);
    fsync(fileno(filep));
    code = fclose(filep);

    ep.saved = true;

    return 0;
}

- (bool) damagedEntry: (ExportEntry *) ep {
    MFANAqStreamReader *reader;
    MFANAqStreamPacket *p;
    MFANAqStreamPacket *badPacket = nil;
    AudioStreamBasicDescription dataFormat;
    bool isMp3;
    uint64_t packetSize;
    uint64_t endMs;
    bool bad;

    [_buffer getDataFormat: &dataFormat];
    isMp3 = (dataFormat.mFormatID == '.mp3');

    reader = [[MFANAqStreamReader alloc]
		  initWithBuffer: _buffer];
    [reader seek: (uint64_t) (ep.start * 1000) whence: 0];
    reader.noWait = true;

    endMs = (uint64_t)(ep.end * 1000);

    bad = false;
    uint32_t badCount = 0;
    uint32_t badLength = 0;
    while(true) {
	p = [reader read];
	if (p == nil)
	    break;
	if (p.startMs >= endMs)
	    break;
	packetSize = [p getLength];
	if (p.flags & [MFANAqStreamPacket kMagicFlagError]) {
	    bad = true;
	    if (badPacket == nil) {
		badLength = [p getLength];
		badPacket = p;
		NSLog(@"=c= found first bad packet at ms=%lld", p.startMs);
	    }
	    badCount++;
	}
    }

    NSLog(@"=c= found %d bad packets", badCount);

    if (bad) {
	[reader seek: (uint64_t) (ep.start * 1000) whence: 0];
	while(true) {
	    p = [reader read];
	    if (p == nil)
		break;
	    if (p.startMs >= endMs)
		break;
	    if (p.flags & [MFANAqStreamPacket kMagicFlagError]) {
		break;
	    }

	    if ( [p getLength] == badLength &&
		 memcmp([p getData], [badPacket getData], badLength) == 0) {
		NSLog(@"=c= found bad packet again at ms=%lld", p.startMs);
		break;
	    }
	}
    }

    return bad;
}

@end

@implementation ExportId3V2 {
    NSMutableData *_buffer;
}

- (uint64_t) frameSizeForString: (NSString *) ins {
    // we don't even append frames for empty strings
    if ([ins length] == 0)
	return 0;

    // 4 bytes frame ID, 4 bytes size, 2 bytes flags, 1 byte encoding,
    // one byte for terminating null in UTF-8 data (since we're
    // writing version 2.4 tags).
    return 12 + [ins length];
}

- (void) appendSyncSafe: (uint64_t) val {
    char tdata[4];
    tdata[0] = (val >> 21) & 0x7F;
    tdata[1] = (val >> 14) & 0x7F;
    tdata[2] = (val >> 7) & 0x7F;
    tdata[3] = val & 0x7F;
    [_buffer appendBytes: tdata length: 4];
}

- (void) appendFrameType: (const char *) type
		   value: (NSString *) value {

    const char *valueString = [value cStringUsingEncoding: NSUTF8StringEncoding];

    // NSString length may not count multibyte characters as N bytes.
    // the valueLength does include the null termination required for
    // C strings and id3 v2.4 string tags.
    uint64_t valueLength = strlen(valueString) + 1;

    // we don't even put out empty frames.
    if (valueLength <= 1)
	return;

    // the frame size includes the encoding byte (0x03 for UTF8), the
    // contents and a terminating null (already counted in
    // valueLength).
    uint64_t frameSize = valueLength + 1;
    [_buffer appendBytes: type length: 4];
    [self appendSyncSafe: frameSize];

    uint32_t zeroData = 0;

    // append flags
    [_buffer appendBytes: &zeroData length: 2];

    char encoding = 0x03;
    [_buffer appendBytes: &encoding length:  1];

    // valueString and valueLength both include the null terminating
    // byte required for v2.4 string frames.
    [_buffer appendBytes: valueString length: valueLength];
}

- (ExportId3V2 *) initWithGroup: (NSString *) group
			   song: (NSString *) song
			  album: (NSString *) album {
    self = [super init];
    if (self != nil) {
	_buffer = [[NSMutableData alloc] initWithCapacity: 256];

	char header[6];
	header[0] = 'I';
	header[1] = 'D';
	header[2] = '3';
	header[3] = 4;		// version 2.4.0
	header[4] = 0;
	header[5] = 0;		// flags
	[_buffer appendBytes: header length: sizeof(header)];

	// the size field doesn't include the 10 byte header (the 6 bytes above
	// plus the length of the entire tag.
	uint64_t totalLength;
	totalLength = [self frameSizeForString: group] + [self frameSizeForString: song] +
	    [self frameSizeForString: album];
	[self appendSyncSafe: totalLength];

	[self appendFrameType: "TIT2" value: song];
	[self appendFrameType: "TPE1" value: group];
	[self appendFrameType: "TALB" value: album];
    }
    return self;
}

- (NSData *) getId {
    return _buffer;
}
@end
