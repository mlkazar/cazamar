//
//  MFANTopList.m
//  DJ To Go
//
//  Created by Michael Kazar on 8/10/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANAlert.h"
#import "MFANTopList.h"
#import "MFANTopLevel.h"
#import "MFANTopEdit.h"
#import "MFANSetList.h"
#import "MFANPlayContext.h"
#import "MFANCGUtil.h"
#import "MFANPlayerView.h"
#import "MFANDownload.h"
#import "MFANTopSettings.h"
#import "MFANWarn.h"
#import "MFANPopHelp.h"
#import "MFANPopStatus.h"
#import "MFANCoreButton.h"
#import "MFANIconButton.h"
#import "MFANTopDoc.h"

@implementation MFANTopList {
    MFANTopLevel *_topLevel;
    MFANRadioConsole *_radioConsole;
    MFANViewController *_viewCont;
    MFANChannelType _channelType;

    MFANPlayContext *_playContext;
    UITableView *_tableView;
    MFANSetList *_setList;
    NSMutableArray *_itemArray; /* of MFANMediaItems */
    int _detailIndex;
    CGFloat _appHMargin;
    CGFloat _appVMargin;
    CGRect _appFrame;
    CGFloat _buttonWidth;
    CGFloat _buttonHeight;
    long _currentIndex;
    long _originalIndex;	/* when screen first pops up */
    MFANIconButton *_doneButton;
    MFANIconButton *_cancelButton;
    MFANIconButton *_delPrevButton;
    MFANIconButton *_refreshButton;
    MFANIconButton *_randomizeButton;
    MFANIconButton *_resetButton;
    MFANIconButton *_editButton;
    MFANIconButton *_loadAllButton;
    BOOL _changedArray;
    BOOL _changedSelection;
    UIImage *_defaultImage;
    UIImage *_scaledDefaultImage;
    UISwipeGestureRecognizer *_swipeLeft;
    UISwipeGestureRecognizer *_swipeRight;
    CGFloat _rowHeight;
    CGFloat _imageHeight;
    MFANMediaItem *_alertItem;
    int _alertRow;
    MFANLabel *_luButton;
    MFANLabel *_exButton;
    MFANLabel *_deleteButton;
    MFANLabel *_infoButton;
    NSTimer *_detailTimer;
    MFANPopHelp *_popHelp;
    MFANPopStatus *_popStatus;
    int _lastDownloadCount;
    MFANMediaItem *_originalMFANItem;
    MFANRenamePrompt *_renamePrompt;
    BOOL _refreshRss;
}

- (void) activateTop
{
    _playContext = [_topLevel currentContext];
    _setList = [_playContext setList];
    _itemArray = [NSMutableArray arrayWithArray: [_setList itemArray]];
    if ([_itemArray count] == 0) {
	[_viewCont switchToAppByName: @"menu"];
	return;
    }
    _originalIndex = _currentIndex = (int) [_playContext getCurrentIndex];
    _originalMFANItem = [_setList itemWithIndex: _originalIndex];
    [[_playContext download] checkDownloadedArray: _itemArray];

    [_tableView reloadData];

    if (_currentIndex < [_itemArray count]) {
	[_tableView
	    scrollToRowAtIndexPath: [NSIndexPath indexPathForRow: _currentIndex inSection: 0]
	    atScrollPosition: UITableViewScrollPositionMiddle
	    animated: YES];
    }
}

- (void) deactivateTop
{
    /* make life easier for ref count gc */
    _playContext = nil;
    _setList = nil;
    _itemArray = nil;
    if (_detailTimer) {
	[_detailTimer invalidate];
	_detailTimer = nil;
    }
}

- (void) tableView: (UITableView *) tview didSelectRowAtIndexPath: (NSIndexPath *) path
{
    long row;
    long oldRow;
    NSIndexPath *oldPath;
    NSMutableArray *array;

    oldPath = [NSIndexPath indexPathForRow: _currentIndex inSection: 0];
    row = [path row];
    oldRow = _currentIndex;

    _currentIndex = row;

    _changedSelection = YES;

    array = [[NSMutableArray alloc] init];
    [array addObject: path];
    if (row != oldRow)
	[array addObject: oldPath];
    [tview reloadRowsAtIndexPaths: array
	   withRowAnimation: UITableViewRowAnimationAutomatic];
}

- (UITableViewCellEditingStyle) tableView: (UITableView *) tview
	editingStyleForRowAtIndexPath: (NSIndexPath *) path
{
    return UITableViewCellEditingStyleNone;
}

