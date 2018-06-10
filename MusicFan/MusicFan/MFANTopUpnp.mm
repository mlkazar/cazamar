//
//  MFANTopUpnp.m
//  MusicFan
//
//  Created by Michael Kazar on 4/27/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANTopUpnp.h"
#import "MFANIconButton.h"
#import "MFANViewController.h"
#import "MFANSetList.h"
#import "MFANPlayContext.h"
#import "MFANIndicator.h"
#import "MFANCGUtil.h"
#import "MFANTopSettings.h"
#import "MFANWarn.h"
#import "MFANPopHelp.h"
#import "MFANPopStatus.h"
#import "MFANDownload.h"

#include "xgml.h"
#include "upnp.h"
#include <string>
#include <string.h>
#include <stdio.h>

static UIColor *_backgroundColor;

@implementation MFANUpnpEdit {
    /* get data from topEdit's scanItems */
    UpnpProbe *_probep;
    MFANTopUpnp *_topUpnp;
    NSMutableSet *_listTableSelections;	/* set of NSIndexPaths with checkmarks */
    UITableView *_tableView;
    NSArray *_tagArray;
}

- (BOOL) anySelected
{
    return ([_listTableSelections count] != 0);
}

- (MFANUpnpEdit *) initWithParent: (MFANTopUpnp *) upnp
			tableView: (UITableView *) tview
			    probe: (void *) aprobep
			 tagArray: (NSArray *)tagArray
{
    self = [super init];
    if (self) {
	_topUpnp = upnp;
	_tableView = tview;
	_probep = (UpnpProbe *) aprobep;
	_listTableSelections = [[NSMutableSet alloc] init];
	_tagArray = tagArray;
    }
    return self;
}

- (BOOL) tableView: (UITableView *) tview canEditRowAtIndexPath: (NSIndexPath *) path
{
    return YES;
}

- (void) tableView: (UITableView *) tview didSelectRowAtIndexPath: (NSIndexPath *) path
{
    return;
}

- (void) tableView: (UITableView *) tview
commitEditingStyle: (UITableViewCellEditingStyle) style
 forRowAtIndexPath: (NSIndexPath *) path
{
    long row;
    long rows;
    uint32_t tag;
    UpnpDBase *dbasep;

    if (style == UITableViewCellEditingStyleDelete) {
	row = [path row];

	/* and mark that we've made changes so that we'll randomize and reload 
	 * song list when Done is pressed.
	 */
	_topUpnp.changesMade = YES;

	/* delete the device */
	rows = _probep->count();
	if (row < rows) {
	    tag = (uint32_t) [_tagArray[row] integerValue];
	    dbasep = (UpnpDBase *) [_topUpnp getDBase];
	    dbasep->deleteByTag(tag);
	    _probep->deleteNthDevice((uint32_t) row);

	    /* tag array is bad now, so recompute it */
	    [_topUpnp buildTagArray];

	    [_topUpnp saveStateWithDBase: YES];
	}

	/* what a sad, sad joke is iOS -- clowns broke deleting an
	 * item from a view from the swipe handler in ios 8.0.2.
	 */
	[NSTimer scheduledTimerWithTimeInterval: 0.01
		 target: self
		 selector: @selector(updateView:)
		 userInfo: nil
		 repeats: NO];
    }
}

- (void) updateView: (id) junk
{
    [_tableView reloadData];
}

- (NSInteger) tableView: (UITableView *)tview numberOfRowsInSection: (NSInteger) section
{
    return (NSInteger) _probep->count();
}

