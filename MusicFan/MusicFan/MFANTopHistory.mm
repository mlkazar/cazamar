//
//  MFANTopHistory.m
//  MusicFan
//
//  Created by Michael Kazar on 4/27/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANTopHistory.h"
#import "MFANIconButton.h"
#import "MFANViewController.h"
#import "MFANIndicator.h"
#import "MFANCGUtil.h"
#import "MFANRadioConsole.h"
#import "MFANTopSettings.h"
#import "MFANWarn.h"
#import "MFANPopHelp.h"
#import "MFANCGUtil.h"
#import "MFANAqPlayer.h"

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "osp.h"
#include "json.h"
#include <string>

static UIColor *_backgroundColor;

@implementation MFANHistoryItem {
    /* nothing here that's not a property */
}
@end

@implementation MFANListHistory {
    /* get data from topEdit's histItems */
    MFANTopHistory *_topHist;
    NSMutableSet *_listTableSelections;	/* set of NSIndexPaths with checkmarks */
    UITableView *_tableView;
}

- (BOOL) anySelected
{
    return ([_listTableSelections count] != 0);
}

- (MFANListHistory *) initWithParent:(MFANTopHistory *) hist tableView: (UITableView *) tview
{
    self = [super init];
    if (self) {
	_topHist = hist;
	_tableView = tview;
	_listTableSelections = [[NSMutableSet alloc] init];

	
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

#ifdef notdef
- (void) tableView: (UITableView *) tview
commitEditingStyle: (UITableViewCellEditingStyle) style
 forRowAtIndexPath: (NSIndexPath *) path
{
    long row;
    NSMutableArray *histItemArray;

    if (style == UITableViewCellEditingStyleDelete) {
	row = [path row];
	histItemArray = [_topHist histItems];
	[histItemArray removeObjectAtIndex: row];

	/* and mark that we've made changes so that we'll randomize and reload 
	 * song list when Done is pressed.
	 */
	_topHist.changesMade = YES;

	/* what a sad, sad joke is iOS -- clowns broke deleting an
	 * item from a view from the swipe handler in ios 8.0.2.
	 */
	[NSTimer scheduledTimerWithTimeInterval: 0.01
		 target: self
		 selector: @selector(updateView:)
		 userInfo: nil
		 repeats: NO];
    }
    NSLog(@"SURPRISE CALL TO commiteditngstyle");
}
#endif

- (void) removeRowAtPath: (NSIndexPath *) path
{
    long row;
    NSMutableArray *histItemArray;

    row = [path row];
    histItemArray = [_topHist histItems];
    [histItemArray removeObjectAtIndex: row];

    /* and mark that we've made changes so that we'll randomize and reload 
     * song list when Done is pressed.
     */
    _topHist.changesMade = YES;

    /* what a sad, sad joke is iOS -- clowns broke deleting an
     * item from a view from the swipe handler in ios 8.0.2.
     */
    [NSTimer scheduledTimerWithTimeInterval: 0.01
				     target: self
				   selector: @selector(updateView:)
				   userInfo: nil
				    repeats: NO];
}

- (void) highlightRowAtPath: (NSIndexPath *) path
{
    long row;
    NSMutableArray *histItemArray;
    MFANHistoryItem *hist;

    row = [path row];
    histItemArray = [_topHist histItems];
    hist = [histItemArray objectAtIndex: row];
    hist.highlighted = !hist.highlighted;
    [histItemArray replaceObjectAtIndex: row withObject: hist];

    /* and mark that we've made changes so that we'll randomize and reload 
     * song list when Done is pressed.
     */
    _topHist.changesMade = YES;

    /* what a sad, sad joke is iOS -- clowns broke deleting an
     * item from a view from the swipe handler in ios 8.0.2.
     */
    [NSTimer scheduledTimerWithTimeInterval: 0.01
				     target: self
				   selector: @selector(updateView:)
				   userInfo: nil
				    repeats: NO];
}

- (void) updateView: (id) junk
{
    [_tableView reloadData];
}

- (NSArray *) tableView: (UITableView *) tview
editActionsForRowAtIndexPath: (NSIndexPath *) path
{
    long row;
    NSMutableArray *histItemArray;
    MFANHistoryItem *hist;
    NSString *hlString;
    
    row = [path row];
    histItemArray = [_topHist histItems];
    hist = [histItemArray objectAtIndex: row];

    UITableViewRowAction *deleteAction =
	[UITableViewRowAction rowActionWithStyle:UITableViewRowActionStyleNormal
					   title:@"Delete"
					 handler:^(UITableViewRowAction *action,
						   NSIndexPath *path) {
		[self removeRowAtPath: path];
	    }];

    deleteAction.backgroundColor = [UIColor redColor];

    if (hist.highlighted)
	hlString = @"Unhighlight";
    else
	hlString = @"Highlight";

    UITableViewRowAction *hlAction =
	[UITableViewRowAction rowActionWithStyle:UITableViewRowActionStyleNormal
					   title:hlString
					 handler:^(UITableViewRowAction *action,
						   NSIndexPath *path) {
		[self highlightRowAtPath: path];
	    }];
    hlAction.backgroundColor = [UIColor blueColor];

    return @[hlAction, deleteAction];
}

- (NSInteger) tableView: (UITableView *)tview numberOfRowsInSection: (NSInteger) section
{
    return (NSInteger) [[_topHist histItems] count];
}

- (UITableViewCell *) tableView: (UITableView *) tview cellForRowAtIndexPath: (NSIndexPath *)path
{

    unsigned long ix;
    UITableViewCell *cell;
    NSMutableArray *histItemArray;
    
    UIColor *textColor;
    MFANHistoryItem *hist;
    NSString *subtitle;
    time_t playTime;
    char timeBuffer[64];

    ix = [path row];
    
    histItemArray = [_topHist histItems];
    if (ix >= [histItemArray count])
	return nil;

    hist = [histItemArray objectAtIndex: ix];
    if (hist == nil)
	return nil;

    textColor = [MFANTopSettings textColor];

    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
				    reuseIdentifier: nil];
    cell.textLabel.text = hist.song;
    cell.textLabel.textColor = [MFANTopSettings textColor];
    cell.textLabel.font = [MFANTopSettings basicFontWithSize: 18];
    cell.textLabel.adjustsFontSizeToFitWidth = YES;

    playTime = hist.when;
    ctime_r(&playTime, timeBuffer);
    timeBuffer[20] = 0;	/* drop the year, and the +4 below drops the name of the day */
    subtitle = [NSString stringWithFormat: @"%@ at %s", hist.station, timeBuffer+4];

    cell.detailTextLabel.text = subtitle;
    cell.detailTextLabel.textColor = textColor;

    // cell.contentView.backgroundColor = [UIColor clearColor];
    // cell.backgroundView.backgroundColor = [UIColor clearColor];
    // cell.multipleSelectionBackgroundView.backgroundColor = [UIColor clearColor];
    // cell.selectedBackgroundView.backgroundColor = [UIColor clearColor];
    if (hist.highlighted) {
	cell.backgroundColor = [MFANTopSettings selectedBackgroundColor];
    } else {
	cell.backgroundColor = [UIColor clearColor];
    }

    return cell;
}

@end /* MFANListHistory */

@implementation MFANTopHistory {
    UITableView *_listTableView;	/* table view showing what's selected */
    NSMutableArray *_histItems;		/* array of MFANHistoryItems */
    MFANIconButton *_doneButton;	/* done button for entire edit */
    MFANIconButton *_clearButton;	/* clear button for entire edit */
    MFANIconButton *_helpButton;	/* button for context specific help */
    MFANViewController *_viewCont;	/* back ptr to our view controller */
    MFANListHistory *_listHist;		/* to supply data to _listTableView */
    NSMutableArray *_subviews;		/* array of child views (buttons mostly) */
    CGRect _appFrame;			/* frame for edit application */
    CGFloat _appHMargin;
    CGFloat _appVMargin;
    CGFloat _buttonHeight;
    CGFloat _buttonWidth;
    CGFloat _buttonMargin;
    uint32_t _alertIndex;
    uint32_t _maxEntries;
}

- (id)initWithFrame:(CGRect)frame
     viewController: (MFANViewController *) viewCont
{
    CGRect tframe;
    UIColor *buttonColor;
    CGFloat nextButtonY;
    CGFloat defaultHue = 0.55;
    CGFloat defaultSat = 0.7;

    self = [super initWithFrame:frame];
    if (self) {
        // Initialization code
	_viewCont = viewCont;
	_histItems = [[NSMutableArray alloc] init];
	_appVMargin = 20.0;
	_appHMargin = 2.0;
	_buttonHeight = 50.0;
	_buttonMargin = 5.0;
	_appFrame = frame;
	_appFrame.origin.x += _appHMargin;
	_appFrame.origin.y += _appVMargin;
	_appFrame.size.width -= 2 * _appHMargin;
	_appFrame.size.height -= _appVMargin + _appHMargin;
	_subviews = [[NSMutableArray alloc] init];
	_maxEntries = 1000;

	// float col1of1 = _appFrame.origin.x + _appFrame.size.width/2 - _buttonWidth/2;

	/* how to center 2 buttons on screen */
	_buttonWidth = _appFrame.size.width/3;
	float col1of2 = _appFrame.origin.x + _appFrame.size.width/4 - _buttonWidth/2;
	float col2of2 = _appFrame.origin.x + 3*_appFrame.size.width/4 - _buttonWidth/2;

	/* how to center 3 buttons on screen -- currently unused */
	//float col1of3 = _appFrame.origin.x + _appFrame.size.width/6 - _buttonWidth/2;
	//float col2of3 = _appFrame.origin.x + 3*_appFrame.size.width/6 - _buttonWidth/2;
	//float col3of3 = _appFrame.origin.x + 5*_appFrame.size.width/6 - _buttonWidth/2;

	/* how to center 4 buttons on screen */
	// float col1of4 = _appFrame.origin.x + _appFrame.size.width/8 - _buttonWidth/2;
	// float col2of4 = _appFrame.origin.x + 3*_appFrame.size.width/8 - _buttonWidth/2;
	// float col3of4 = _appFrame.origin.x + 5*_appFrame.size.width/8 - _buttonWidth/2;
	// float col4of4 = _appFrame.origin.x + 7*_appFrame.size.width/8 - _buttonWidth/2;

	/* how to center 5 buttons on screen */
	// float col1of5 = _appFrame.origin.x + _appFrame.size.width/10 - _buttonWidth/2;
	// float col2of5 = _appFrame.origin.x + 3*_appFrame.size.width/10 - _buttonWidth/2;
	// float col3of5 = _appFrame.origin.x + 5*_appFrame.size.width/10 - _buttonWidth/2;
	// float col4of5 = _appFrame.origin.x + 7*_appFrame.size.width/10 - _buttonWidth/2;
	// float col5of5 = _appFrame.origin.x + 9*_appFrame.size.width/10 - _buttonWidth/2;

	_backgroundColor = [UIColor colorWithHue: 0.0
				    saturation: 0.0
				    brightness: 1.0
				    alpha: 0.0];

	nextButtonY = _appFrame.origin.y;

	// create a view here
	CGRect textFrame = CGRectMake( _appFrame.origin.x,
				       nextButtonY,
				       _appFrame.size.width,
				       _appFrame.size.height - 2*_buttonHeight);
	_listTableView = [[UITableView alloc] initWithFrame: textFrame
					      style:UITableViewStylePlain];
	[_listTableView setAllowsMultipleSelection: YES];
	_listHist = [[MFANListHistory alloc] initWithParent: self tableView: _listTableView];
	[_listTableView setDelegate: _listHist];
	[_listTableView setDataSource: _listHist];
	_listTableView.backgroundColor = [UIColor clearColor];
	_listTableView.separatorStyle = UITableViewCellSeparatorStyleNone;
	// _listTableView.separatorStyle = UITableViewCellSeparatorStyleSingleLine;
	_listTableView.separatorColor = [UIColor blackColor];

	[_subviews addObject: _listTableView];
	[self addSubview: _listTableView];

	nextButtonY = _appFrame.origin.y + _appFrame.size.height - 2*_buttonHeight;

	buttonColor = [UIColor colorWithHue: defaultHue
			       saturation: defaultSat
			       brightness: 1.0
			       alpha: 1.0];

	/* now layout the clear/done/cancel buttons */
	tframe.origin.x = col1of2;
	tframe.origin.y = nextButtonY;
	tframe.size.width = _buttonWidth;
	tframe.size.height = _buttonHeight;
	_clearButton = [[MFANIconButton alloc] initWithFrame: tframe
					       title: @"Clear All"
					       color: buttonColor
					       file: @"icon-erase.png"];
	[_subviews addObject: _clearButton];
	[self addSubview: _clearButton];
	[_clearButton addCallback: self
		      withAction: @selector(clearPressed:withData:)];

	/* now layout the clear/done/cancel buttons */
	tframe.origin.x = col2of2;
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

	/* and read in the file */
	[self restoreEdits];
    }
    return self;
}

- (NSMutableArray *) histItems
{
    return _histItems;
}

+ (UIColor *) backgroundColor
{
    return _backgroundColor;
}

- (void) restoreSubviews
{
    UIView *view;
    for (view in _subviews) {
	[self addSubview: view];
    }
}

- (void) hideSubviews
{
    UIView *view;
    for (view in _subviews) {
	[view removeFromSuperview];
    }
}

- (void) clearPressed: (id) sender withData: (NSNumber *) number
{
    UIAlertView *alert = [[UIAlertView alloc] initWithTitle:@"Clear all entries?"
					      message:@"Are you sure you want to clear all entries in this playlist?"
					      delegate:nil 
					      cancelButtonTitle:@"Cancel"
					      otherButtonTitles:nil];

    [alert addButtonWithTitle: @"OK"];
    [alert setDelegate: self];
    [alert show];
    return;
}

- (void) clearAfterAlert
{
    uint32_t ix;
    uint32_t count;
    MFANHistoryItem *hist;

    if (_alertIndex == 0) {
	/* did a cancel */
	return;
    }

    // Otherwise prune all unhighlighted entries
    ix = 0;
    while(1) {
	count = [_histItems count];
	if (ix >= count)
	    break;
	hist = [_histItems objectAtIndex: ix];
	if (hist.highlighted)
	    ix++;
	else
	    [_histItems removeObjectAtIndex: ix];
    }

    _changesMade = YES;

    [_listTableView reloadData];
} 

/* add code here to dispatch on aview if we have more than one alert in this
 * class.
 */
- (void) alertView: (UIAlertView *)aview didDismissWithButtonIndex: (NSInteger) ix
{
    _alertIndex = (int) ix;
    [self clearAfterAlert];
}

/* called at initialization to read saved history */
- (void) restoreEdits
{
    int32_t code;
    NSString *fileName;
    FILE *inFilep;
    MFANHistoryItem *item;
    Json::Node *childNodep;
    Json::Node *songp;
    Json::Node *stationp;
    Json::Node *highlightedp;
    Json::Node *whenp;
    Json::Node *rootNodep;
    Json json;

    fileName = fileNameForFile(@"history.txt");
    inFilep = fopen([fileName cStringUsingEncoding: NSUTF8StringEncoding], "r");
    if (inFilep == NULL) {
	NSLog(@"MFANTopHistory: failed to open history.txt");
	return;
    }

    InStreamFile jsonStream(inFilep);
    code = json.parseJsonValue(&jsonStream, &rootNodep);
    if (code != 0) {
	NSLog(@"MFANTopHistory: failed to open history.txt");
	return;
    }
    
    for(childNodep = rootNodep->_children.head(); childNodep; childNodep = childNodep->_dqNextp) {
	/* each child is a struct with 'when', 'song' and 'station' records */
	whenp = childNodep->searchForChild("when", 0);
	stationp = childNodep->searchForChild("station", 0);
	songp = childNodep->searchForChild("song", 0);
	highlightedp = childNodep->searchForChild("highlighted", 0);
	if (!whenp || !stationp || !songp) {
	    NSLog(@"* restoreEdits failed to read complete record %p %p %p",
		  whenp, stationp, songp);
	    continue;
	}

	/* otherwise, create a new history item and append it */
	item = [[MFANHistoryItem alloc] init];
	item.when = atoi(whenp->_children.head()->_name.c_str());
	item.station = [NSString stringWithCString: stationp->_children.head()->_name.c_str()
				 encoding: NSUTF8StringEncoding];
	item.song = [NSString stringWithCString: songp->_children.head()->_name.c_str()
			      encoding: NSUTF8StringEncoding];
	if (highlightedp) {
	    item.highlighted = atoi(highlightedp->_children.head()->_name.c_str());
	}
	[_histItems addObject: item];
    }
    fclose(inFilep);
    delete rootNodep;

    [_listTableView reloadData];
}

- (void) saveEdits
{
    FILE *outFilep;
    MFANHistoryItem *item;
    Json::Node *rootNodep;
    Json::Node *structNodep;
    Json::Node *leafNodep;
    Json::Node *namedNodep;
    std::string outString;
    uint32_t nbytes;
    NSString *fileName;
    int32_t code;
    NSString *finalName;
    NSError *errorObj;
    NSFileManager *fileManager = [NSFileManager defaultManager];

    if (!_changesMade) {
	return;
    }

    fileName = fileNameForFile(@"history.new");
    finalName = fileNameForFile(@"history.txt");
    outFilep = fopen([fileName cStringUsingEncoding: NSUTF8StringEncoding], "w");
    if (outFilep == NULL) {
	NSLog(@"MFANTopHistory: failed to open history.txt");
	return;
    }

    /* otherwise, go through the history array and build a json structure from it */
    
    rootNodep = new Json::Node();
    rootNodep->initArray();
    for (item in _histItems) {
	/* we put three items in each element, 'when' is an integer giving the unix
	 * time (since 1/1/1970 GMT) that the song started to play, 'station' gives
	 * the radio station name, as a string, and 'song' gives the stream title,
	 * which usually includes both artist and song name, in random order, often
	 * separated by a '-' character.
	 */
	structNodep = new Json::Node();
	structNodep->initStruct();

	/* add 'when' */
	leafNodep = new Json::Node();
	leafNodep->initInt(item.when);
	namedNodep = new Json::Node();
	namedNodep->initNamed("when", leafNodep);
	structNodep->appendChild(namedNodep);

	/* add 'station' */
	leafNodep = new Json::Node();
	leafNodep->initString([item.station cStringUsingEncoding: NSUTF8StringEncoding], 1);
	namedNodep = new Json::Node();
	namedNodep->initNamed("station", leafNodep);
	structNodep->appendChild(namedNodep);

	/* add 'song' */
	leafNodep = new Json::Node();
	leafNodep->initString([item.song cStringUsingEncoding: NSUTF8StringEncoding], 1);
	namedNodep = new Json::Node();
	namedNodep->initNamed("song", leafNodep);
	structNodep->appendChild(namedNodep);

	// Add highlighted
	leafNodep = new Json::Node();
	leafNodep->initInt(item.highlighted);
	namedNodep = new Json::Node();
	namedNodep->initNamed("highlighted", leafNodep);
	structNodep->appendChild(namedNodep);

	/* and now append the struct to the array */
	rootNodep->appendChild(structNodep);
    }

    /* unparse to a string */
    rootNodep->unparse(&outString);
    nbytes = (uint32_t) outString.length();
    code = (int32_t) fwrite(outString.c_str(), 1, nbytes, outFilep);
    if (code != nbytes) {
	NSLog(@"MFANTopHistory: failed to write all data %d should be %d", code, nbytes);
    }

    /* and clean up everything */
    fclose(outFilep);
    outFilep = NULL;
    delete rootNodep;
    rootNodep = NULL;

    [fileManager removeItemAtPath: finalName error: nil];
    [fileManager moveItemAtPath: fileName toPath: finalName error: &errorObj];
    if (errorObj)
	NSLog(@"rename error=%p/%d %@",
	      errorObj, (int) [errorObj code], [errorObj localizedDescription]);
}

- (void) nextView
{
    [_viewCont switchToAppByName: @"main"];
}

- (void) helpPressed: (id) sender withData: (NSNumber *) number
{
    /* even if we're canceled, someone may have changed the load/unload status
     * of some of our items.  So, reevaluate the real array.
     */
    MFANPopHelp *popHelp;

    popHelp = [[MFANPopHelp alloc] initWithFrame: _appFrame
				   helpFile: @"help-hist"
				   parentView: self
				   warningFlag: 0];
    [popHelp show];
}

- (void) donePressed: (id) sender withData: (NSNumber *) number
{
    if (!_changesMade) {
	[self nextView];
	return;
    }

    [self saveEdits];

    [self nextView];
}

-(void) deactivateTop
{
    return;
}

-(void) activateTop
{
    int ix;

    ix = (int) [_histItems count];
    if (ix <= 0)
	return;

    [_listTableView reloadData];

    [_listTableView
	scrollToRowAtIndexPath: [NSIndexPath indexPathForRow: ix-1 inSection: 0]
	atScrollPosition: UITableViewScrollPositionBottom
	animated: YES];
}

- (void) addHistoryStation: (NSString *) station withSong: (NSString *) song
{
    MFANHistoryItem *hist;
    MFANHistoryItem *prev;
    int32_t count;
    int32_t ix;
    NSString *unknownString;

    /* prune from the head */
    ix = 0;
    while (1) {
	// Delete oldest entries, but also watch for situation where
	// we have selected to keep more than _maxEntries.
	count = (int32_t) [_histItems count];
	if (count <= _maxEntries || ix >= count)
	    break;

	hist = [_histItems objectAtIndex: ix];
	if (hist.highlighted)
	    ix++;
	else
	    [_histItems removeObjectAtIndex: ix];
    }

    // Recompute in case we bailed from above loop due to large ix.
    count = [_histItems count];

    /* ignore updates from stations that don't provide song information */
    unknownString = MFANAqPlayer_getUnknownString();
    if ([song isEqualToString: unknownString]) {
	return;
    }

    /* suppress duplicates */
    if (count >= 1) {
	prev = [_histItems objectAtIndex: count-1];
	if ( [prev.station isEqualToString: station] &&
	     [prev.song isEqualToString: song]) {
	    return;
	}
    }

    hist = [[MFANHistoryItem alloc] init];
    hist.station = station;
    hist.song = song;
    hist.when = osp_time_sec();
    hist.highlighted = NO;

    [_histItems addObject: hist];
    _changesMade = YES;

    [_listTableView reloadData];

    [self saveEdits];
}

- (void) toggleHighlight
{
    uint32_t count;
    uint32_t ix;
    MFANHistoryItem *lastItem;

    count = [_histItems count];
    if (count < 1)
	return;
    ix = count-1;	// dealing with the last item

    lastItem = [_histItems objectAtIndex: ix];
    lastItem.highlighted = !lastItem.highlighted;
    [_histItems replaceObjectAtIndex: ix withObject: lastItem];
}

- (BOOL) isHighlighted
{
    uint32_t count;
    uint32_t ix;
    MFANHistoryItem *lastItem;

    count = [_histItems count];
    if (count < 1)
	return NO;
    ix = count-1;	// dealing with the last item

    lastItem = [_histItems objectAtIndex: ix];
    return lastItem.highlighted;
}

- (void)drawRect:(CGRect)rect
{
    drawBackground(rect);
}

@end /* MFANTopHistory */