- (void) tableView: (UITableView *) tview
commitEditingStyle: (UITableViewCellEditingStyle) style
 forRowAtIndexPath: (NSIndexPath *) path
{
    long row;

    if (style == UITableViewCellEditingStyleDelete) {
	row = [path row];
	[_itemArray removeObjectAtIndex: row];

	_changedArray = YES;

	/* what a sad, sad joke is iOS -- clowns broke deleting an
	 * item from a view from the swipe handler.
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

- (BOOL) tableView: (UITableView *) tview canEditRowAtIndexPath: (NSIndexPath *) path
{
    return YES;
}

- (BOOL)tableView:(UITableView *)tableview
shouldIndentWhileEditingRowAtIndexPath:(NSIndexPath *)indexPath {
    return NO;
}

- (NSInteger) tableView: (UITableView *)tview numberOfRowsInSection: (NSInteger) section
{
    return (NSInteger) [_itemArray count];
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

    _currentIndex = toRow;
    _changedSelection = YES;

    if (fromRow != toRow) {
	fromOb = [_itemArray objectAtIndex: fromRow];
	[_itemArray removeObjectAtIndex: fromRow];
	if (fromRow > toRow) {
	    /* moving earlier, so slide everything else down one */
	    [_itemArray insertObject: fromOb atIndex: toRow];
	}
	else {
	    /* looks like toRow has already been adjusted to take into account
	     * that item at fromRow has been removed.
	     */
	    [_itemArray insertObject: fromOb atIndex: toRow];
	}
	_changedArray = YES;
    }

    [tview reloadData];
}

- (UITableViewCell *) tableView: (UITableView *) tview cellForRowAtIndexPath: (NSIndexPath *)path
{

    unsigned long ix;
    UITableViewCell *cell;
    MPMediaItem *item;
    MFANMediaItem *mfanItem;
    NSString *titleString;
    NSString *artistString;
    NSString *detailString;
    NSString *albumString;
    NSString *podcastString;
    long mediaType;
    UIImage *artImage;
    CGSize imageSize;
    BOOL isPresent;
    BOOL isLoadable;
    int percentLoaded;
    BOOL displayImage = YES;

    ix = [path row];
    
    if (ix >= [_itemArray count])
	return nil;

    mfanItem = [_itemArray objectAtIndex: ix];
    item = [mfanItem item];
    if (item != nil) {
	imageSize.height = _imageHeight;
	imageSize.width = _imageHeight;
	artImage = mediaImageWithSize(item, imageSize, _scaledDefaultImage);

	mediaType = [[item valueForProperty:  MPMediaItemPropertyMediaType] integerValue];
	titleString = [item valueForProperty: MPMediaItemPropertyTitle];
	podcastString = [item valueForProperty: MPMediaItemPropertyPodcastTitle];
	if (mediaType & MPMediaTypePodcast) {
	    NSLog(@"!old podcast detail!");
	    detailString = [NSString stringWithFormat: @"%d - %@",
				     (int) ix+1, podcastString];
	}
	else {
	    artistString = [item valueForProperty: MPMediaItemPropertyArtist];
	    albumString = [item valueForProperty: MPMediaItemPropertyAlbumTitle];
	    detailString = [NSString stringWithFormat: @"%d - %@ / %@",
				     (int) (ix+1), artistString, albumString];
	}
    }
    else {
	/* URL only */
	titleString =  [mfanItem title];
	detailString = [NSString stringWithFormat: @"%d - %@", (int) ix+1, [mfanItem albumTitle]];
	artImage = [mfanItem artworkWithSize: _imageHeight];
    }

    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
					reuseIdentifier: nil];
    cell.textLabel.text = titleString;
    cell.textLabel.font = [MFANTopSettings basicFontWithSize: 11];
    cell.textLabel.adjustsFontSizeToFitWidth = NO;

    /* if the MPMediaItem entry is null, then we either have a podcast
     * (downloaded or not), a recorded radio station, a real radio
     * station, or a downloaded UPnP song.  If the _url field is 
     * not "http:" or "https:", then the file is downloadable (it may not 
     * be downloaded yet if the string is empty or null).
     */
    if (mfanItem.item == nil && ![mfanItem isWebStream]) {
	static float dim = 0.25;
	displayImage = NO;
	isLoadable = YES;
	if ([[_playContext download] isLoading: mfanItem]) {
	    cell.textLabel.textColor = [UIColor colorWithRed: dim green: dim blue: 1.0 alpha: 1.0];
	    isPresent = NO;
	}
	else {
	    if (![mfanItem isPlayable]) {
		cell.textLabel.textColor = [UIColor colorWithRed: 1.0
						    green: dim
						    blue:dim
						    alpha: 1.0];
	    }
	    else {
		cell.textLabel.textColor = [UIColor colorWithRed: 0.0
						    green: 0.8
						    blue: 0.0
						    alpha: 1.0];
	    }
	    if ([mfanItem localUrl].length == 0)
		isPresent = NO;
	    else
		isPresent = YES;
	}
    }
    else {
	/* no remote URL, just draw in white, since it isn't downloadable */
	isLoadable = NO;
	isPresent = YES;
	cell.textLabel.textColor = [MFANTopSettings textColor];
    }

    cell.detailTextLabel.text = detailString;
    cell.detailTextLabel.textColor = [MFANTopSettings textColor];

    if (displayImage)
	[[cell imageView] setImage: artImage];

    cell.shouldIndentWhileEditing = NO;

    if (_detailIndex == ix) {
	/* show detail buttons */
	[_infoButton removeFromSuperview];
	_infoButton.frame = CGRectMake(4*_appFrame.size.width/6, 0,
					 _appFrame.size.width/6, _rowHeight);
	[cell.contentView addSubview: _infoButton];

	[_deleteButton removeFromSuperview];
	_deleteButton.frame = CGRectMake(5*_appFrame.size.width/6, 0,
					 _appFrame.size.width/6, _rowHeight);
	[cell.contentView addSubview: _deleteButton];

	[_luButton removeFromSuperview];
	if (isLoadable) {
	    _luButton.frame = CGRectMake(3*_appFrame.size.width/6, 0,
					 _appFrame.size.width/6, _rowHeight);
	    [cell.contentView addSubview: _luButton];
	    percentLoaded = [[_playContext download] percentForItem: mfanItem];
	    if (percentLoaded >= 0) {
		[_luButton setTitle: [NSString stringWithFormat: @"%02d%%", percentLoaded]
			   forState: UIControlStateNormal];
	    }
	    else if (isPresent)
		[_luButton setTitle: @"Unload" forState: UIControlStateNormal];
	    else
		[_luButton setTitle: @"DnLoad" forState: UIControlStateNormal];

	    [_exButton removeFromSuperview];
	    _exButton.frame = CGRectMake(2*_appFrame.size.width/6, 0,
					 _appFrame.size.width/6, _rowHeight);
	    [cell.contentView addSubview: _exButton];

	}
	[self setBackgroundColors];
    }

    /* this can't be used to turn off reorder control; use canMoveRowAtIndex to say
     * the cell can't move.
     */
    cell.showsReorderControl = YES;

    if (ix == _currentIndex) {
	cell.backgroundColor = [UIColor orangeColor];
    }
    else {
	cell.backgroundColor = [UIColor clearColor];
    }

    return cell;
}

