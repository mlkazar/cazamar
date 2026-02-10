#import "SearchStation.h"
#import "MFANCGUtil.h"
#import "MFANSocket.h"

#include "radioscan.h"

// View calls callback once the search is done or
// canceled.  It doesn't indicate completion until the
// asynchronous thread completes.
@implementation SearchStation {
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

    self = [super initWithFrame: frame];
    if (self != nil) {
	_signStations = [[NSMutableArray alloc] init];
	searchFrame = frame;
	searchFrame.size.height *= 0.1;
	_rowHeight = 72.0;

	_stationp = nullptr;

	_searchBar = [[UISearchBar alloc] initWithFrame: searchFrame];
	_searchBar.showsCancelButton = YES;
	_searchBar.delegate = self;
	[self addSubview: _searchBar];

	tableFrame = frame;
	tableFrame.origin.y = searchFrame.origin.y + searchFrame.size.height;
	tableFrame.size.height *= 0.9;

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
	[_stationTable setBackgroundColor: [UIColor whiteColor]];
	_stationTable.sectionIndexBackgroundColor = [UIColor clearColor];
	[_stationTable setSeparatorStyle: UITableViewCellSeparatorStyleNone];
	[self addSubview: _stationTable];

	_canceled = NO;
    }

    return self;
}

- (void) searchAsync: (id) junk {
    const char *searchStringp;

    searchStringp = [_queryString cStringUsingEncoding: NSUTF8StringEncoding];

    _queryp = NULL;	/* searchStation will allocate it */
    _scanp->searchStation(std::string(searchStringp), &_queryp);

    _queryDone = YES;

    [NSThread exit];
}

- (void) searchBarCancelButtonClicked:(UISearchBar *)searchBar {
    NSLog(@"search canceled");
    _canceled = YES;
    [_searchBar resignFirstResponder];
    [self removeFromSuperview];
    
    [self doNotify];
}

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
	    if (_queryp->_stations.head() != nullptr) {
		_stationp = _queryp->_stations.head();
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

	    // origin is set by layout code later.
	    [_signStations addObject: newStation];
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

    if (_queryDone) {
	// nothing until done button pressed
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

    cell.detailTextLabel.text = station.shortDescr;
    cell.detailTextLabel.font = [UIFont fontWithName: @"Arial-BoldMT" size: 16];
    cell.detailTextLabel.textColor = [UIColor greenColor];

    if (station.isSelected)
	cell.accessoryType = UITableViewCellAccessoryCheckmark;
    else
	cell.accessoryType = UITableViewCellAccessoryNone;

#if 0
    imageHeight = 8.0 * 12 / 10;
    image = [_popMediaSel imageByIx: realIx size: imageHeight];
    if (image == nil) {
	if (imageHeight != _scaledDefaultImageSize || _scaledDefaultImage == nil) {
	    _scaledDefaultImage = resizeImage(_defaultImage, imageHeight);
	    _scaledDefaultImageSize = imageHeight;
	}
	image = _scaledDefaultImage;
    }


    [[cell imageView] setImage: image];
#else
    if ([station.iconUrl length] == 0)
	[[cell imageView] setImage: _scaledGenericImage];
    else {
	NSURL *imageUrl = [NSURL URLWithString: station.iconUrl];
	NSData *imageData = [[NSData alloc] initWithContentsOfURL: imageUrl];
	UIImage *image = [UIImage imageWithData: imageData];
	UIImage *scaledImage = resizeImage(image, 60);
	[[cell imageView] setImage: scaledImage];
    }
#endif
    /* make cell clear */
    cell.contentView.backgroundColor = [UIColor clearColor];
    cell.backgroundView.backgroundColor = [UIColor clearColor];
    cell.multipleSelectionBackgroundView.backgroundColor = [UIColor clearColor];
    cell.selectedBackgroundView.backgroundColor = [UIColor clearColor];
    cell.backgroundColor = [UIColor clearColor];

    return cell;
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

@end