- (UITableViewCell *) tableView: (UITableView *) tview cellForRowAtIndexPath: (NSIndexPath *)path
{

    int32_t ix;
    UITableViewCell *cell;
    UIColor *textColor;
    int32_t totalCount;
    UpnpDevice *devp;
    std::string detailsStr;

    ix = (int32_t) [path row];
    
    totalCount = (int32_t) [_tagArray count];

    if (ix >= totalCount)
	return nil;

    devp = _probep->getDeviceByTag( (int32_t) [_tagArray[ix] integerValue]);
    if (!devp)
	return NULL;

    textColor = [MFANTopSettings textColor];

    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
				    reuseIdentifier: nil];
    cell.textLabel.text = [NSString stringWithCString: devp->_name.c_str()
				    encoding: NSUTF8StringEncoding];
    cell.textLabel.textColor = textColor;
    cell.textLabel.font = [MFANTopSettings basicFontWithSize: 18];
    cell.textLabel.adjustsFontSizeToFitWidth = YES;

    detailsStr = "Url: " + devp->_host;
    cell.detailTextLabel.text = [NSString stringWithCString: detailsStr.c_str()
					  encoding: NSUTF8StringEncoding];
    cell.detailTextLabel.textColor = textColor;

    // cell.contentView.backgroundColor = [UIColor clearColor];
    // cell.backgroundView.backgroundColor = [UIColor clearColor];
    // cell.multipleSelectionBackgroundView.backgroundColor = [UIColor clearColor];
    // cell.selectedBackgroundView.backgroundColor = [UIColor clearColor];
    cell.backgroundColor = [UIColor clearColor];

    return cell;
}

@end /* MFANUpnpEdit */

@implementation MFANTopUpnp {
    UITableView *_listTableView;	/* table view showing what's selected */
    MFANIconButton *_doneButton;	/* done button for entire edit */
    MFANIconButton *_clearButton;	/* clear button for entire edit */
    MFANIconButton *_findButton;	/* button to popup media choice */
    MFANIconButton *_resetButton;	/* button to popup media choice */
    MFANViewController *_viewCon;	/* back ptr to our view controller */
    MFANUpnpEdit *_upnpEdit;		/* to supply data to _listTableView */
    CGRect _appFrame;			/* frame for edit application */
    CGFloat _appHMargin;
    CGFloat _appVMargin;
    CGFloat _buttonHeight;
    CGFloat _buttonWidth;
    MFANPlayContext *_playContext;
    MFANPopStatus *_popStatus;
    NSMutableSet *_subviews;
    int _alertIndex;
    UpnpProbe _probe;
    UpnpDBase _dbase;
    UpnpAv _av;
    UIAlertView *_alert;
    BOOL _changesMade;
    NSMutableArray *_tagArray;		/* array of NSNumbers of tags */
    MFANPopHelp *_popHelp;
    MFANDownload *_download;		/* for downloading cover art / tag info */
    int32_t _covers;			/* # of covers processed */

    /* variables shared with async task */
    NSThread *_asyncThread;		/* runs to handle loads */
    int32_t _asyncRow;			/* row we're loading */
    BOOL _asyncDone;			/* we stopped */
    BOOL _loadCanceled;
}

/* global static variable */
MFANTopUpnp *_globalUpnp;

- (void) buildTagArray
{
    uint32_t count;
    uint32_t i;
    UpnpDevice *devp;

    count = _probe.count();
    [_tagArray removeAllObjects];
    for(i=0;i<count;i++) {
	devp = _probe.getNthDevice(i);
	[_tagArray addObject: [NSNumber numberWithInteger: devp->_tag]];
    }
}