- (id)initWithFrame:(CGRect)frame
	channelType: (MFANChannelType) channelType
	      level: (MFANTopLevel *) level
	    console: (MFANRadioConsole *) console
     viewController: (MFANViewController *) viewCont
{
    CGRect tframe;
    CGRect tableFrame;
    CGFloat nextButtonX;
    CGFloat nextButtonY;
    CGFloat buttonGap;
    UILabel *tlabel;
    int nbuttons;

    self = [super initWithFrame: frame];

    if(self) {
        // Initialization code
	_channelType = channelType;
	_popStatus = nil;
	[self setBackgroundColor: [UIColor whiteColor]];

	_detailIndex = -1;
	_lastDownloadCount = 0;
	_changedArray = NO;
	_changedSelection = NO;

	_topLevel = level;
	_radioConsole = console;
	_viewCont = viewCont; 

	_appVMargin = 20.0;
	_appHMargin = 2.0;
	_rowHeight = 54;
	_imageHeight = _rowHeight * 0.85;

	_appFrame = frame;
	_appFrame.origin.x += _appHMargin;
	_appFrame.origin.y += _appVMargin;
	_appFrame.size.width -= 2 * _appHMargin;
	_appFrame.size.height -= _appVMargin + _appHMargin;

	_buttonHeight = 40;
	_buttonWidth = _buttonHeight;

	_infoButton = [[MFANLabel alloc] initWithTarget: self
					 selector:@selector(infoRow:withData:)];
	[_infoButton setTitle: @"Details" forState: UIControlStateNormal];
	tlabel = [_infoButton titleLabel];
	[tlabel setFont: [MFANTopSettings  basicFontWithSize: _rowHeight * 0.4]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];

	_deleteButton = [[MFANLabel alloc] initWithTarget: self
					   selector:@selector(deleteRow:withData:)];
	[_deleteButton setTitle: @"Delete" forState: UIControlStateNormal];
	tlabel = [_deleteButton titleLabel];
	[tlabel setFont: [MFANTopSettings basicFontWithSize: _rowHeight * 0.4]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];

	_luButton =  [[MFANLabel alloc] initWithTarget: self
					selector:@selector(luRow:withData:)];
	tlabel = [_luButton titleLabel];
	[tlabel setFont: [MFANTopSettings basicFontWithSize: _rowHeight * 0.4]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];

	_exButton =  [[MFANLabel alloc] initWithTarget: self
					selector:@selector(exRow:withData:)];
	[_exButton setTitle: @"Export" forState: UIControlStateNormal];
	tlabel = [_exButton titleLabel];
	[tlabel setFont: [MFANTopSettings basicFontWithSize: _rowHeight * 0.4]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];

	[self setBackgroundColors];

	/* reserve space for the buttons, based on the type of
	 * channel.
	 *
	 * For music, we have Sources, Refresh, Randomize, Cancel and Done (4).
	 *
	 * For radio, we have Sources, Cancel and Done (3).
	 *
	 * For podcasts, we have Sources, Refresh, Del previous, Load
	 * all, Cancel and Done (6).
	 */

	tableFrame = _appFrame;
	if (_channelType == MFANChannelRadio)
	    nbuttons = 3;
	else if (_channelType == MFANChannelPodcast)
	    nbuttons = 7;
	else {
	    /* default, including for music channels */
	    nbuttons = 6;
	}
	tableFrame.size.height -= _buttonHeight;

	_tableView = [[UITableView alloc] initWithFrame: tableFrame
					  style:UITableViewStylePlain];
	[_tableView setAllowsMultipleSelectionDuringEditing: NO];
	[_tableView setRowHeight: _rowHeight];
	_tableView.separatorStyle = UITableViewCellSeparatorStyleNone;
	_tableView.backgroundColor = [UIColor clearColor];

	[_tableView setDelegate: self];
	[_tableView setDataSource: self];

	[_tableView setEditing: YES animated: NO];
	_tableView.allowsSelectionDuringEditing = YES;

	_swipeRight = [[UISwipeGestureRecognizer alloc]
			  initWithTarget:self action:@selector(handleSwipeFrom:)];
	_swipeRight.direction = UISwipeGestureRecognizerDirectionRight;
	[_tableView addGestureRecognizer: _swipeRight];

	_swipeLeft = [[UISwipeGestureRecognizer alloc]
			 initWithTarget:self action:@selector(handleSwipeFrom:)];
	_swipeLeft.direction = UISwipeGestureRecognizerDirectionLeft;
	[_tableView addGestureRecognizer: _swipeLeft];

	[self addSubview: _tableView];

	nextButtonY = tableFrame.origin.y + tableFrame.size.height;
	if (nbuttons < 3) {
	    buttonGap = _appFrame.size.width / (nbuttons+1);
	    nextButtonX = _appFrame.origin.x + buttonGap - _buttonWidth/2;
	}
	else {
	    buttonGap = (_appFrame.size.width - _buttonWidth) / (nbuttons-1);
	    nextButtonX = _appFrame.origin.x;
	}
	
	if (_channelType != MFANChannelRadio) {
	    /* now layout the clear/done/cancel buttons */
	    tframe.origin.x = nextButtonX;
	    tframe.origin.y = nextButtonY;
	    tframe.size.width = _buttonWidth;
	    tframe.size.height = _buttonHeight;
	    _editButton = [[MFANIconButton alloc] initWithFrame: tframe
						  title: @"+Media"
						  color: [MFANTopSettings baseColor]
						  file: @"icon-add.png"];
	    [self addSubview: _editButton];
	    [_editButton addCallback: self
			 withAction: @selector(editPressed:)];
	    nextButtonX += buttonGap;

	    tframe.origin.x = nextButtonX;
	    tframe.origin.y = nextButtonY;
	    tframe.size.width = _buttonWidth;
	    tframe.size.height = _buttonHeight;
	    _refreshButton = [[MFANIconButton alloc] initWithFrame: tframe
						     title: @"Refresh"
						     color: [MFANTopSettings baseColor]
						     file: @"icon-refresh.png"];
	    [self addSubview: _refreshButton];
	    [_refreshButton addCallback: self
			    withAction: @selector(refreshPressed:)];
	    nextButtonX += buttonGap;
	}

	if (_channelType == MFANChannelMusic) {
	    tframe.origin.x = nextButtonX;
	    tframe.origin.y = nextButtonY;
	    tframe.size.width = _buttonWidth;
	    tframe.size.height = _buttonHeight;
	    _randomizeButton = [[MFANIconButton alloc] initWithFrame: tframe
						       title: @"Randomize"
						       color: [MFANTopSettings baseColor]
						       file: @"icon-randomize.png"];
	    [self addSubview: _randomizeButton];
	    [_randomizeButton addCallback: self
			      withAction: @selector(randomizePressed:)];
	    nextButtonX += buttonGap;
	}

	if (_channelType == MFANChannelPodcast) {
	    tframe.origin.x = nextButtonX;
	    tframe.origin.y = nextButtonY;
	    tframe.size.width = _buttonWidth;
	    tframe.size.height = _buttonHeight;
	    _delPrevButton = [[MFANIconButton alloc] initWithFrame: tframe
						     title: @"Delete Older"
						     color: [MFANTopSettings baseColor]
						     file: @"icon-delprev.png"];
	    [self addSubview: _delPrevButton];
	    [_delPrevButton addCallback: self
			    withAction: @selector(delPrevPressed:withData:)];
	    nextButtonX += buttonGap;

	    /* now layout the loadAll / load Art */
	    tframe.origin.x = nextButtonX;
	    tframe.origin.y = nextButtonY;
	    tframe.size.width = _buttonWidth;
	    tframe.size.height = _buttonHeight;
	    _loadAllButton = [[MFANIconButton alloc] initWithFrame: tframe
						     title: @"Dnload ALL"
						     color: [MFANTopSettings baseColor]
						     file: @"icon-loadall.png"];
	    [self addSubview: _loadAllButton];
	    [_loadAllButton addCallback: self
			    withAction: @selector(loadAllPressed:)];
	    nextButtonX += buttonGap;
	}

	/* now layout the clear/done/cancel buttons */
	tframe.origin.x = nextButtonX;
	tframe.origin.y = nextButtonY;
	tframe.size.width = _buttonWidth;
	tframe.size.height = _buttonHeight;
	_cancelButton = [[MFANIconButton alloc] initWithFrame: tframe
						title: @"Define"
						color: [MFANTopSettings baseColor]
						file: @"icon-definition.png"];
	[self addSubview: _cancelButton];
	[_cancelButton addCallback: self
		       withAction: @selector(definitionPressed:)];
	nextButtonX += buttonGap;

	/* now layout the clear/done/cancel buttons */
	tframe.origin.x = nextButtonX;
	tframe.origin.y = nextButtonY;
	tframe.size.width = _buttonWidth;
	tframe.size.height = _buttonHeight;
	_cancelButton = [[MFANIconButton alloc] initWithFrame: tframe
						title: @"Cancel"
						color: [MFANTopSettings baseColor]
						file: @"icon-cancel.png"];
	[self addSubview: _cancelButton];
	[_cancelButton addCallback: self
		       withAction: @selector(cancelPressed:)];
	nextButtonX += buttonGap;

	_refreshRss = NO;

	tframe.origin.x = nextButtonX;
	tframe.origin.y = nextButtonY;
	tframe.size.width = _buttonWidth;
	tframe.size.height = _buttonHeight;
	_doneButton = [[MFANIconButton alloc] initWithFrame: tframe
					      title: @"Done"
					      color: [MFANTopSettings baseColor]
					      file: @"icon-done.png"];
	[self addSubview: _doneButton];
	[_doneButton addCallback: self
		     withAction: @selector(donePressed:withData:)];
	nextButtonX += buttonGap;

	/* finally, layout popupHelp */
	_defaultImage = [UIImage imageNamed: @"record-152.png"];
	_scaledDefaultImage = resizeImage(_defaultImage, _imageHeight);
    }
    return self;
}

