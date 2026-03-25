#import "SearchStation.h"
#import "MFANCGUtil.h"
#import "MFANIconButton.h"
#import "MFANSocket.h"
#import "ViewController.h"

#include "radioscan.h"

// View calls callback once the search is done or
// canceled.  It doesn't indicate completion until the
// asynchronous thread completes.
@implementation SearchStation {
    ViewController *_vc;
    UISearchBar *_searchBar;
    UITableView *_stationTable;
    float _rowHeight;
    UIImage *_genericImage;
    UIImage *_scaledGenericImage;
    bool _canceled;

    id _callbackObj;
    SEL _callbackSel;

    // Search state
    RadioScan *_scanp;
    RadioScanQuery *_queryp;
    RadioScanStation *_stationp;
    RadioScanStation::Entry *_stationStreamp;
    NSThread *_searchThread;
    MFANSocketFactory *_factory;
    NSTimer *_queryTimer;
    bool _queryDone;
    NSString *_queryString;

    // The search code fills this in with an array of SignStation
    // objects, which get displayed via the _stationTable.
    NSMutableArray *_signStations;

    MFANIconButton *_cancelButton;
    MFANIconButton *_doneButton;
    UIPickerView *_pickerView;

    // displaying state of running query
    UIAlertController *_alert;

    UIAlertController *_finishedAlert;
    NSTimer *_finishedTimer;

    uint32_t _pickerRow;
}

- (void) doNotify {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    if (_callbackObj != nil) {
	[_callbackObj  performSelector: _callbackSel withObject: _searchBar];
    }
#pragma clang diagnostic pop
}

- (SearchStation *) initWithFrame:(CGRect) frame ViewCont: (ViewController *) vc {
    CGRect searchFrame;
    CGRect tableFrame;

    // TODO: we shouldn't have frame as a parameter -- just confusing.
    self.frame = vc.view.frame;
    frame = vc.view.frame;

    self = [super initWithFrame: frame];
    if (self != nil) {
	// size for search bar and bottom buttons
	float verticalViewSize = frame.size.height * 0.1;
	float searchBarWidthPct = 0.6;

	_vc = vc;
	_signStations = [[NSMutableArray alloc] init];
	searchFrame = frame;
	searchFrame.size.width = frame.size.width * searchBarWidthPct;
	searchFrame.origin.y = vc.topMargin;
	searchFrame.size.height = verticalViewSize;

	NSLog(@"FRAME searchstation %f x %f at %f.%f",
	      self.frame.size.width, self.frame.size.height,
	      self.frame.origin.x, self.frame.origin.y);

	NSLog(@"FRAME searchframe %f x %f at %f.%f",
	      frame.size.width, frame.size.height, frame.origin.x, frame.origin.y);

	CGRect textFrame;
	textFrame = searchFrame;
	textFrame.origin.x = searchFrame.size.width;
	textFrame.size.width = frame.size.width * (1 - searchBarWidthPct);

	self.backgroundColor = vc.backgroundColor;

	// for UITableView
	_rowHeight = 72.0;

	_stationp = nullptr;

	_searchBar = [[UISearchBar alloc] initWithFrame: searchFrame];
	_searchBar.showsCancelButton = NO;
	_searchBar.showsBookmarkButton = NO;
	_searchBar.delegate = self;
	_searchBar.searchBarStyle = UISearchBarStyleMinimal;
	[self addSubview: _searchBar];
	_searchBar. searchTextField.backgroundColor = [UIColor colorWithRed: 0.7
								     green: 0.7
								      blue: 0.7
								     alpha: 1.0];
	_searchBar.barTintColor = vc.backgroundColor;

	_pickerView = [[UIPickerView alloc] initWithFrame: textFrame];
	[self addSubview: _pickerView];
	_pickerView.delegate = self;
	_pickerView.dataSource = self;
	_pickerView.backgroundColor = vc.backgroundColor;
	[_pickerView setValue: [UIColor blackColor] forKey: @"textColor"];

	_pickerRow = 0;

	tableFrame.origin.x = 0;
	tableFrame.origin.y = searchFrame.origin.y + searchFrame.size.height;
	tableFrame.size.width = frame.size.width;
	// use the rest of the vertical space, but reserve one
	// verticalViewSize for buttons at the bottom.
	tableFrame.size.height = (frame.size.height - tableFrame.origin.y
				  - verticalViewSize);

	_genericImage = [UIImage imageNamed: @"radio-icon.png"];
	_scaledGenericImage = resizeImage(_genericImage, 60);

	_factory = new MFANSocketFactory();
	std::string dirPrefix =
	    std::string([fileNameForFile(@"") cStringUsingEncoding: NSUTF8StringEncoding]);

	_scanp = new RadioScan();
	_scanp->init(_factory, dirPrefix);

	_stationTable = [[UITableView alloc] initWithFrame: tableFrame
						     style:UITableViewStylePlain];
	[_stationTable setAllowsMultipleSelection: YES];
	[self addSubview: _stationTable];
	[_stationTable setDataSource: self];
	[_stationTable setDelegate: self];
	[_stationTable setRowHeight: _rowHeight];
	[_stationTable setSectionIndexMinimumDisplayRowCount: 20];
	[_stationTable setBackgroundColor: vc.backgroundColor];
	_stationTable.sectionIndexBackgroundColor = [UIColor clearColor];
	[_stationTable setSeparatorStyle: UITableViewCellSeparatorStyleNone];
	[self addSubview: _stationTable];

	// layout cancel and done buttons
	CGRect cancelFrame;
	CGRect doneFrame;
	cancelFrame.origin.y = frame.size.height - verticalViewSize;
	cancelFrame.size.height = verticalViewSize;
	cancelFrame.size.width = verticalViewSize; // make it square
	// center button 1/3 of way across
	cancelFrame.origin.x = frame.size.width/3 - verticalViewSize/2;

        _cancelButton = [[MFANIconButton alloc]
			    initWithFrame: cancelFrame
				    title: @"Cancel"
				    color: [UIColor colorWithHue: 0.4
						      saturation: 1.0
						      brightness: 0.56
							   alpha: 1.0]
				     file: @"icon-cancel.png"];
        [_cancelButton addCallback: self
		      withAction: @selector(cancelPressed:withData:)];
        [self addSubview: _cancelButton];


	doneFrame = cancelFrame;
	doneFrame.origin.x = frame.size.width*(2.0/3.0) - verticalViewSize/2;

        _doneButton = [[MFANIconButton alloc]
			  initWithFrame: doneFrame
				  title: @"Done"
				  color: [UIColor colorWithHue: 0.4
						    saturation: 1.0
						    brightness: 0.56
							 alpha: 1.0]
				   file: @"icon-done.png"];
        [_doneButton addCallback: self
		      withAction: @selector(donePressed:withData:)];
        [self addSubview: _doneButton];

	_canceled = NO;

	[vc pushTopView: self];
    }

    return self;
}