- (id)initWithFrame:(CGRect)frame
     viewController: (MFANViewController *) viewCon
{
    CGRect tframe;
    UIColor *buttonColor;
    CGFloat nextButtonY;
    CGFloat defaultHue = 0.55;
    CGFloat defaultSat = 0.7;
    NSString *fileName;
    int nbuttons = 4;
    CGFloat buttonGap;
    CGFloat nextButtonX;
    CGFloat buttonMargin = 2;

    self = [super initWithFrame:frame];
    if (self) {
        // Initialization code
	_globalUpnp = self;
	_tagArray = [[NSMutableArray alloc] init];
	_viewCon = viewCon;
	_appVMargin = 20.0;
	_appHMargin = 2.0;

	[self setBackgroundColor: [UIColor whiteColor]];

	_subviews = [NSMutableSet setWithCapacity: 10];
	
	_appFrame = frame;
	_appFrame.origin.x += _appHMargin;
	_appFrame.origin.y += _appVMargin;
	_appFrame.size.width -= 2 * _appHMargin;
	_appFrame.size.height -= _appVMargin;

	_buttonHeight = 64.0;
	_buttonWidth = _buttonHeight;
	buttonGap = (_appFrame.size.width - _buttonWidth) / (nbuttons-1);
	nextButtonX = _appFrame.origin.x;

	_backgroundColor = [UIColor colorWithHue: 0.0
				    saturation: 0.0
				    brightness: 1.0
				    alpha: 0.0];

	nextButtonY = _appFrame.origin.y;

	// create a view here
	CGRect textFrame = CGRectMake( _appFrame.origin.x,
				       nextButtonY,
				       _appFrame.size.width,
				       _appFrame.size.height - _buttonHeight - buttonMargin);
	_listTableView = [[UITableView alloc] initWithFrame: textFrame
					      style:UITableViewStylePlain];
	// [_listTableView setAllowsMultipleSelection: YES];
	_upnpEdit = [[MFANUpnpEdit alloc] initWithParent: self
					  tableView: _listTableView
					  probe: &_probe
					  tagArray: _tagArray];
	[_listTableView setDelegate: _upnpEdit];
	[_listTableView setDataSource: _upnpEdit];
	_listTableView.backgroundColor = [UIColor clearColor];
	_listTableView.separatorStyle = UITableViewCellSeparatorStyleNone;
	// _listTableView.separatorStyle = UITableViewCellSeparatorStyleSingleLine;
	_listTableView.separatorColor = [UIColor blackColor];

	[_subviews addObject: _listTableView];
	[self addSubview: _listTableView];

	nextButtonY = _appFrame.origin.y + _appFrame.size.height - _buttonHeight;

	buttonColor = [UIColor colorWithHue: defaultHue
			       saturation: defaultSat
			       brightness: 1.0
			       alpha: 1.0];


	tframe.origin.x = nextButtonX;
	tframe.origin.y = nextButtonY;
	tframe.size.width = _buttonWidth;
	tframe.size.height = _buttonHeight;
	_findButton = [[MFANIconButton alloc] initWithFrame: tframe
					      title:@"Find Servers"
					      color: buttonColor
					      file: @"icon-search.png"];
	[_subviews addObject: _findButton];
	[self addSubview: _findButton];
	[_findButton addCallback: self
		     withAction: @selector(findPressed:withData:)];
	nextButtonX += buttonGap;

	/* now layout the clear/done/cancel buttons */
	tframe.origin.x = nextButtonX;
	tframe.origin.y = nextButtonY;
	tframe.size.width = _buttonWidth;
	tframe.size.height = _buttonHeight;
	_clearButton = [[MFANIconButton alloc] initWithFrame: tframe
					       title: @"Load From Selected"
					       color: buttonColor
					       file: @"icon-loadall.png"];
	[_subviews addObject: _clearButton];
	[self addSubview: _clearButton];
	[_clearButton addCallback: self
		      withAction: @selector(loadSelectedPressed:)];
	nextButtonX += buttonGap;

	/* now layout the clear/done/cancel buttons */
	tframe.origin.x = nextButtonX;
	tframe.origin.y = nextButtonY;
	tframe.size.width = _buttonWidth;
	tframe.size.height = _buttonHeight;
	_resetButton = [[MFANIconButton alloc] initWithFrame: tframe
					       title: @"Reset Upnp"
					       color: buttonColor
					       file: @"icon-erase.png"];
	[_subviews addObject: _resetButton];
	[self addSubview: _resetButton];
	[_resetButton addCallback: self
		      withAction: @selector(resetPressed:withData:)];
	nextButtonX += buttonGap;

	/* now layout the clear/done/cancel buttons */
	tframe.origin.x = nextButtonX;
	tframe.origin.y = nextButtonY;
	tframe.size.width = _buttonWidth;
	tframe.size.height = _buttonHeight;
	_doneButton = [[MFANIconButton alloc] initWithFrame: tframe
					      title: @"Done"
					      color: [UIColor greenColor]
					      file: @"icon-done.png"];
	[_subviews addObject: _doneButton];
	[self addSubview: _doneButton];
	[_doneButton addCallback: self
		     withAction: @selector(donePressed:withData:)];
	nextButtonX += buttonGap;

	_changesMade = NO;

	_probe.clearAll();

	fileName = [self fileNameForDevs];
	_probe.restoreFromFile([fileName cStringUsingEncoding: NSUTF8StringEncoding]);

	[self buildTagArray];

	fileName = [self fileNameForDBase];
	_dbase.restoreFromFile([fileName cStringUsingEncoding: NSUTF8StringEncoding]);

	_popHelp = [[MFANPopHelp alloc] initWithFrame: _appFrame
					helpFile: @"help-upnp"
					parentView: self
					warningFlag: MFANTopSettings_warnedUpnp];

	_download = [[MFANDownload alloc] initWithPlayContext: nil];
    }
    return self;
}