- (void) setBackgroundColors
{
    _luButton.backgroundColor = [UIColor colorWithRed: 0.0
					 green: 0.4
					 blue: 0.0
					 alpha: 1.0];
    _deleteButton.backgroundColor = [UIColor redColor];
    _infoButton.backgroundColor = [UIColor blackColor];
    _exButton.backgroundColor = [UIColor blueColor];
}

- (void) handleSwipeFrom: (UISwipeGestureRecognizer *)recog
{
    CGPoint pt;
    NSIndexPath *path;
    int row;
    MFANMediaItem *mfanItem;

    pt = [recog locationInView: _tableView];
    path = [_tableView indexPathForRowAtPoint: pt];
    row = (int) [path row];

    _alertRow = row;
    _alertItem = mfanItem = [_itemArray objectAtIndex: row];
    
    if (recog == _swipeLeft) {
	_detailIndex = row;
	if (_detailTimer == nil) {
	    _detailTimer = [NSTimer scheduledTimerWithTimeInterval: 1.0
				    target:self
				    selector:@selector(timerFired:)
				    userInfo:nil
				    repeats: YES];
	}
    }
    else {
	_detailIndex = -1;
	[_deleteButton removeFromSuperview];
	[_infoButton removeFromSuperview];
	[_luButton removeFromSuperview];
	[_exButton removeFromSuperview];
    }

    [_tableView reloadData];
}