// This thread creates the query, but the query gets freed on the main
// thread to simplify concurrency control.  Once _queryDone is set,
// the main thread is allowed to do whatever it wants with _queryp,
// like delete it.
- (void) searchAsync: (id) junk {
    const char *searchStringp;
    RadioScan::ScanType scanType = RadioScan::useName;

    searchStringp = [_queryString cStringUsingEncoding: NSUTF8StringEncoding];

    if (_pickerRow == 0) {
	scanType = RadioScan::useName;
    } else if (_pickerRow == 1) {
	scanType = RadioScan::useTag;
    }

    _queryp = new RadioScanQuery();
    _queryp->initSmart(_scanp, std::string(searchStringp));
    _scanp->searchStation(_queryp, scanType);

    _queryDone = YES;

    [NSThread exit];
}

// cancel button pressed before starting or after completion of search
// *NOT* code that runs when search is aborted.
- (void) cancelPressed: (id) sender withData: (NSNumber *) number {
    NSLog(@"search canceled");
    _canceled = YES;
    if (_queryTimer != nil) {
	[_queryTimer invalidate];
    }
    [_searchBar resignFirstResponder];
    // [self removeFromSuperview];
    [_vc popTopView];
    
    [self doNotify];
}

// All done with the search process.
- (void) donePressed: (id) sender withData: (NSNumber *) number {
    NSLog(@"search complete");
    if (_queryTimer != nil) {
	[_queryTimer invalidate];
	_queryTimer = nil;
    }
    _canceled = NO;
    [_searchBar resignFirstResponder];
    [_vc popTopView];
    
    [self doNotify];
}

