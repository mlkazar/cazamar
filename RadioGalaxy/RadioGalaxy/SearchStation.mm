#import "SearchStation.h"

#import "HelpView.h"
#import "MFANAqStream.h"
#import "MFANAqStreamBuffer.h"
#import "MFANCGUtil.h"
#import "MFANCoreButton.h"
#import "MFANIconButton.h"
#import "MFANSocket.h"
#import "MFANStreamPlayer.h"
#import "Settings.h"
#import "ViewController.h"

#include "radioscan.h"

// View calls callback once the search is done or
// canceled.  It doesn't indicate completion until the
// asynchronous thread completes.
@implementation SearchStation {
    ViewController *_vc;
    UISearchBar *_searchBar;
    UITableView *_stationTable;
    UITextView *_textView;
    float _rowHeight;
    UIImage *_genericImage;
    UIImage *_scaledGenericImage;
    bool _canceled;

    id _callbackObj;
    SEL _callbackSel;

    // Search state
    RadioScan *_scanp;
    RadioScanQuery *_queryp;
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
    MFANCoreButton *_helpButton;
    UIPickerView *_pickerView;

    // displaying state of running query
    UIAlertController *_alert;

    UIAlertController *_finishedAlert;
    NSTimer *_finishedTimer;

    uint32_t _pickerRow;

    uint32_t _lastUpdateVersion;

    bool _didNotify;

    uint32_t _maxSearchReturn;

    // for a short period after 
    bool _suppressReloads;
    NSTimer *_suppressTimer;

    // these are non-null if we're playing a sample
    MFANAqStreamBuffer *_sampleStreamBuffer;
    MFANAqStream *_sampleStream;
    MFANStreamPlayer *_samplePlayer;
    SignStation *_sampleStation;
    Settings *_settings;
}

- (void) doNotify {
    [self stopSampler];
    _didNotify = true;
    if (_callbackObj != nil) {
	[_callbackObj  performSelectorOnMainThread: _callbackSel
					withObject: _searchBar
				     waitUntilDone: true];
    }
}