- (void) infoRow: (id) j1 withData: (id) j2
{
    MFANMediaItem *mfanItem;
    NSMutableString *summary;

    long row = _detailIndex;

    mfanItem = [_itemArray objectAtIndex: row];

    if (mfanItem.details != nil) {
	MFANAlert *alert = [[MFANAlert alloc]
				 initWithTitle: @"Details"
				       message: mfanItem.details
				   buttonTitle: @"OK"];
	[alert show];
    }
    else {
	summary = [[NSMutableString alloc] init];
	if (mfanItem.title != nil) {
	    [summary setString: mfanItem.title];
	    [summary appendString: @"\n"];
	}
	if (mfanItem.artist != nil) {
	    [summary appendString: mfanItem.artist];
	    [summary appendString: @"\n"];
	}
	if (mfanItem.albumTitle != nil) {
	    [summary appendString: mfanItem.albumTitle];
	}
	MFANAlert *alert = [[MFANAlert alloc]
				 initWithTitle: @"Details"
				       message: summary
				   buttonTitle: @"OK"];
	[alert show];
    }
}

- (void) deleteRow: (id) j1 withData: (id) j2
{
    long row = _detailIndex;
    MFANMediaItem *mfanItem;

    mfanItem = [_itemArray objectAtIndex: row];
    if (mfanItem.urlRemote != nil) {
	[[_playContext download] unloadItem: mfanItem];
    }

    [_itemArray removeObjectAtIndex: row];
    _changedArray = YES;

    _detailIndex = -1;
    [_tableView reloadData];
}