- (void) displayQueryStatus: (RadioScanQuery *) queryp {
    if (_alert == nil) {
	_alert = [UIAlertController
				   alertControllerWithTitle: @"RadioStar"
						    message: @"Searching"
					     preferredStyle: UIAlertControllerStyleAlert];

	UIAlertAction *action = [UIAlertAction
				    actionWithTitle:@"Stop search"
					      style: UIAlertActionStyleDefault
					    handler:^(UIAlertAction *act) {
		NSLog(@"stopping search");
		if (self->_queryp)
		    self->_queryp->abort();
	    }];
	[_alert addAction: action];
	[_vc presentViewController:_alert animated:YES completion: nil];
    }

    if (queryp != nullptr) {
	std::string status = queryp->getStatus();
	_alert.message = [NSString stringWithUTF8String: status.c_str()];
    } else {
	_alert.message = @"Search complete";
    }
}

// Call this after the query monitor has completed waiting for the
// RadioScanQuery to terminate, to cleanup any other state created by
// the queryMonitor.
- (void) cleanupQueryMonitor {
    [_alert dismissViewControllerAnimated: YES completion: nil];
    _alert = nil;

    if (_queryTimer != nil) {
	[_queryTimer invalidate];
	_queryTimer = nil;
    }

    if (_queryp) {
	delete _queryp;
	_queryp = nullptr;
    }

    [self displayNextSteps];
}

- (void) displayNextSteps {
    _finishedAlert = [UIAlertController
				   alertControllerWithTitle: @"RadioStar"
						    message: @"Next, select stations to add\n"
			 @"and press done"
					     preferredStyle: UIAlertControllerStyleAlert];

    UIAlertAction *action = [UIAlertAction actionWithTitle:@"OK"
                                                     style: UIAlertActionStyleDefault
                                                   handler:^(UIAlertAction *act) {
	    self->_finishedAlert = nil;
	    if (self->_finishedTimer != nil) {
		[self->_finishedTimer invalidate];
		self->_finishedTimer = nil;
	    }
	}];
    [_finishedAlert addAction: action];

    _finishedTimer = [NSTimer scheduledTimerWithTimeInterval: 5.0
						      target:self
						    selector:@selector(finishedDismiss:)
						    userInfo:nil
						     repeats: NO];
    [_vc presentViewController: _finishedAlert animated:YES completion: nil];
}

- (void) finishedDismiss: (id) junk {
    if (_finishedAlert) {
	[_finishedAlert dismissViewControllerAnimated: YES completion:nil];
	_finishedAlert = nil;
    }
    _finishedTimer = nil;
}