+ (UIColor *) backgroundColor
{
    return _backgroundColor;
}

+ (MFANTopUpnp *) getGlobalUpnp
{
    return _globalUpnp;
}

- (NSString *) fileNameForDevs
{
    NSArray *paths;
    NSString *libdir;
    NSString *value;

    paths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES);
    libdir = [paths objectAtIndex:0];
    value = [NSString stringWithFormat: @"%@/upnp-devs.state", libdir];

    return value;
}

- (NSString *) fileNameForDBase
{
    NSArray *paths;
    NSString *libdir;
    NSString *value;

    paths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES);
    libdir = [paths objectAtIndex:0];
    value = [NSString stringWithFormat: @"%@/upnp-dbase.state", libdir];

    return value;
}

- (void) saveStateWithDBase: (bool) saveDBase
{
    NSString *fileName;

    fileName = [self fileNameForDevs];
    _probe.saveToFile([fileName cStringUsingEncoding: NSUTF8StringEncoding]);

    if (saveDBase) {
	fileName = [self fileNameForDBase];	
	_dbase.saveToFile([fileName cStringUsingEncoding: NSUTF8StringEncoding]);
    }
}

- (void) findPressed: (id) sender withData: (NSNumber *) number
{
    _alert = [[UIAlertView alloc]
		 initWithTitle:@"Searching"
		 message:@"Searching for UPNP Music Servers"
		 delegate:nil 
		 cancelButtonTitle:nil
		 otherButtonTitles: nil];
    [_alert show];

    [NSTimer scheduledTimerWithTimeInterval: 0.1
	     target:self
	     selector:@selector(findPart2:)
	     userInfo:nil
	     repeats: NO];
}

- (void) findPart2: (id) junk
{
    int32_t code;
    uint32_t i;
    uint32_t devCount;
    UpnpDevice *devp;

    code = _probe.init();
    NSLog(@"All done with probe, code = %d\n", code);
    code = _probe.contactAllDevices();
    NSLog(@"All done with contact devices, code = %d\n", code);

    devCount = _probe.count();
    [_tagArray removeAllObjects];
    for(i=0;i<devCount;i++) {
	devp = _probe.getNthDevice(i);
	[_tagArray addObject: [NSNumber numberWithInteger: devp->_tag]];
    }

    [_listTableView reloadData];

    [_alert dismissWithClickedButtonIndex: 0 animated: YES];
    _alert = nil;

    [self saveStateWithDBase: 0];
}

- (void) loadSelectedPressed: (id) sender
{
    NSIndexPath *selectedPath;
    CGRect tframe;

    selectedPath = [_listTableView indexPathForSelectedRow];
    _asyncRow = (int32_t) [selectedPath row];
    _asyncDone = NO;
    NSLog(@"row is %d\n", _asyncRow);

    _asyncThread = [[NSThread alloc] initWithTarget: self
				     selector: @selector(bkgOp:)
				     object: nil];
    [_asyncThread start];

    tframe = _appFrame;
    _popStatus = [[MFANPopStatus alloc] initWithFrame: tframe
					msg: @""
					parentView: self];
    [_popStatus show];
    _loadCanceled = NO;

    [NSTimer scheduledTimerWithTimeInterval: 1.0
	     target:self
	     selector:@selector(loadPart2:)
	     userInfo:nil
	     repeats: NO];
}

- (void) loadPart2: (id) junk
{
    NSString *msg;

    if (_asyncDone) {
	/* and save file */
	[self saveStateWithDBase: YES];

	msg = [NSString stringWithFormat: @"Scanned %d pages / %d songs / %d covers",
			(int) _av.loadedPages(),
			(int) _av.loadedItems(),
			(int) _covers];
	[_popStatus stop];
	_popStatus = nil;
    }
    else {
	/* still going */
	msg = [NSString stringWithFormat: @"Scanned %d pages / %d songs / %d covers",
			(int) _av.loadedPages(), (int) _av.loadedItems(), (int) _covers];
	[_popStatus updateMsg: msg];

	if ([_popStatus canceled]) {
	    _popStatus = nil;
	    NSLog(@"load set canceled");
	    _loadCanceled = YES;	/* cancel art download */
	    _av.cancel();		/* cancel browsing */
	}

	[NSTimer scheduledTimerWithTimeInterval: 1.0
		 target:self
		 selector:@selector(loadPart2:)
		 userInfo:nil
		 repeats: NO];
    }
}

