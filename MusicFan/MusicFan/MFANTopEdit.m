//
//  MFANTopEdit.m
//  MusicFan
//
//  Created by Michael Kazar on 4/27/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANTopEdit.h"
#import "MFANTopLevel.h"
#import "MFANIconButton.h"
#import "MFANViewController.h"
#import "MFANSetList.h"
#import "MFANPlayContext.h"
#import "MFANIndicator.h"
#import "MFANStarsView.h"
#import "MFANCGUtil.h"
#import "MFANPopEdit.h"
#import "MFANRadioConsole.h"
#import "MFANAddChoice.h"
#import "MFANTopSettings.h"
#import "MFANDownload.h"
#import "MFANWarn.h"
#import "MFANPopHelp.h"

static UIColor *_backgroundColor;

@implementation MFANListEdit {
    /* get data from topEdit's scanItems */
    MFANTopEdit *_topEdit;
    NSMutableSet *_listTableSelections;	/* set of NSIndexPaths with checkmarks */
    UITableView *_tableView;
    MFANLabel *_deleteButton;
    CGFloat _rowHeight;
    CGRect _localFrame;
    int _detailIndex;
    UISwipeGestureRecognizer *_swipeLeft;
    UISwipeGestureRecognizer *_swipeRight;
}

- (BOOL) anySelected
{
    return ([_listTableSelections count] != 0);
}

- (void) reloadData
{
    [_tableView reloadData];
}

- (MFANListEdit *) initWithParent:(MFANTopEdit *) edit frame: (CGRect) frame;
{

    self = [super init];
    if (self) {
	_topEdit = edit;
	_localFrame = frame;
	_detailIndex = -1;

	_tableView = [[UITableView alloc] initWithFrame: frame
					  style:UITableViewStylePlain];
	[_tableView setAllowsMultipleSelection: NO];
	[_tableView setDelegate: self];
	[_tableView setDataSource: self];
	_tableView.backgroundColor = [UIColor clearColor];
	_tableView.separatorStyle = UITableViewCellSeparatorStyleNone;
	// _tableView.separatorStyle = UITableViewCellSeparatorStyleSingleLine;
	_tableView.separatorColor = [UIColor blackColor];
	[_tableView setEditing: YES animated: NO];

	_swipeLeft = [[UISwipeGestureRecognizer alloc]
			 initWithTarget:self action:@selector(handleSwipeFrom:)];
	_swipeLeft.direction = UISwipeGestureRecognizerDirectionLeft;
	[_tableView addGestureRecognizer: _swipeLeft];

	_swipeRight = [[UISwipeGestureRecognizer alloc]
			 initWithTarget:self action:@selector(handleSwipeFrom:)];
	_swipeRight.direction = UISwipeGestureRecognizerDirectionRight;
	[_tableView addGestureRecognizer: _swipeRight];

	[_topEdit addSubview: _tableView];

	_listTableSelections = [[NSMutableSet alloc] init];

	_rowHeight = _tableView.rowHeight;
	NSLog(@"tableview rowh=%f", _rowHeight);

	_deleteButton = [[MFANLabel alloc] initWithTarget: self
					   selector:@selector(deleteRow:withData:)];
	[_deleteButton setTitle: @"Delete" forState: UIControlStateNormal];
	_deleteButton.backgroundColor = [UIColor redColor];
    }
    return self;
}

- (UITableView *) tableView
{
    return _tableView;
}

- (void) deleteRow: (id) j1 withData: (id) j2
{
    NSMutableArray *scanItemArray;
    NSLog(@"in deleterow");

    if (_detailIndex >= 0) {
	scanItemArray = [_topEdit scanItems];
	[scanItemArray removeObjectAtIndex: _detailIndex];
	/* and mark that we've made changes so that we'll randomize and reload 
	 * song list when Done is pressed.
	 */
	_topEdit.changesMade = YES;
	_detailIndex = -1;

	[_tableView reloadData];
    }
}