// This does more than monitor the query -- it is responsible for
// aborting the query and cleaning up the RadioScanQuery object.  It
// implicitly doesn't terminate before the query's termination is
// acknowledged, which is important because this code doesn't handle
// more than one outstanding query at a time (which we could fix if
// necessary).
- (void) queryMonitor: (id) junk {
    SignStation *newStation;
    bool doAdd = NO;
    bool didAny = NO;

    _queryTimer = nil;

    NSLog(@"querymonitor new invocation self=%p", self);
    // At the top of this loop, if _stationp is non-null, it is the
    // last station successfully added to the _signStations array.
    // Otherwise, no stations have been added yet.  Note that we don't
    // start the add between timer activations, instead _stationp keeps
    // track of which stations we've already added, so we can just keep
    // adding the new stations.
    while(true) {
	doAdd = NO;
	if (!_stationp) {
	    if (_queryp->_goodStations.head() != nullptr) {
		_stationp = _queryp->_goodStations.head();
		doAdd = YES;
	    }
	} else {
	    if (_stationp->_dqNextp != nullptr) {
		_stationp = _stationp->_dqNextp;
		doAdd = YES;
	    }
	}

	// if doAdd is true, we have a new station to add to the return
	// array.  Reset the timer if the query isn't done, otherwise do
	// the notify operation and terminate the polling.
	if (doAdd) {
	    didAny = YES; 
	    newStation = [[SignStation alloc] init];
	    newStation.stationName = [NSString stringWithUTF8String: _stationp->_stationName.c_str()];
	    newStation.shortDescr = [NSString stringWithUTF8String:
						  _stationp->_stationShortDescr.c_str()];
	    newStation.iconUrl = [NSString stringWithUTF8String: _stationp->_iconUrl.c_str()];
	    SignCoord rowColumn = {0, 0};
	    [newStation setRowColumn: rowColumn];
	    newStation.isPlaying = NO;
	    newStation.isRecording = NO;

	    // TODO: find best stream to use
	    RadioScanStation::Entry *ep;
	    RadioScanStation::Entry *bestEp = nullptr;
	    uint32_t bestRate = 0;
	    for(ep = _stationp->_entries.head(); ep; ep=ep->_dqNextp) {
		if (ep->_streamRateKb >= bestRate) {
		    bestRate = ep->_streamRateKb;
		    bestEp = ep;
		}
	    }
	    if (bestEp != nullptr) {
		newStation.streamUrl = [NSString stringWithUTF8String:
						     bestEp->_streamUrl.c_str()];
		newStation.streamRateKb = bestEp->_streamRateKb;
		newStation.streamType = [NSString stringWithUTF8String:
						      bestEp->_streamType.c_str()];
	    }

	    // origin is set by layout code later.
	    if (bestEp != nil) {
		[_signStations addObject: newStation];
	    } else {
		NSLog(@"internal error -- returned station %@ has no streams",
		      newStation.stationName);
	    }
	    NSLog(@"querymonitor added station %@ %@",
		  newStation.stationName, newStation.shortDescr);
	} else {
	    // Nothing new to return.
	    NSLog(@"querymonitor breaking from loop");
	    break;
	}
    }

    // trigger reload of uitable's visible parts.
    [_stationTable reloadData];

    [self displayQueryStatus: _queryp];

    if (_queryDone) {
	// nothing until done button pressed
	[self cleanupQueryMonitor];
    } else {
	_queryTimer = [NSTimer scheduledTimerWithTimeInterval: 0.5
						       target:self
						     selector:@selector(queryMonitor:)
						     userInfo:nil
						      repeats: NO];
	NSLog(@"querymonitor restarting timer");
    }
}

- (void) searchBarSearchButtonClicked:(UISearchBar *)searchBar {
    [_searchBar resignFirstResponder];
    _canceled = NO;
    _queryString = _searchBar.text;
    NSLog(@"search test is %@", _queryString);

    _queryDone = NO;
    _searchThread = [[NSThread alloc] initWithTarget: self
					    selector: @selector(searchAsync:)
					      object: nil];
    [_searchThread start];

    // The RadioScan system keeps appending to a stations list; we're
    // going to poll it.  TODO: see if NSCondition sleeps in such a
    // way that UI interface objects keep working.  If so, perhaps we
    // can wire up RadioScan to upcall us when a new station is added
    // to the list.

    _queryTimer = [NSTimer scheduledTimerWithTimeInterval: 0.5
						   target:self
						 selector:@selector(queryMonitor:)
						 userInfo:nil
						  repeats: NO];
}

- (void) setCallback: (id) object WithSel: (SEL) selector {
    _callbackObj = object;
    _callbackSel = selector;
}

- (BOOL)tableView:(UITableView *)tableView 
canEditRowAtIndexPath:(NSIndexPath *)indexPath {
    long row;
    row = [indexPath row];
    NSLog(@"canEditRow %d", (int) row);
    return YES;
}

- (void) tableView: (UITableView *) tview
commitEditingStyle: (UITableViewCellEditingStyle) style
 forRowAtIndexPath: (NSIndexPath *) path
{
    long row;

    if (style == UITableViewCellEditingStyleDelete) {
	row = [path row];
	NSLog(@"**remove item at row %d", (int) row);
    }
}

- (NSInteger) numberOfSectionsInTableView:(UITableView *) tview
{
    return 1;
}

- (NSInteger) tableView: (UITableView *)tview numberOfRowsInSection: (NSInteger) section
{
    return [_signStations count];
 }