- (void) exRow: (id) j1 withData: (id) j2
{
    long row = _detailIndex;
    NSLog(@"export row %ld", row);

    _renamePrompt = [[MFANRenamePrompt alloc] initWithFrame: _appFrame
					      keyLabel: @"ExportFileName.mp3"];
    [_renamePrompt displayFor: self sel: @selector(exportPart2:) parent: self];
}

- (void) exportPart2: (id) junk
{
    BOOL status;
    NSString *newPath;
    NSString *oldPath;
    MFANMediaItem *mfanItem;
    NSString *mlkUrl;
    NSError *error;

    if (_detailIndex < 0) {
	_renamePrompt = nil;
	return;
    }

    NSLog(@"blah %@", [_renamePrompt text]);
    newPath = fileNameForDoc([_renamePrompt text]);
    _renamePrompt = nil;
    mfanItem = _itemArray[_detailIndex];
    mlkUrl = [mfanItem localUrl];
    if ([mlkUrl length] == 0) {
	MFANWarn *warn = [[MFANWarn alloc] initWithTitle: @"Export Failed"
					   message: @"File not downloaded yet"
					   secs: 1.0];
	warn = nil;
	return;
    }
    oldPath = [MFANDownload fileNameFromMlkUrl: mlkUrl];

    error = nil;
    status = [[NSFileManager defaultManager] copyItemAtPath: oldPath
					     toPath: newPath
					     error: &error];

    if (!status) {
	MFANWarn *warn = [[MFANWarn alloc] initWithTitle: @"Export Failed"
					   message: @"Copy of file to export dir failed"
					   secs: 1.0];
	warn = nil;
    }

}

- (void) luRow: (id) j1 withData: (id) j2
{
    long row = _detailIndex;
    MFANMediaItem *mfanItem;

    /* pretend we changed the array, since that'll do a superset of what
     * we need done if donePressed: is called.
     */
    _changedArray = YES;

    /* context sensitive button: load or unload */
    mfanItem = [_itemArray objectAtIndex: row];
    if ([[mfanItem localUrl] length] == 0) {
	[[_playContext download] loadItem: mfanItem];
	if (_detailTimer == nil) {
	    _detailTimer = [NSTimer scheduledTimerWithTimeInterval: 1.0
				    target:self
				    selector:@selector(timerFired:)
				    userInfo:nil
				    repeats: YES];
	}
    }
    else {
	[[_playContext download] unloadItem: mfanItem];
	_detailIndex = -1;	/* done showing this */
    }

    [_tableView reloadData];
}

- (void) timerFired: (id) junk
{
    MFANMediaItem *mfanItem;
    int downloadCount;
    MFANDownload *download;

    download = [_playContext download];
    downloadCount = [download downloadCount];

    if (_detailIndex >= 0) {
	mfanItem = [_itemArray objectAtIndex: _detailIndex];
	[_tableView reloadData];
    }
    else if (downloadCount > _lastDownloadCount) {
	_lastDownloadCount = downloadCount;
	[_tableView reloadData];
    }

    if ([download isIdle]) {
	[_detailTimer invalidate];
	_detailTimer = nil;
	[_tableView reloadData];
    }
}

- (void) definitionPressed: (id) sender
{
    /* make sure the changes we've made in the list are visible to the definition code */
    if (_changedArray) {
	[_setList setMediaArray: _itemArray];
    }

    [_viewCont switchToAppByName: @"edit"];
}

- (void) editPressed: (id) sender
{
    MFANTopEdit *edit;
    /* do we need to do this? */
    // [[_playContext download] checkDownloadedArray: [_setList itemArray]];

    /* get the channel specific edit object */
    edit = (MFANTopEdit *) [_viewCont getTopAppByName: @"edit"];
    [edit setBridgeCallback: self withAction: @selector(editDone:)];

    /* and make edit screen top level */
    [_viewCont switchToAppByName: @"edit"];

    if (_channelType == MFANChannelRadio) {
	[edit bridgePopupMedia: [MFANSetList radioSel]];
    }
    else if (_channelType == MFANChannelPodcast) {
	[edit bridgePopupMedia: [MFANSetList podcastsSel]];
    }
    else if (_channelType == MFANChannelMusic) {
	[edit bridgeAddPressed];
    }
}

