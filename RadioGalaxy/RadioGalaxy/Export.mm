#import "Export.h"

#import "ExportSlider.h"
#import "HelpLabel.h"
#import "MFANAqStreamBuffer.h"
#import "MFANCGUtil.h"
#import "MFANIconButton.h"
#import "MFANWarn.h"
#import "SignView.h"
#import "ViewController.h"

#include "osp.h"

@implementation Export {
    ViewController *_vc;
    SignStation *_station;
    MFANAqStreamBuffer *_buffer;

    ExportSlider *_startSlider;
    ExportSlider *_endSlider;
    UITableView *_songTable;

    // tags
    UILabel *_startLabel;
    UILabel *_endLabel;
    UILabel *_populateLabel;
    
    // useful buttons
    MFANIconButton *_exportButton;
    MFANIconButton *_cancelButton;
    MFANIconButton *_populateSwitch;
    MFANIconButton *_doneButton;

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
    CGRect labelFrame;
    CGRect buttonFrame;
    CGRect startSliderFrame;
    CGRect endSliderFrame;
    CGRect startLabelFrame;
    CGRect endLabelFrame;
    CGRect populateTextFrame;
    CGRect populateSwitchFrame;
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
	
	UIColor *labelColor = [UIColor colorWithRed: 0.9
					      green: 0.9
					       blue: 0.9
					      alpha: 1.0];
	// layout the station name, descr, URL, rate+type
	float boxHeight = frame.size.height * 0.06;
	float boxWidth = frame.size.width * 0.60;
	float labelHeight = boxHeight;
	float labelWidth = frame.size.width * 0.35;
	float okButtonWidth = labelHeight;

	// 60% for the table
	// 6% for start slider
	// 6% for end slider
	// 6% for populate button
	// spare space
	// 6% for the cancel / export buttons

	// indent things so that we center the label and text box in
	//the frame.
	float indent = (frame.size.width - labelWidth - boxWidth) / 2;

	float viewOffset = 0.0;
	float viewHeight = 0.6 * frame.size.height;

	tableFrame = frame;
	tableFrame.size.height = viewHeight;
	tableFrame.origin.y = viewOffset;

	_songTable = [[UITableView alloc] initWithFrame: tableFrame
						  style:UITableViewStylePlain];
	[_songTable setAllowsMultipleSelection: YES];
	[_songTable setDataSource: self];
	[_songTable setDelegate: self];
	[_songTable setRowHeight: labelHeight];
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
	startSliderFrame.origin.x = 0.25 * frame.size.width;
	startSliderFrame.size.width = 0.75 * frame.size.width;

	_startSlider = [[ExportSlider alloc] initWithFrame: startSliderFrame
						    buffer: _buffer
						     apply: ^(float value) {
		[self playFrom: value];
	    }
						  viewCont: _vc];
	[self addSubview: _startSlider];

	startLabelFrame = startSliderFrame;
	startLabelFrame.origin.x = 0;
	startLabelFrame.size.width = 0.25 * frame.size.width;
	_startLabel = [[UILabel alloc] initWithFrame: startLabelFrame];
	_startLabel.backgroundColor = [UIColor whiteColor];
	_startLabel.text = @"Start pos.";
	_startLabel.textColor = [UIColor blackColor];
	_startLabel.textAlignment = NSTextAlignmentLeft;
	[self addSubview: _startLabel];


	viewOffset += viewHeight;
	viewHeight = labelHeight;
	endSliderFrame = frame;
	endSliderFrame.origin.y = viewOffset;
	endSliderFrame.size.height = viewHeight;

	float textLabelWidth = frame.size.width * 0.33;

	// and shrink to allow label
	endSliderFrame.origin.x = 0.25 * frame.size.width;
	endSliderFrame.size.width = 0.75 * frame.size.width;

	_endSlider = [[ExportSlider alloc] initWithFrame: endSliderFrame
						  buffer: _buffer
						   apply: ^(float value) {
		[self playTo: value];
	    }
						viewCont: _vc];
	[self addSubview: _endSlider];

	endLabelFrame = endSliderFrame;
	endLabelFrame.origin.x = 0;
	endLabelFrame.size.width = 0.25 * frame.size.width;
	_endLabel = [[UILabel alloc] initWithFrame: endLabelFrame];
	_endLabel.backgroundColor = [UIColor whiteColor];
	_endLabel.text = @"End pos.";
	_endLabel.textColor = [UIColor blackColor];
	_endLabel.textAlignment = NSTextAlignmentLeft;
	[self addSubview: _endLabel];

	// add populate button
	viewOffset += viewHeight;
	viewHeight = labelHeight;

	// Populate (from song list) contents button
	populateTextFrame.origin.x = indent;
	populateTextFrame.origin.y = viewOffset;
	populateTextFrame.size.height = labelHeight;
	populateTextFrame.size.width = textLabelWidth;
	HelpLabel *populateHelpLabel = [[HelpLabel alloc]
					   initWithFrame: populateTextFrame
						  target: self
						selector: @selector(populateHelp:)];
	[populateHelpLabel setTitle: @"Populate streamed buffer"
			forState: UIControlStateNormal];

	[self addSubview: populateHelpLabel];

	populateSwitchFrame = populateTextFrame;
	populateSwitchFrame.origin.x = populateTextFrame.origin.x + populateTextFrame.size.width;
	populateSwitchFrame.size.width = textLabelWidth;
	_populateSwitch = [[MFANIconButton alloc] initWithFrame: populateSwitchFrame
							  title: @"Populate from songs"
							  color: [UIColor clearColor]
							   file: @"icon-button.png"];
	[_populateSwitch addCallback: self
		       withAction: @selector(populatePressed:)];
	[self addSubview: _populateSwitch];

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

- (void) playTo: (float) value {
    NSLog(@"playto %f", value);
}

- (void) playFrom: (float) value {
    NSLog(@"playfrom %f", value);
}

- (void) donePressed: (id) junk1 {
    // [self doNotify];
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
    cell.textLabel.font = [UIFont fontWithName: @"Arial-BoldMT" size: 32];
    cell.textLabel.adjustsFontSizeToFitWidth = YES;

    cell.detailTextLabel.text = @"Detailed info";
    cell.detailTextLabel.font = [UIFont fontWithName: @"Arial-BoldMT" size: 16];
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

- (void) populatePressed: (id) junk {
    NSLog(@"write populate from file code");
}

- (void) deactivateTopView {
    [_startSlider shutdown];
    _startSlider = nil;
    [_endSlider shutdown];
    _endSlider = nil;
    return;
}

@end