- (void) handleSwipeFrom: (UISwipeGestureRecognizer *)recog
{
    CGPoint pt;
    NSIndexPath *path;
    int row;

    pt = [recog locationInView: _tableView];
    path = [_tableView indexPathForRowAtPoint: pt];
    row = (int) [path row];

    if (recog == _swipeLeft) {
	_detailIndex = row;
    }
    else {
	_detailIndex = -1;
    }

    [_deleteButton removeFromSuperview];

    [_tableView reloadData];
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
    NSMutableArray *scanItemArray;

    if (style == UITableViewCellEditingStyleDelete) {
	row = [path row];
	scanItemArray = [_topEdit scanItems];
	[scanItemArray removeObjectAtIndex: row];

	/* and mark that we've made changes so that we'll randomize and reload 
	 * song list when Done is pressed.
	 */
	_topEdit.changesMade = YES;

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
    return (NSInteger) [[_topEdit scanItems] count];
}

- (UITableViewCell *) tableView: (UITableView *) tview cellForRowAtIndexPath: (NSIndexPath *)path
{

    unsigned long ix;
    UITableViewCell *cell;
    NSMutableArray *scanItemArray;
    MFANScanItem *scan;
    NSString *suffix;
    long mediaCount;
    UIColor *textColor;
    UILabel *tlabel;

    ix = [path row];
    
    scanItemArray = [_topEdit scanItems];
    if (ix >= [scanItemArray count])
	return nil;

    scan = [scanItemArray objectAtIndex: ix];
    if (scan == nil)
	return nil;

    textColor = [MFANTopSettings textColor];

    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
				    reuseIdentifier: nil];
    if (scan.scanFlags == [MFANScanItem scanSong])
	suffix = @" [Song]";
    else if (scan.scanFlags == [MFANScanItem scanPlaylist])
	suffix = @" [Playlist]";
    else if (scan.scanFlags == [MFANScanItem scanAlbum])
	suffix = @" [Album]";
    else if (scan.scanFlags == [MFANScanItem scanArtist])
	suffix = @" [Artist]";
    else if (scan.scanFlags == [MFANScanItem scanChildAlbum])
	suffix = @" [Album w/Artist]";
    else if (scan.scanFlags == [MFANScanItem scanRadio])
	suffix = @" [Radio]";
    else if (scan.scanFlags == [MFANScanItem scanUpnpSong])
	suffix = @" [Song/Net]";
    else if (scan.scanFlags == [MFANScanItem scanUpnpArtist])
	suffix = @" [Artist/Net]";
    else if (scan.scanFlags == [MFANScanItem scanUpnpAlbum])
	suffix = @" [Album/Net]";
    else if (scan.scanFlags == [MFANScanItem scanUpnpGenre])
	suffix = @" [Genre/Net]";
    else
	suffix = @"[Podcast]";
    cell.textLabel.text = [[scan title] stringByAppendingString: suffix];
    cell.textLabel.textColor = textColor;
    cell.textLabel.font = [MFANTopSettings basicFontWithSize: 18];
    cell.textLabel.adjustsFontSizeToFitWidth = YES;

    if (scan.cloud)
	suffix = @"Cloud";
    else
	suffix = @"Local";
    if (scan.minStars == 1) {
	suffix = [suffix stringByAppendingString: @", 1 star"];
    }
    else if (scan.minStars > 0) {
	suffix = [suffix stringByAppendingFormat: @", %ld stars", scan.minStars];
    }

    mediaCount = scan.mediaCount;
    if (mediaCount > 0) {
	if (mediaCount > 1) {
	    suffix = [suffix stringByAppendingFormat: @", %ld songs", mediaCount];
	}
	else {
	    suffix = [suffix stringByAppendingString: @", 1 song"];
	}
    }

    cell.detailTextLabel.text = suffix;
    cell.detailTextLabel.textColor = textColor;

    /* this can't be used to turn off reorder control; use canMoveRowAtIndex to say
     * the cell can't move.
     */
    cell.showsReorderControl = YES;

    // cell.contentView.backgroundColor = [UIColor clearColor];
    // cell.backgroundView.backgroundColor = [UIColor clearColor];
    // cell.multipleSelectionBackgroundView.backgroundColor = [UIColor clearColor];
    // cell.selectedBackgroundView.backgroundColor = [UIColor clearColor];
    cell.backgroundColor = [UIColor clearColor];

    if (_detailIndex == ix) {
	CGFloat height;
	height = cell.contentView.frame.size.height;

	[_deleteButton removeFromSuperview];
	
	NSLog(@"HEIGHT is %f", height);
	_deleteButton.frame = CGRectMake(4*_localFrame.size.width/5,
					 0,
					 _localFrame.size.width/5,
					 height);
	tlabel = [_deleteButton titleLabel];
	[tlabel setFont: [MFANTopSettings basicFontWithSize: height * 0.4]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];

	[cell.contentView addSubview: _deleteButton];
    }

    return cell;
}