- (void) editDone: (id) sender
{
    NSLog(@"- topedit edit done");
    _changedArray = YES;
    [_viewCont makeActive: self];
    [_tableView reloadData];
}

- (void) cancelPressed: (id) sender
{
    /* even if we're canceled, someone may have changed the load/unload status
     * of some of our items.  So, reevaluate the real array.
     */
    [[_playContext download] checkDownloadedArray: [_setList itemArray]];
    
    [_viewCont switchToAppByName: @"main"];
}

- (void) helpPressed: (id) sender withData: (NSNumber *) junk
{
    /* even if we're canceled, someone may have changed the load/unload status
     * of some of our items.  So, reevaluate the real array.
     */
    MFANPopHelp *popHelp;

    popHelp = [[MFANPopHelp alloc] initWithFrame: _appFrame
				   helpFile: @"help-list"
				   parentView: self
				   warningFlag: 0];
    [popHelp show];
}

- (void) donePressed: (id) sender withData: (NSNumber *) junk
{
    BOOL changedSelection = NO;
    float currentPlaybackTime;
    MFANMediaItem *currentMFANItem;

    _detailIndex = -1;

    if (_changedArray) {
	/* no need to call checkDownloadedArray, since we're loading our collection
	 * of media items into the setlist.
	 */
	[_setList setMediaArray: _itemArray];

	currentMFANItem = [_setList itemWithIndex: _currentIndex];
	if (currentMFANItem == _originalMFANItem) {
	    currentPlaybackTime = [_playContext currentPlaybackTime];
	    [_playContext setIndex: (int) _currentIndex];
	    _playContext.currentTime = currentPlaybackTime; /* was reset to 0 by setIndex: */
	    [_playContext play];
	}
	else {
	    [_playContext setIndex: (int) _currentIndex];
	    [_playContext play];
	}
    }
    else {
	/* didn't change array, so don't do anything if didn't change position */
	if (_changedSelection && _currentIndex != _originalIndex) {
	    [_playContext setIndex: (int) _currentIndex];
	    [_playContext play];
	}
    }

    if (_changedArray || changedSelection) {
	[_playContext saveListToFile];
    }

    [_viewCont switchToAppByName: @"main"];
}

- (void) refreshLibrary
{
    MFANSetList *setList;

    /* otherwise, save the state of the context, and save the file as well */
    [_playContext setQueryInfo: [_playContext queryInfo]];	/* changes setList's itemArray */
    _itemArray = [NSMutableArray arrayWithArray: [_setList itemArray]];

    /* try reconnecting detached guys in the new setlist */
    setList = [_playContext setList];
    [[_playContext download] checkDownloadedArray: [setList itemArray]];

    [_playContext saveListToFile];
    _changedArray = YES;

    [_tableView reloadData];
}

/* check if full or update refresh */
- (void) checkRefreshRss
{
    UIAlertController *alert = [UIAlertController alertControllerWithTitle: @"Reload  / Update"
								   message: @"Reload all items / Update with newest items"
							    preferredStyle: UIAlertControllerStyleAlert];
    UIAlertAction *action = [UIAlertAction actionWithTitle:@"Reload all"
						     style: UIAlertActionStyleDefault
						   handler:^(UIAlertAction *) {
	    [self refreshRss: 1];
	}];
    [alert addAction: action];

    action = [UIAlertAction actionWithTitle:@"Update new items"
				      style: UIAlertActionStyleDefault
				    handler:^(UIAlertAction *) {
	    [self refreshRss: 2];
	}];
    [alert addAction: action];

    action = [UIAlertAction actionWithTitle:@"Cancel"
				      style: UIAlertActionStyleCancel
				    handler:^(UIAlertAction *) {}];
    [alert addAction: action];

    [currentViewController() presentViewController: alert animated:YES completion: nil];
}

- (void) refreshRss: (NSInteger) ix
{
    MFANWarn *warn;

    warn = [[MFANWarn alloc] initWithTitle: @"Refreshing station"
			     message: (ix == 1? @"Reload all items from RSS feed" :
				       @"Loading new items from RSS feeds")
			     secs: 2.0];

    [self updatePodcastsWithForce: (ix == 1? YES : NO)];
    [[_playContext download] checkDownloadedArray: _itemArray];

    /* save the file so that a cancel after this doesn't forget the items that we
     * added, while at the same time having updated the last refresh time.
     */
    [_setList setMediaArray: _itemArray];
    [_playContext saveListToFile];

    _changedArray = YES;
    [_tableView reloadData];
}

