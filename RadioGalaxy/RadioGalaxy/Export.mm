#import "Export.h"

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

@implementation ExportEntry {
    // all properties
}

- (ExportEntry *) initWithStartTime: (float) start end: (float) end {
    self = [super init];
    if (self != nil) {
	self.start = start;
	self.end = end;
	self.saved = false;
    }

    return self;
}

- (void) setSong: (NSString *) song  alt: (NSString *) alt {
    _song = song;
    if (alt != nil) {
	_altSong = alt;
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
    long _selectedRow;

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

	_recordings = [[NSMutableArray alloc] init];
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
	HelpLabel *endHelpLabel = [[HelpLabel alloc]
					   initWithFrame: endLabelFrame
						  target: self
						selector: @selector(endHelp:)];
	[endHelpLabel setTitle: @"End"
		      forState: UIControlStateNormal];
	[self addSubview: endHelpLabel];

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

    // remember what we're playing
    _sampleIndex = ix;

    ep = _recordings[ix];
    float duration = ep.end - ep.start;
    NSLog(@"playing entryIndex=%lld with duration=%f at start=%f",
	  ix, duration, ep.start);
    _samplePlayer = [[MFANStreamPlayer alloc]
			initWithStreamBuffer: _buffer
					  ms: (uint64_t) (ep.start * 1000.0)];
    _sampleTimer = [NSTimer scheduledTimerWithTimeInterval: duration
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
    cell.textLabel.text = ep.song;
    cell.textLabel.textColor = [UIColor blueColor];
    cell.textLabel.font = [UIFont fontWithName: @"Arial-BoldMT" size: 20];
    cell.textLabel.adjustsFontSizeToFitWidth = YES;

    cell.detailTextLabel.text = ep.altSong;
    cell.detailTextLabel.font = [UIFont fontWithName: @"Arial-BoldMT" size: 10];
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
					      blue: 0.8
					     alpha: 1.0];
    if (_selectedRow == row) {
	cell.contentView.backgroundColor = selectedColor;
    } else {
	cell.contentView.backgroundColor = [UIColor clearColor];
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
		    [self saveFile: ep];
		    complete(true);
		}];
	exportAction.backgroundColor = [UIColor greenColor];
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
		    [self->_recordings removeObjectAtIndex:row];
		    [self->_songTable reloadData];
		    complete(true);
		}];
	exportAction.backgroundColor = [UIColor redColor];
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
		complete(true);
	    }];
    }
    playAction.backgroundColor = [UIColor blueColor];

    return [UISwipeActionsConfiguration configurationWithActions: @[exportAction, playAction]];
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

    // I guess this is easier than building an entire screen to push
    // into the viewcontroller's stack, but I'm not sure.
    [self promptFor:@"Song name" default: song handler:^(NSString *value) {
	    NSLog(@"=6= in addrangepart2 with %@", value);
	    if ([value length] == 0)
		return;
	    ep.song = value;
	    [self->_recordings addObject: ep];
	    [self->_songTable reloadData];
	    [self saveFile: ep];
	}];
}

- (void) deactivateTopView {
    [_startSlider shutdown];
    _startSlider = nil;
    [_endSlider shutdown];
    _endSlider = nil;
    return;
}

- (NSString *) generateName: (ExportEntry *) ep isMp3: (bool) isMp3 {
    NSString *entryName;
    NSArray<NSString *> *parsed = [ep.song componentsSeparatedByString: @"-"];
    uint64_t parsedCount = [parsed count];

    if (parsedCount == 0)
	return (isMp3? @"Unknown.mp3" : @"Unknown.aac");
    entryName = [parsed[parsedCount-1] stringByTrimmingCharactersInSet:
			   NSCharacterSet.whitespaceCharacterSet];
    entryName = [entryName stringByAppendingString: (isMp3? @".mp3" : @".aac")];
    return fileNameForDoc(entryName);
}

- (void) finishMp3File: (FILE *) filep
		 entry: (ExportEntry *) ep {
    char tbuffer[130];
    char *tp;

    /* put out old style MP3 trailer (easiest to do) */
    memset(tbuffer, 0, sizeof(tbuffer));
    tp = tbuffer;

    *tp++ = 'T';	/* id3v1 tag */
    *tp++ = 'A';
    *tp++ = 'G';

    strcpy(tp, "Unknown title");
    tp += 30;
    strcpy(tp, "Unknown artist");
    tp += 30;
    strcpy(tp, "Unknown album");
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

    *tp++ = 96;	/* big band :-) */

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
    if (success)
	return 0;
    else
	return -1;
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
    NSString *startSong;
    uint64_t startMs = 0;
    uint64_t endMs = 0;
    bool first = true;
    ExportEntry *ep;
    MFANAqStreamReader *reader;
    MFANAqStreamPacket *p;
    uint64_t populateStartMs;
    uint64_t populateEndMs;

    reader = [[MFANAqStreamReader alloc]
		  initWithBuffer: _buffer];
    reader.noWait = true;

    [reader seek: 0  whence: 0];

    while(true) {
	p = [reader read];
	if (p == nil)
	    break;

	// canceled, stop early
	if (_populateCanceled)
	    break;

	if (first) {
	    startMs = endMs = p.startMs;
	    startSong = p.playingSong;
	    first = false;
	    continue;
	}

	if ( [startSong isEqualToString: p.playingSong] ||
	     [p.playingSong length] == 0) {
	    endMs = p.startMs + p.durationMs;
	    continue;
	}

	// new song
	ep = [[ExportEntry alloc] initWithStartTime: startMs/1000.0
						end: endMs/1000.0];
	ep.song = startSong;

	pthread_mutex_lock(&_populateLock);
	_populateSong = startSong;
	[_recordings addObject: ep];
	pthread_mutex_unlock(&_populateLock);

	populateStartMs = _buffer.firstPacketStartMs;
	populateEndMs = _buffer.lastPacketEndMs;

	startSong = p.playingSong;
	startMs = p.startMs;
	endMs = p.startMs + p.durationMs;

	_populatePct = (uint32_t) (100 * (endMs - populateStartMs) /
				   (populateEndMs - populateStartMs));
    }

    pthread_mutex_lock(&_populateLock);
    _populateDone = true;
    pthread_mutex_unlock(&_populateLock);

    pthread_exit(nullptr);
}

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

@end