- (BOOL) tableView: (UITableView *) tview canMoveRowAtIndexPath: path
{
    long row;
    row = [path row];

    if (row == _detailIndex)
	return NO;
    else
	return YES;
}

- (void) tableView:(UITableView *)tview
moveRowAtIndexPath:(NSIndexPath *) fromPath
       toIndexPath:(NSIndexPath *) toPath
{
    long fromRow = [fromPath row];
    long toRow = [toPath row];
    id fromOb;
    NSMutableArray *scanItemArray;

    if (fromRow != toRow) {
	scanItemArray = [_topEdit scanItems];

	fromOb = [scanItemArray objectAtIndex: fromRow];
	[scanItemArray removeObjectAtIndex: fromRow];
	if (fromRow > toRow) {
	    /* moving earlier, so slide everything else down one */
	    [scanItemArray insertObject: fromOb atIndex: toRow];
	}
	else {
	    /* looks like toRow has already been adjusted to take into account
	     * that item at fromRow has been removed.
	     */
	    [scanItemArray insertObject: fromOb atIndex: toRow];
	}
	_topEdit.changesMade = YES;
    }

    [tview reloadData];
}

- (UITableViewCellEditingStyle) tableView: (UITableView *) tview
	editingStyleForRowAtIndexPath: (NSIndexPath *) path
{
    return UITableViewCellEditingStyleNone;
}

- (BOOL)tableView:(UITableView *)tableview
shouldIndentWhileEditingRowAtIndexPath:(NSIndexPath *)indexPath {
    return NO;
}
@end /* MFANListEdit */

@implementation MFANTopEdit {
    MFANChannelType _channelType;	/* type of channel this defines */
    NSMutableArray *_scanItems;		/* array of MFANScanItems */
    MFANIconButton *_cancelButton;	/* cancel button for entire edit */
    MFANIconButton *_doneButton;	/* done button for entire edit */
    MFANIconButton *_clearButton;	/* clear button for entire edit */
    MFANIconButton *_playlistButton;	/* button to select a playlist */
    MFANIconButton *_albumButton;	/* button to select an album */
    MFANIconButton *_artistButton;	/* button to select an artist */
    MFANIconButton *_songButton;	/* button to select a song */
    MFANIconButton *_podcastButton;	/* button to select a podcast */
    MFANIconButton *_addButton;		/* button to popup media choice */
    MFANIconButton *_refreshButton;	/* button to refresh media */
    MFANIconButton *_resetButton;	/* button to reset media */
    MFANIconButton *_upnpButton;	/* button for configuring UPNP servers */
    MFANTopLevel *_topLevel; 
    MFANViewController *_viewCont;	/* back ptr to our view controller */
    MFANListEdit *_listEdit;		/* to supply data to _listTableView */
    MFANAddChoice *_addChoice;		/* media chooser */
    BOOL _addChoiceVisible;		/* de-bounce making addChoice visible */
    CGRect _appFrame;			/* frame for edit application */
    CGFloat _appHMargin;
    CGFloat _appVMargin;
    CGFloat _buttonHeight;
    CGFloat _buttonWidth;
    CGFloat _buttonMargin;
    NSMutableArray *_popEditStack;	/* of MFANPopEdit objects */
    MFANPopEdit *_popEdit;		/* top of popEdit stack, or nil */
    MFANRadioConsole *_radioConsole;
    MFANPlayContext *_playContext;
    NSMutableSet *_subviews;
    int _alertIndex;
    int _keepFilter;
    id _bridgeObj;
    SEL _bridgeSel;
}