- (void) refreshPressed: (id) sender
{
    MFANSetList *setList;

    setList = [_playContext setList];
    if ([setList hasRssItems]) {
	[self checkRefreshRss];
    }
    else {
	[self refreshLibrary];
    }
}

- (void) randomizePressed: (id) sender
{
    MFANSetList *setList;
    uint32_t i;

    setList = [_playContext setList];

    /* randomize is sort of a shuffle, and so we do several of them */
    for(i=0;i<11; i++)
	[setList randomize];

    _itemArray = [NSMutableArray arrayWithArray: [setList itemArray]];
    _currentIndex = 0;
    _changedSelection = YES;
    _changedArray = YES;

    [_playContext saveListToFile];

    [_tableView reloadData];
}

- (void) updatePodcastsWithForce: (BOOL) forceDateToZero
{
    MFANScanItem *scanItem;
    NSArray *itemsArray;
    long podcastDate;
    NSArray *scanArray;

    scanArray = [_setList queryInfo];

    /* scanArray has an array of MFANScanItem entries; _itemArray has
     * an array of MFANMediaItems corresponding to the scan items.
     * These won't become permanent until done (as opposed to cancel)
     * is pressed.
     */
    for(scanItem in scanArray) {
	if ([scanItem scanFlags] & [MFANScanItem scanPodcast]) {
	    podcastDate = (forceDateToZero? 0 : scanItem.podcastDate);
	    scanItem.items = nil;	/* force new evaluation of MFANMediaItems */
	    itemsArray = [scanItem performFilters: [scanItem mediaItems]];
	    _itemArray = [_setList mergeItems: _itemArray
				   withItems: itemsArray
				   fromDate: podcastDate];
	}
    }
}

/* new version of delPrev -- delete all previous unloaded items from
 * the list.  Leave loaded items alone.
 */
- (long) deletePrevious: (long) ix
{
    NSString *podcastName;
    MFANMediaItem *mfanItem;
    long endIx;
    long count;
    long i;
    long delCount;

    count = [_itemArray count];
    if (ix >= count)
	return 0;

    mfanItem = [_itemArray objectAtIndex: ix];
    if (mfanItem == nil)
	return 0;

    /* not used right now */
    podcastName = mfanItem.albumTitle;

    /* delete all earlier entries with the same albumTitle field */
    endIx = ix;
    delCount = 0;
    for(i=0; i<endIx; ) {
	mfanItem = [_itemArray objectAtIndex: i];
	/* skip items that are downloading or are already downloaded */
	if ( ![[_playContext download] isLoading: mfanItem] &&
	     [mfanItem.localUrl length] <= 0) {
	    /* this item gets deleted */
	    [_itemArray removeObjectAtIndex: i];
	    endIx--;	/* last guy's index has changed */
	    delCount++;
	}
	else {
	    i++;
	}
    }

    return delCount;
}

- (void) delPrevPressed: (id) sender withData: (NSNumber *) number
{
    MFANSetList *setList;
    long delCount;

    setList = [_playContext setList];

    /* _currentIndex is our guy */
    delCount = [self deletePrevious: _currentIndex];

    _currentIndex -= delCount;

    _changedArray = YES;
    _changedSelection = YES;

    if (_currentIndex < [_itemArray count]) {
	[_tableView
	    scrollToRowAtIndexPath: [NSIndexPath indexPathForRow: _currentIndex inSection: 0]
	    atScrollPosition: UITableViewScrollPositionMiddle
	    animated: YES];
    }

    [_tableView reloadData];
}

- (void) loadAllPressed: (id) sender
{
    MFANDownload *download;

    download = [_playContext download];
    [download setDownloadAll: YES];

    _popStatus = [[MFANPopStatus alloc] initWithFrame: _appFrame
					msg: @""
					parentView: self];
    [_popStatus show];
    
    [self loadAllTimerFired: nil];
}

- (void) loadAllTimerFired: (id) junk
{
    MFANDownload *download;
    NSString *msg;

    _detailTimer = nil;

    download = [_playContext download];

    msg = [NSString stringWithFormat: @"%ld entries left to load", [download unloadedCount]];
    [_popStatus updateMsg: msg];

    if ([_popStatus canceled]) {
	[download setDownloadAll: NO];
    }

    if ([download downloadAll]) {
	/* still downloading */
	if (!_detailTimer) {
	    _detailTimer = [NSTimer scheduledTimerWithTimeInterval: 0.5
				    target:self
				    selector:@selector(loadAllTimerFired:)
				    userInfo:nil
				    repeats: NO];
	}
    }
    else {
	[_popStatus stop];
	_popStatus = nil;
    }
}

// Only override drawRect: if you perform custom drawing.
// An empty implementation adversely affects performance during animation.
//- (void)drawRect:(CGRect)rect
//{
//    drawBackground(rect);
//}

@end