int32_t
loadArtForEntry(void *contextp, void *recordContextp)
{
    int32_t code;
    UpnpDBase::Record *recordp = (UpnpDBase::Record *) recordContextp;
    MFANTopUpnp *top = (__bridge MFANTopUpnp *) contextp;
    NSString *artUrl;
    NSString *fileName;
    MFANDownloadReq *downloadReq;
    const char *artUrlp;

    /* returning non-zero tells apply to stop now */
    if (top->_loadCanceled) {
	NSLog(@"loadArt saw cancel");
	return 1;
    }

    /* load the object so we can extract the info */
    artUrlp = recordp->_artUrl.c_str();
    artUrl = [NSString stringWithCString: artUrlp encoding: NSUTF8StringEncoding];
    if (strlen(artUrlp) == 0) {
	NSLog(@"No artUrl provided");
	return 0;
    }
    NSLog(@"about to load art from URL=%@", artUrl);
    fileName = [MFANDownload artFileNameForHash: artUrl extension: @"jpg"];

    top->_covers++;

    /* don't download it if already downloaded */
    if ([[NSFileManager defaultManager] fileExistsAtPath: fileName]) {
	return 0;
    }

    downloadReq = [[MFANDownloadReq alloc] initWithUrlRemote: artUrl
					   localPath: fileName];

    code  = [top->_download loadUrlInternal: downloadReq wait: YES];

    NSLog(@"load done code=%d", code);

    return 0;
}

- (int32_t) loadArt: (UpnpDevice *) devp
{
    /* how many covers loaded */
    _covers = 0;

    _dbase.apply(loadArtForEntry, (__bridge void *) self, 1);

    NSLog(@"LoadArt is done");

    [self saveStateWithDBase: YES];

    return 0;
}

/* do browse from a background thread */
- (void) bkgOp: (id) junk
{
    int32_t row;
    UpnpDevice *devp;
    int32_t totalCount;
    int32_t code;
    int32_t tag;
    
    row = _asyncRow;
    totalCount = (int32_t) [_tagArray count];

    if (row < totalCount) {
	tag = (int32_t) [_tagArray[row] integerValue];
	devp = _probe.getDeviceByTag( tag);
	if (!devp) {
	    _asyncDone = YES;
	    _asyncThread = nil;
	    [NSThread exit];
	    return;
	}

	_dbase.deleteByTag(tag);

	code = _av.init(devp);

	if (code != 0) {
	    NSLog(@"av init failed");
	    _asyncDone = YES;
	    _asyncThread = nil;
	    [NSThread exit];
	    return;
	}

	code = _av.browse(&_dbase, "0");
	if (code) {
	    NSLog(@"av browse failed");
	    _asyncDone = YES;
	    _asyncThread = nil;
	    [NSThread exit];
	    return;
	}

	code = [self loadArt: devp];
    }

    _asyncDone = YES;
    _asyncThread = nil;
    [NSThread exit];
}

- (void) resetPressed: (id) sender withData: (NSNumber *) number
{
    _dbase.deleteAll();
    _probe.clearAll();
    [self saveStateWithDBase: YES];
    [_listTableView reloadData];
}

- (void) donePressed: (id) sender withData: (NSNumber *) number
{
    [_viewCon switchToAppByName: @"main"];
}    MFANWarn *warn;

- (void) helpPressed: (id) sender withData: (NSNumber *) number
{
    /* even if we're canceled, someone may have changed the load/unload status
     * of some of our items.  So, reevaluate the real array.
     */
    MFANPopHelp *popHelp;

    popHelp = [[MFANPopHelp alloc] initWithFrame: _appFrame
				   helpFile: @"help-upnp"
				   parentView: self
				   warningFlag: 0];
    [popHelp show];
}

-(void) deactivateTop
{
    return;
}

-(void) activateTop
{
    [_popHelp checkShow];
    return;
}

- (void) addScanItem: (MFANScanItem *) scan
{
    return;
}

- (void *)getDBase
{
    return (void *) &_dbase;
}

@end /* MFANTopUpnp */