- (SearchStation *) initWithFrame:(CGRect) frame ViewCont: (ViewController *) vc {
    CGRect searchFrame;
    CGRect tableFrame;

    // TODO: we shouldn't have frame as a parameter -- just confusing.
    self.frame = vc.activeFrame;
    frame = vc.activeFrame;

    self = [super initWithFrame: frame];
    if (self != nil) {
	// size for search bar and bottom buttons
	float verticalViewSize = vc.activeFrame.size.height * 0.1;

	_vc = vc;

	searchFrame = vc.activeFrame;
	searchFrame.origin.y = 0;
	searchFrame.size.width = frame.size.width;
	searchFrame.size.height = verticalViewSize;

	CGRect pickerFrame;
	pickerFrame = searchFrame;
	pickerFrame.origin.y += verticalViewSize;
	pickerFrame.size.width = frame.size.width;

	CGRect textFrame;
	textFrame = pickerFrame;
	textFrame.origin.y += verticalViewSize;
	textFrame.size.width = frame.size.width;

	// for UITableView
	_rowHeight = 72.0;

	_searchBar = [[UISearchBar alloc] initWithFrame: searchFrame];
	_searchBar.showsCancelButton = NO;
	_searchBar.showsBookmarkButton = NO;
	_searchBar.delegate = self;
	_searchBar.searchBarStyle = UISearchBarStyleMinimal;
	_searchBar.showsCancelButton = YES;
	[self addSubview: _searchBar];
	_searchBar. searchTextField.backgroundColor = [UIColor colorWithRed: 0.7
								     green: 0.7
								      blue: 0.7
								     alpha: 1.0];
	_searchBar.barTintColor = [UIColor whiteColor];

	_pickerView = [[UIPickerView alloc] initWithFrame: pickerFrame];
	[self addSubview: _pickerView];
	_pickerView.delegate = self;
	_pickerView.dataSource = self;
	_pickerView.backgroundColor = [UIColor whiteColor];
	[_pickerView setValue: [UIColor blackColor] forKey: @"textColor"];

	_pickerRow = 0;

	_textView = [[UITextView alloc] initWithFrame: textFrame];
	[self addSubview: _textView];

	tableFrame.origin.x = 0;
	tableFrame.origin.y = textFrame.origin.y + textFrame.size.height;
	tableFrame.size.width = frame.size.width;
	// use the rest of the vertical space, but reserve one
	// verticalViewSize for buttons at the bottom.
	tableFrame.size.height = (vc.activeFrame.size.height - tableFrame.origin.y
				  - verticalViewSize);

	_genericImage = [UIImage imageNamed: @"radio-icon.png"];
	_scaledGenericImage = resizeImage(_genericImage, 60);

	_factory = new MFANSocketFactory();
	std::string dirPrefix =
	    std::string([fileNameForFile(@"") cStringUsingEncoding: NSUTF8StringEncoding]);

	_scanp = new RadioScan();
	_scanp->init(_factory, dirPrefix);
	_scanp->setStrictLicense();

	_stationTable = [[UITableView alloc] initWithFrame: tableFrame
						     style:UITableViewStylePlain];
	[_stationTable setAllowsMultipleSelection: YES];
	[_stationTable setDataSource: self];
	[_stationTable setDelegate: self];
	[_stationTable setRowHeight: _rowHeight];
	[_stationTable setSectionIndexMinimumDisplayRowCount: 20];
	[_stationTable setBackgroundColor: [UIColor whiteColor]];
	_stationTable.sectionIndexBackgroundColor = [UIColor clearColor];
	[_stationTable setSeparatorStyle: UITableViewCellSeparatorStyleNone];
	[self addSubview: _stationTable];

	// layout cancel and done buttons
	CGRect cancelFrame;
	CGRect doneFrame;
	CGRect helpFrame;
	cancelFrame.origin.y = vc.activeFrame.size.height - verticalViewSize;
	cancelFrame.size.height = verticalViewSize;
	cancelFrame.size.width = verticalViewSize; // make it square
	// center button 1/3 of way across
	cancelFrame.origin.x = frame.size.width/4.0 - verticalViewSize/2;

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

	helpFrame = cancelFrame;
	helpFrame.origin.x = vc.activeFrame.size.width*(2.0/4.0) - verticalViewSize/2;
	_helpButton = [[MFANCoreButton alloc]
			      initWithFrame: helpFrame
				      title: @"Circle"
				      color: [UIColor blackColor]
			    backgroundColor: [UIColor clearColor]];
	[_helpButton addCallback: self withAction:@selector(helpPressed:withData:)];
	[_helpButton setClearText: @"?"];
	[self addSubview: _helpButton];

	doneFrame = cancelFrame;
	doneFrame.origin.x = vc.activeFrame.size.width*(3.0/4.0) - verticalViewSize/2;

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
	_didNotify = false;
	_suppressReloads = false;

	_settings = (Settings *) vc.settings;
	_maxSearchReturn = _settings.maxSearchReturn;

	[vc pushTopView: self];

	[self setBackgroundColor: [UIColor whiteColor]];
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
	scanType = RadioScan::useTag;
    } else if (_pickerRow == 1) {
	scanType = RadioScan::useName;
    }

    _queryp = new RadioScanQuery();
    _queryp->initSmart(_scanp, std::string(searchStringp));
    _queryp->setMaxReturnCount(_settings.maxSearchReturn);
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

- (void) helpPressed: (id) sender withData: (NSNumber *) number {
    (void) [[HelpView alloc] initWithFile: @"help-search" viewCont: _vc];
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
    if (queryp != nullptr) {
	std::string status = queryp->getStatus();
	_textView.text = [NSString stringWithUTF8String: status.c_str()];
    } else {
	_textView.text = @"Search complete";
    }
}

// Call this after the query monitor has completed waiting for the
// RadioScanQuery to terminate, to cleanup any other state created by
// the queryMonitor.
- (void) cleanupQueryMonitor {
    uint32_t actualCount;

    if (_alert != nil) {
	[_alert dismissViewControllerAnimated: YES completion: nil];
	_alert = nil;
    }

    if (_queryTimer != nil) {
	[_queryTimer invalidate];
	_queryTimer = nil;
    }

    actualCount = (uint32_t) [_signStations count];

    [self cleanupFailedStations];

    if (_queryp) {
	delete _queryp;
	_queryp = nullptr;
    }

    [self displayNextSteps: actualCount];
}

- (void) cleanupFailedStations {
    uint64_t stationCount = [_signStations count];
    SignStation *station;
    NSMutableArray *cleanStations = [[NSMutableArray alloc] init];
    for(uint64_t i=0;i<stationCount;i++) {
	station = _signStations[i];
	if (station.verified && station.verifiedWorking)
	    [cleanStations addObject: station];
    }
    _signStations = cleanStations;

    [_stationTable reloadData];
}

- (void) displayNextSteps: (uint32_t) actualCount {
    // don't do this if the user already moved on.
    if (_didNotify)
	return;

    NSString *message;

    message = @"Next, select stations to add and press 'done.  "
	@"Swipe left to sample a station.\n";

    if (actualCount > _maxSearchReturn) {
	message = [message stringByAppendingString:
			[NSString stringWithFormat: @"[%d stations found; randomly trimmed to %d]",
				  actualCount, _maxSearchReturn]];
    }
    _finishedAlert = [UIAlertController
				   alertControllerWithTitle: @"RadioStar"
						    message: message
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

    _finishedTimer = [NSTimer scheduledTimerWithTimeInterval: 10.0
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
    bool doAdd = NO;

    NSLog(@"QM new invocation self=%p ", self);

    _queryTimer = nil;
    if (!_queryp->_verifying) {
	// TODO: figure out where to put the status, returned as a
	// std::string from _queryp->getStatus()
	[self displayQueryStatus: _queryp];
	_queryTimer = [NSTimer scheduledTimerWithTimeInterval: 0.5
						       target:self
						     selector:@selector(queryMonitor:)
						     userInfo:nil
						      repeats: NO];
	return;
    }

    // At the top of this loop, if _stationp is non-null, it is the
    // last station successfully added to the _signStations array.
    // Otherwise, no stations have been added yet.  Note that we don't
    // start the add between timer activations, instead _stationp keeps
    // track of which stations we've already added, so we can just keep
    // adding the new stations.
    const uint32_t kFlagAdded = 1;
    RadioScanStation *scanStationp;
    SignStation *station;
    uint32_t ix;	// index into _signStations output array
    bool queryDoneAtStart = _queryDone;

    uint32_t nextUpdateVersion = _queryp->getUpdateVersion();
    uint32_t prevUpdateVersion = _lastUpdateVersion;

    bool didAny = false;
    for( ix=0, scanStationp = _queryp->_goodStations.head();
	 scanStationp != nullptr;
	 ++ix, scanStationp = scanStationp->_dqNextp) {
	// the query's update version is the next version to be
	// allocated, so all already existing entries have properly
	// smaller update versions.  So if we perform a scan of the
	// list when the update version is X (or starts at X and only
	// increases), we will process all entries with update
	// versions < X.  On a later pass, we can skip those entries.
	if (scanStationp->getUpdateVersion() < prevUpdateVersion) {
	    // we've already processed this element; it's already in _signStations
	    continue;
	}

	didAny = true;

	// if doAdd is true, we have a new station to add to the return
	// array.
	doAdd = !(scanStationp->_userFlags & kFlagAdded);
	if (doAdd) {
	    // origin is set by layout code later.
	    station = [[SignStation alloc] initWithFileId: ~0U];
	    [_signStations addObject: station];
	    scanStationp->_userFlags |= kFlagAdded;
	} else {
	    station = _signStations[ix];
	    // Nothing new to return.
	}

	// update existing station
	station.stationName =
	    [NSString stringWithUTF8String: scanStationp->_stationName.c_str()];
	station.shortDescr = [NSString stringWithUTF8String:
					   scanStationp->_stationShortDescr.c_str()];
	station.iconUrl = [NSString stringWithUTF8String: scanStationp->_iconUrl.c_str()];
	SignCoord rowColumn = {0, 0};		// filled in by 
	[station setRowColumn: rowColumn];
	[station setIconImageFromUrl: NO];
	station.verified = scanStationp->_verified;
	station.verifiedWorking = scanStationp->_verifiedWorking;

	// Find the best stream to use
	RadioScanStation::Entry *ep;
	RadioScanStation::Entry *bestEp = nullptr;
	uint32_t bestRate = 0;
	for(ep = scanStationp->_entries.head(); ep; ep=ep->_dqNextp) {
	    if (ep->_streamRateKb >= bestRate) {
		bestRate = ep->_streamRateKb;
		bestEp = ep;
	    }
	}
	if (bestEp != nullptr) {
	    station.streamUrl = [NSString stringWithUTF8String:
						 bestEp->_streamUrl.c_str()];
	    station.streamRateKb = bestEp->_streamRateKb;
	    station.streamType = [NSString stringWithUTF8String:
						  bestEp->_streamType.c_str()];
	}
    } // for loop over all radioscan stations
    _lastUpdateVersion = nextUpdateVersion;

    // trigger reload of uitable's visible parts.
    if (didAny && !_suppressReloads)
	[_stationTable reloadData];

    [self displayQueryStatus: _queryp];

    if (queryDoneAtStart) {
	// nothing until done button pressed
	NSLog(@"QM All done");
	[self cleanupQueryMonitor];
    } else {
	_queryTimer = [NSTimer scheduledTimerWithTimeInterval: 0.5
						       target:self
						     selector:@selector(queryMonitor:)
						     userInfo:nil
						      repeats: NO];
    }
}

- (void) searchBarCancelButtonClicked: (UISearchBar *)searchBar {
    [searchBar resignFirstResponder];
}

- (void) searchBarSearchButtonClicked:(UISearchBar *)searchBar {
    [_searchBar resignFirstResponder];
    _canceled = NO;
    _queryString = _searchBar.text;
    NSLog(@"search text is %@", _queryString);

    _signStations = [[NSMutableArray alloc] init];

    _queryDone = NO;
    _lastUpdateVersion = 0;
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
    if (!station.verified)
	cell.textLabel.textColor = [UIColor blueColor];
    else if (station.verifiedWorking)
	cell.textLabel.textColor = [UIColor greenColor];
    else
	cell.textLabel.textColor = [UIColor redColor];
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

    // make sure we've done a URL load
    [station tryLoadFromUrl];

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

- (UISwipeActionsConfiguration *) tableView: (UITableView *) tview
trailingSwipeActionsConfigurationForRowAtIndexPath: (NSIndexPath *) path
{
    long row = [path row];
    SignStation *station = _signStations[row];

    NSString *playString;

    if (station == _sampleStation) {
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
		[self runSampler: station];
		complete(true);
	    }];
    playAction.backgroundColor = [UIColor blueColor];

    _suppressReloads = true;
    if (_suppressTimer == nil) {
	_suppressTimer = [NSTimer scheduledTimerWithTimeInterval: 2.5
							  target:self
							selector:@selector(suppressCanceled:)
							userInfo:nil
							 repeats: NO];
    }

    return [UISwipeActionsConfiguration configurationWithActions: @[playAction]];
}

- (void) suppressCanceled: (id) junk {
    _suppressTimer = nil;
    _suppressReloads = false;
}

- (void) runSampler: (SignStation *) station {
    NSLog(@"IN RUNAMPLER");
    SignStation *origStation = _sampleStation;

    // always stop current station being sampled
    if (_samplePlayer != nil) {
	[self stopSampler];
    }

    // if we selected a different station to sample, or if there was
    // no station being sampled, start sampling now.
    if (origStation != station) {
	// start playing
	_sampleStreamBuffer = [[MFANAqStreamBuffer alloc] initWithFileId: 10000];
	_sampleStream = [[MFANAqStream alloc] initWithUrl: station.streamUrl
						   buffer:_sampleStreamBuffer
						 viewCont: _vc];
	_samplePlayer = [[MFANStreamPlayer alloc] initWithStream: _sampleStream ms: 0];
	_sampleStation = station;
    }
}

- (void) stopSampler {
    if (_samplePlayer != nil) {
	[_samplePlayer shutdown];
	_samplePlayer = nil;
	[_sampleStream shutdown];
	_sampleStream = nil;
	[_sampleStreamBuffer shutdown];
	_sampleStreamBuffer = nil;
    }
}

- (UIImage *) imageFromText: (NSString *) text Size: (CGSize) size {
    // 1. Ensure the UIKit context is pushed (necessary if not in drawRect:)
    //    If you are in a UIView's drawRect:, this is already handled.

    size = [text sizeWithAttributes:
		     @{NSFontAttributeName: [UIFont systemFontOfSize: size.height]}];
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
    if (row == 1)
	return @"By name";
    else if (row == 0)
	return @"By keywords";
    else
	return @"BOOP!";
}

- (void)pickerView:(UIPickerView *)thePickerView 
      didSelectRow:(NSInteger)row 
       inComponent:(NSInteger)component {
    _pickerRow = (int) row;
    NSLog(@"in picker select %d", (int) row);
}

- (void) activateTopView {
    return;
}

- (void) deactivateTopView {
    return;
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