- (NSArray *) sectionIndexTitlesForTableView:(UITableView *) tview
{
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
    SignStation *station;

    /* lookup section and row within section, all zero-based.  We
     * compute ix as the total depth into the combined array.  The
     * variable section gives the # of complete sections we have.
     */
    section = (int) [path section];
    row = (int) [path row];

    station = _signStations[row];

    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
				     reuseIdentifier: nil];
    backgroundView = [[UIView alloc] init];
    backgroundView.backgroundColor = [UIColor clearColor];
    cell.multipleSelectionBackgroundView = backgroundView;
    cell.textLabel.text = station.stationName;
    cell.textLabel.textColor = [UIColor blueColor];
    cell.textLabel.font = [UIFont fontWithName: @"Arial-BoldMT" size: 32];
    cell.textLabel.adjustsFontSizeToFitWidth = YES;

    cell.detailTextLabel.text = [NSString stringWithFormat: @"%@[%dkb %@]",
					  station.shortDescr,
					  station.streamRateKb,
					  station.streamType];
    cell.detailTextLabel.font = [UIFont fontWithName: @"Arial-BoldMT" size: 16];
    cell.detailTextLabel.textColor = [UIColor colorWithRed: 0.0
						     green: 0.5
						      blue: 0.0
						     alpha: 1.0];
    cell.detailTextLabel.adjustsFontSizeToFitWidth = YES;

    if (station.isSelected)
	cell.accessoryType = UITableViewCellAccessoryCheckmark;
    else
	cell.accessoryType = UITableViewCellAccessoryNone;

    [station setIconImageFromUrl: YES];
    UIImage *scaledImage = resizeImage(station.iconImage, 60);

    [[cell imageView] setImage: scaledImage];



    /* make cell clear */
    cell.contentView.backgroundColor = [UIColor clearColor];
    cell.backgroundView.backgroundColor = [UIColor clearColor];
    cell.multipleSelectionBackgroundView.backgroundColor = [UIColor clearColor];
    cell.selectedBackgroundView.backgroundColor = [UIColor clearColor];
    cell.backgroundColor = [UIColor clearColor];

    return cell;
}

- (UIImage *) imageFromText: (NSString *) text Size: (CGSize) size {
    // 1. Ensure the UIKit context is pushed (necessary if not in drawRect:)
    //    If you are in a UIView's drawRect:, this is already handled.

    size = [text sizeWithFont: [UIFont systemFontOfSize: size.height]];
    size.width *= 1.2;

    UIGraphicsBeginImageContext(size);

    // 2. Define the text and attributes
    UIFont *font = [UIFont systemFontOfSize: size.height];
    UIColor *textColor = [UIColor blackColor];

    NSMutableParagraphStyle *paragraphStyle = [[NSMutableParagraphStyle alloc] init];
    paragraphStyle.alignment = NSTextAlignmentCenter; // Example alignment

    NSDictionary *attributes = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: textColor,
        NSParagraphStyleAttributeName: paragraphStyle
    };

    // 3. Define the drawing rectangle
    CGRect textRect = CGRectMake(0.0, 0.0, size.width, size.height);

    // 4. Draw the string
    [text drawInRect:textRect withAttributes:attributes]; // Or use drawAtPoint for single line

    UIImage *resultImage = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();

    return resultImage;
}

- (void) tableView: (UITableView *) tview didSelectRowAtIndexPath: (NSIndexPath *) path
{
    long row;

    row = [path row];
    SignStation *station = _signStations[row];
    station.isSelected = !station.isSelected;
    [_stationTable reloadRowsAtIndexPaths:
		 [NSArray arrayWithObject: path]
			 withRowAnimation: UITableViewRowAnimationAutomatic];
}

- (NSInteger) numberOfComponentsInPickerView:(UIPickerView *) pickerView {
     return 1;
}

- (NSInteger) pickerView:(UIPickerView *) pickerView
 numberOfRowsInComponent:(NSInteger) comp {
    return 2;
}

- (NSString *) pickerView:(UIPickerView *) pickerView
	      titleForRow:(NSInteger)row
	     forComponent:(NSInteger)component {
    if (row == 0)
	return @"By name";
    else if (row == 1)
	return @"By keywords";
    else
	return @"BOOP!";
}

- (void)pickerView:(UIPickerView *)thePickerView 
      didSelectRow:(NSInteger)row 
       inComponent:(NSInteger)component {

    NSLog(@"in picker select %d", (int) row);
}

@end

@implementation SearchStationResults {
    SearchStation *_searchStation;
    uint32_t _ix;
}

- (SearchStationResults *) initWithSearchStation: (SearchStation *) search {
    self = [super init];
    if (self != nil) {
	_ix = 0;
	_searchStation = search;
    }

    return self;
}

// returns nil when out of entries
- (SignStation *) getNext {
    NSMutableArray *stations = _searchStation.signStations;
    if (_ix >= [stations count])
	return nil;
    else
	return stations[_ix++];
}
@end