- (id)initWithFrame:(CGRect)frame
	channelType: (MFANChannelType) channelType
	      level: (MFANTopLevel *) topLevel
	    console: (MFANRadioConsole *) console
     viewController: (MFANViewController *) viewCont
{
    CGRect tframe;
    UIColor *buttonColor;
    CGFloat nextButtonX;
    CGFloat nextButtonY;
    CGFloat consoleHeight;
    CGFloat defaultHue = 0.55;
    CGFloat defaultSat = 0.7;
    int nbuttons = 4;
    CGFloat buttonSkip;

    self = [super initWithFrame:frame];
    if (self) {
        // Initialization code
	_keepFilter = 0;
	_viewCont = viewCont;
	_channelType = channelType;
	_topLevel = topLevel;
	_scanItems = [[NSMutableArray alloc] init];
	_appVMargin = 20.0;
	_appHMargin = 2.0;
	_buttonHeight = 64.0;
	_buttonMargin = 5.0;
	_radioConsole = console;
	_popEditStack = [[NSMutableArray alloc] init];
	consoleHeight = 0;
	_subviews = [NSMutableSet setWithCapacity: 10];
	[self setBackgroundColor: [UIColor whiteColor]];
	
	_appFrame = frame;
	_appFrame.origin.x += _appHMargin;
	_appFrame.origin.y += _appVMargin;
	_appFrame.size.width -= 2 * _appHMargin;
	_appFrame.size.height -= _appVMargin;

	[self setBackgroundColor: [UIColor whiteColor]];

	// float col1of1 = _appFrame.origin.x + _appFrame.size.width/2 - _buttonWidth/2;

	/* how to center 2 buttons on screen */
	// float col1of2 = _appFrame.origin.x + _appFrame.size.width/4 - _buttonWidth/2;
	// float col2of2 = _appFrame.origin.x + 3*_appFrame.size.width/4 - _buttonWidth/2;

	/* how to center 3 buttons on screen -- currently unused */
	_buttonWidth = _buttonHeight;

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
				       _appFrame.size.height - _buttonHeight);
	nextButtonY = _appFrame.origin.y + _appFrame.size.height - _buttonHeight;
	buttonSkip = _appFrame.size.width / (nbuttons+1);
	nextButtonX = buttonSkip - _buttonWidth/2;

	_listEdit = [[MFANListEdit alloc] initWithParent: self frame: textFrame];

	[_subviews addObject: [_listEdit tableView]];

	buttonColor = [UIColor colorWithHue: defaultHue
			       saturation: defaultSat
			       brightness: 1.0
			       alpha: 1.0];

	_addChoiceVisible = NO;

	tframe.origin.x = nextButtonX;
	tframe.origin.y = nextButtonY;
	tframe.size.width = _buttonWidth;
	tframe.size.height = _buttonHeight;
	_addButton = [[MFANIconButton alloc] initWithFrame: tframe
					     title:@"+Sources"
					     color: buttonColor
					     file: @"icon-add.png"];
	[_subviews addObject: _addButton];
	[self addSubview: _addButton];
	[_addButton addCallback: self
		    withAction: @selector(addPressed:withData:)];
	nextButtonX += buttonSkip;

	tframe.origin.x = nextButtonX;
	tframe.origin.y = nextButtonY;
	tframe.size.width = _buttonWidth;
	tframe.size.height = _buttonHeight;
	_upnpButton = [[MFANIconButton alloc] initWithFrame: tframe
					      title: @"+UPNP"
					      color: buttonColor
					      file: @"icon-upnp.png"];
	[_subviews addObject: _upnpButton];
	[self addSubview: _upnpButton];
	[_upnpButton addCallback: self
		     withAction: @selector(upnpPressed:withData:)];
	nextButtonX += buttonSkip;

	/* now layout the clear/done/cancel buttons */
	tframe.origin.x = nextButtonX;
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
	nextButtonX += buttonSkip;

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
	nextButtonX += buttonSkip;
    }
    return self;
}

- (void) setBridgeCallback: (id) obj withAction: (SEL) callback
{
    _bridgeObj = obj;
    _bridgeSel = callback;
}

- (NSMutableArray *) scanItems
{
    return _scanItems;
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

/* called after a popup edit view has finished */
- (void) popFinished: (id) sender withData: (NSNumber *) number
{
    long count;

    [_popEditStack removeLastObject];
    [_popEdit removeFromSuperview];

    count = [_popEditStack count];
    if (count == 0) {
	_popEdit = nil;
	[self restoreSubviews];
	[_listEdit reloadData];
    }
    else {
	_popEdit = [_popEditStack objectAtIndex: count-1];
	[self addSubview: _popEdit];
    }
}

/* called after a popup edit view has finished */
- (void) bridgeFinished: (id) sender withData: (NSNumber *) number
{
    long count;

    [_popEditStack removeLastObject];
    [_popEdit removeFromSuperview];

    count = [_popEditStack count];
    if (count > 0) {
	_popEdit = [_popEditStack objectAtIndex: count-1];
	[self addSubview: _popEdit];
	return;
    }

    /* this call both installs the updated scan item array, and
     * obtains the associated media items and installs it in the play
     * context's setList.
     */
    [self saveEdits];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    if (_bridgeObj != nil) {
	[_bridgeObj performSelector: _bridgeSel withObject: nil];
	_bridgeObj = nil;
    }
#pragma clang diagnostic pop
}

/* call this instead of pushPopupMedia to transparently get to a media
 * add menu.
 */
- (void) bridgePopupMedia: (id<MFANMediaSel>) media
{
    MFANPopEdit *popEdit;
    CGRect tframe;

    _addChoiceVisible = NO;

    /* if media is nil, we were called from cancel being pressed.  Otherwise, make sure
     * we match up with the type of media (RSS feed or library media) already there, so that
     * the appropriate refresh code can be run.
     */
    if ( media != nil) {
	if( [_scanItems count] > 0 &&
	    [MFANSetList arrayHasRssItems: _scanItems] != [media hasRssItems]) {
	    MFANWarn *warn = [[MFANWarn alloc] initWithTitle:@"Mixing RSS and Library items"
					       message:@"Don't mix library and RSS feed sources in same channel"
					       secs: 2.0];
	    warn = nil;
	    return;
	}
    }

    if ([_popEditStack count] > 0) {
	[[_popEditStack lastObject] removeFromSuperview];
    }

    if (media != nil) {
	tframe = _appFrame;
	popEdit = [[MFANPopEdit alloc] initWithFrame: tframe
					  scanHolder: self
					    mediaSel: media
				      viewController: _viewCont];
	[popEdit addCallback: self
		 finishedSel: @selector(bridgeFinished:withData:)
		 nextSel: @selector(bridgePopupMedia:)];
	_popEdit = popEdit;
	[_popEditStack addObject: popEdit];

	[self addSubview: popEdit];
    }
    else {
	/* cancelled the selection of genre, etc to add */
	/* switch all the way back to main screen */
	[_viewCont switchToAppByName: @"list"];
    }
}

- (void) bridgeAddPressed
{
    CGRect tframe;
    NSMutableArray *labels;
    NSMutableArray *contexts;
    NSString *tstring;
    id<MFANMediaSel> setList;

    if (!_addChoiceVisible) {
	_addChoiceVisible = YES;

	[self hideSubviews];

	labels = [[NSMutableArray alloc] init];
	contexts = [[NSMutableArray alloc] init];

	tstring = @"+Artists\niTunes";
	setList = [MFANSetList artistsSel];
	[labels addObject: tstring];
	[contexts addObject: setList];

	tstring = @"+Artists\nUPNP";
	setList = [MFANSetList upnpArtistsSel];
	[labels addObject: tstring];
	[contexts addObject: setList];

	tstring = @"+Albums\niTunes";
	setList = [MFANSetList albumsSel];
	[labels addObject: tstring];
	[contexts addObject: setList];

	tstring = @"+Albums\nUPNP";
	setList = [MFANSetList upnpAlbumsSel];
	[labels addObject: tstring];
	[contexts addObject: setList];

	tstring = @"+Songs\niTunes";
	setList = [MFANSetList songsSel];
	[labels addObject: tstring];
	[contexts addObject: setList];

	tstring = @"+Songs\nUPNP";
	setList = [MFANSetList upnpSongsSel];
	[labels addObject: tstring];
	[contexts addObject: setList];

	tstring = @"+Genres\niTunes";
	setList = [MFANSetList genresSel];
	[labels addObject: tstring];
	[contexts addObject: setList];

	tstring = @"+Genres\nUPNP";
	setList = [MFANSetList upnpGenresSel];
	[labels addObject: tstring];
	[contexts addObject: setList];

	tstring = @"+Playlists";
	setList = [MFANSetList playlistsSel];
	[labels addObject: tstring];
	[contexts addObject: setList];

	tstring = @"+Recordings";
	setList = [MFANSetList recordingsSel];
	[labels addObject: tstring];
	[contexts addObject: setList];

	tframe = _appFrame;
	_addChoice = [[MFANAddChoice alloc] initWithFrame: tframe
					    labels: (NSArray *)labels
					    contexts: contexts
					    viewController: _viewCont];
	[self addSubview: _addChoice];
	[_addChoice addCallback: self
		    withAction: @selector(bridgePopupMedia:)];
    }
}

- (void) pushPopupMedia: (id<MFANMediaSel>) media
{
    MFANPopEdit *popEdit;
    CGRect tframe;

    _addChoiceVisible = NO;

    /* if media is nil, we were called from cancel being pressed.  Otherwise, make sure
     * we match up with the type of media (RSS feed or library media) already there, so that
     * the appropriate refresh code can be run.
     */
    if ( media != nil) {
	if( [_scanItems count] > 0 &&
	    [MFANSetList arrayHasRssItems: _scanItems] != [media hasRssItems]) {
	    MFANWarn *warn = [[MFANWarn alloc] initWithTitle:@"Mixing RSS and Library items"
					       message:@"Don't mix library and RSS feed sources in same channel"
					       secs: 2.0];
	    warn = nil;
	    return;
	}
    }

    if ([_popEditStack count] == 0) {
	[_addChoice removeFromSuperview];
    }
    else {
	[[_popEditStack lastObject] removeFromSuperview];
    }

    if (media != nil) {
	tframe = _appFrame;
	popEdit = [[MFANPopEdit alloc] initWithFrame: tframe
					  scanHolder: self
					    mediaSel: media
				      viewController: _viewCont];
	[popEdit addCallback: self
		 finishedSel: @selector(popFinished:withData:)
		 nextSel: @selector(pushPopupMedia:)];
	_popEdit = popEdit;
	[_popEditStack addObject: popEdit];

	[self addSubview: popEdit];
    }
    else {
	/* cancelled the selection of genre, etc to add */
	[self restoreSubviews];

	if (![_playContext hasAnyItems]) {
	    /* switch all the way back to main screen */
	    [_viewCont switchToAppByName: @"main"];
	}
    }
}

- (void) addPressed: (id) sender withData: (NSNumber *) number
{
    CGRect tframe;
    NSMutableArray *labels;
    NSMutableArray *contexts;
    NSString *tstring;
    id<MFANMediaSel> setList;
    MFANChannelType channelType;

    if (!_addChoiceVisible) {
	channelType = [_viewCont channelType];

	_addChoiceVisible = YES;

	[self hideSubviews];

	if (channelType == MFANChannelMusic) {
	    labels = [[NSMutableArray alloc] init];
	    contexts = [[NSMutableArray alloc] init];

	    tstring = @"+Artists\niTunes";
	    setList = [MFANSetList artistsSel];
	    [labels addObject: tstring];
	    [contexts addObject: setList];

	    tstring = @"+Artists\nUPNP";
	    setList = [MFANSetList upnpArtistsSel];
	    [labels addObject: tstring];
	    [contexts addObject: setList];

	    tstring = @"+Albums\niTunes";
	    setList = [MFANSetList albumsSel];
	    [labels addObject: tstring];
	    [contexts addObject: setList];

	    tstring = @"+Albums\nUPNP";
	    setList = [MFANSetList upnpAlbumsSel];
	    [labels addObject: tstring];
	    [contexts addObject: setList];

	    tstring = @"+Songs\niTunes";
	    setList = [MFANSetList songsSel];
	    [labels addObject: tstring];
	    [contexts addObject: setList];

	    tstring = @"+Songs\nUPNP";
	    setList = [MFANSetList upnpSongsSel];
	    [labels addObject: tstring];
	    [contexts addObject: setList];

	    tstring = @"+Genres\niTunes";
	    setList = [MFANSetList genresSel];
	    [labels addObject: tstring];
	    [contexts addObject: setList];

	    tstring = @"+Genres\nUPNP";
	    setList = [MFANSetList upnpGenresSel];
	    [labels addObject: tstring];
	    [contexts addObject: setList];

	    tstring = @"+Playlists";
	    setList = [MFANSetList playlistsSel];
	    [labels addObject: tstring];
	    [contexts addObject: setList];

	    tstring = @"+Recordings";
	    setList = [MFANSetList recordingsSel];
	    [labels addObject: tstring];
	    [contexts addObject: setList];

	    tframe = _appFrame;
	    _addChoice = [[MFANAddChoice alloc] initWithFrame: tframe
						labels: (NSArray *)labels
						contexts: contexts
						viewController: _viewCont];
	    [self addSubview: _addChoice];
	    [_addChoice addCallback: self
			withAction: @selector(pushPopupMedia:)];
	} /* music channel */
	else if (channelType == MFANChannelPodcast) {
	    [self pushPopupMedia: [MFANSetList podcastsSel]];
	}
	else if (channelType == MFANChannelRadio) {
	    [self pushPopupMedia: [MFANSetList radioSel]];
	}
    }
}

- (void) upnpPressed: (id) sender withData: (NSNumber *) number
{
    MFANChannelType channelType;
    channelType = [_viewCont channelType];

    if (channelType == MFANChannelMusic)
	[_viewCont switchToAppByName: @"upnp"];
    else {
	MFANWarn *warn = [[MFANWarn alloc] initWithTitle:@"UPnP servers for music channels only"
					   message:@"UPnP servers are for music only"
					   secs: 2.0];
	warn = nil;
    }
}

- (void) albumPressed: (id) sender withData: (NSNumber *) number
{
    [self pushPopupMedia: [MFANSetList albumsSel]];
}

- (void) plPressed: (id) sender withData: (NSNumber *) number
{
    [self pushPopupMedia: [MFANSetList playlistsSel]];
}

- (void) artistPressed: (id) sender withData: (NSNumber *) number
{
    [self pushPopupMedia: [MFANSetList artistsSel]];
}

- (void) songPressed: (id) sender withData: (NSNumber *) number
{
    [self pushPopupMedia: [MFANSetList songsSel]];
}

- (void) podcastPressed: (id) sender withData: (NSNumber *) number
{
    [self pushPopupMedia: [MFANSetList podcastsSel]];
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
    if (_alertIndex == 0) {
	/* did a cancel */
	return;
    }

    /* otherwise, clear all entries */
    [_scanItems removeAllObjects];

    _changesMade = YES;

    [_listEdit reloadData];
} 

- (void) alertView: (UIAlertView *)aview didDismissWithButtonIndex: (NSInteger) ix
{
    _alertIndex = (int) ix;
    [self clearAfterAlert];
}

- (void) pruneScanItems: (int) preserveType
{
    MFANScanItem *scanItem;
    NSMutableArray *newScanItems;

    newScanItems = [[NSMutableArray alloc] init];
    for (scanItem in _scanItems) {
	if (preserveType == MFANTopEdit_keepMusic) {
	    if ( !((scanItem.scanFlags & [MFANScanItem scanRadio]) ||
		   (scanItem.scanFlags & [MFANScanItem scanPodcast]))) {
		[newScanItems addObject: scanItem];
	    }
	}
	else if (preserveType == MFANTopEdit_keepPodcast) {
	    if ( scanItem.scanFlags & [MFANScanItem scanPodcast]) {
		[newScanItems addObject: scanItem];
	    }
	}
	else if (preserveType == MFANTopEdit_keepRadio) {
	    if ( scanItem.scanFlags & [MFANScanItem scanRadio]) {
		[newScanItems addObject: scanItem];
	    }
	}
    }

    /* and install */
    _scanItems = newScanItems;

    _changesMade = YES;
    [_listEdit reloadData];
}

/* this is a hack that let's us insert a filter at the very next
 * activation of the edit view.  This way we can get the filtering
 * done after activateTop determines the scanItems list, but before it
 * determines if it needs to go directly to the add page, or show the
 * edit page.
 */
- (void) setScanFilter: (int) keepType
{
    _keepFilter = keepType;
}

- (void) saveEdits
{
    MFANSetList *setList;

    if (!_changesMade) {
	return;
    }

    /* otherwise, save the state of the context, and save the file as well */
    [_playContext setQueryInfo: _scanItems];	/* changes setList's itemArray */

    /* try reconnecting detached guys in the new setlist */
    setList = [_playContext setList];
    [[_playContext download] checkDownloadedArray: [setList itemArray]];

    [_playContext saveListToFile];
}

- (void) nextView
{
    if ([_playContext hasAnyItems])
	[_viewCont switchToAppByName: @"list"];
    else
	[_viewCont switchToAppByName: @"main"];
}

- (void) helpPressed: (id) sender withData: (NSNumber *) number
{
    /* even if we're canceled, someone may have changed the load/unload status
     * of some of our items.  So, reevaluate the real array.
     */
    MFANPopHelp *popHelp;

    popHelp = [[MFANPopHelp alloc] initWithFrame: _appFrame
				   helpFile: @"help-edit"
				   parentView: self
				   warningFlag: 0];
    [popHelp show];
}

- (void) donePressed: (id) sender withData: (NSNumber *) number
{
    MFANWarn *warn;

    if (!_changesMade) {
	[self nextView];
	return;
    }

    [self saveEdits];


    if (_playContext == [_topLevel currentContext]) {
	/* if we changed contents of the current context, start playing it now */
	[_topLevel playCurrent];
    }

    if ( [_playContext hasPodcastItems] &&
	 ![MFANTopSettings autoDownload]) {
	warn = [[MFANWarn alloc]
		   initWithTitle: @"Download podcasts"
		   message: @"Select podcasts for download"
		   secs: 2.0];
    }

    [self nextView];
}

-(void) deactivateTop
{
    return;
}

-(void) activateTop
{
    /* use a fake warning flag to indicate that we've never run
     * before, so we can reset the library state on first run; this is
     * required since the 'OK to use iTunes library' enables the
     * library after we've first done the setup.
     */
    if (!([MFANTopSettings warningFlags] & MFANTopSettings_initialResync)) {
	[MFANSetList doSetup: nil force:YES];
	[MFANTopSettings setWarningFlag: MFANTopSettings_initialResync];
	NSLog(@"**doing initial library resync");
    }

    /* see if we need to reload the global setlist info */
    [MFANSetList checkLibrary];

    _playContext = [_topLevel currentContext];

    /* haven't made changes yet */
    _changesMade = NO;

    if (_playContext != nil) {
	_scanItems = [_playContext queryInfo];
	[_listEdit reloadData];
    }

    _addChoiceVisible = NO;
    [_addChoice removeFromSuperview];
    [self restoreSubviews];

    if (_keepFilter != 0) {
	[self pruneScanItems: _keepFilter];
	_keepFilter = 0;
    }

    if (_bridgeObj == nil && [_scanItems count] == 0) {
	[self addPressed: nil withData: nil];
    }
}

- (void) addScanItem: (MFANScanItem *) scan
    
{
    [_scanItems addObject: scan];
    _changesMade = YES;
}

@end /* MFANTopEdit */
