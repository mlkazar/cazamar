//
//  MFANTopDoc.m
//  MusicFan
//
//  Created by Michael Kazar on 4/27/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANTopDoc.h"
#import "MFANTopLevel.h"
#import "MFANIconButton.h"
#import "MFANViewController.h"
#import "MFANCGUtil.h"
#import "MFANWarn.h"
#import "MFANTopSettings.h"
#import "MFANPlayContext.h"

static UIColor *_backgroundColor;

/* ****************MFANFileItem implementation **************** */
@implementation MFANFileItem {
    NSString *_fileName;
    uint64_t _size;
}

- (MFANFileItem *) initWithName: (NSString *) name size: (uint64_t) size
{
    self = [super init];

    if (self) {
	_fileName = name;
	_size = size;
    }

    return self;
}

- (NSString *) name
{
    return _fileName;
}

- (void) setName: (NSString *) newName
{
    _fileName = newName;
}

- (uint64_t) size
{
    return _size;
}

@end

/* ****************MFANRenamePrompt implementation **************** */
@implementation MFANRenamePrompt {
    CGRect _appFrame;
    NSString *_keyLabel;
    NSString *_key;
    SEL _returnSel;
    MFANIconButton *_doneButton;
    MFANIconButton *_cancelButton;
    UIView __weak *_parentView;
    UITextField *_keyView;
}

- (MFANRenamePrompt *) initWithFrame: (CGRect) appFrame
			    keyLabel: (NSString *) keyLabel
{
    CGFloat buttonWidth;
    CGFloat buttonHeight;
    CGFloat textWidth;
    CGFloat textHeight;	/* used for label and text */
    CGRect tframe;
    CGFloat nextY;
    CGFloat fontSizeScale;

    self = [super initWithFrame: appFrame];
    if (self) {
	_appFrame = appFrame;
	_keyLabel = keyLabel;

	/* the Done and Cancel button sizes */
	buttonWidth = 0.25 * _appFrame.size.width;
	buttonHeight = 0.1 * _appFrame.size.height;

	/* the text width and height  */
	textWidth = 0.9 * _appFrame.size.width;
	textHeight = 0.1 * _appFrame.size.height;
	fontSizeScale = 0.9;

	nextY = _appFrame.size.height * 0.05;

	/* add first text box */
	tframe = _appFrame;
	tframe.origin.y = nextY;
	tframe.origin.x += (_appFrame.size.width - textWidth) / 2;
	tframe.size.width = textWidth;
	tframe.size.height = textHeight;
	_keyView = [[UITextField alloc] initWithFrame: tframe];
	[_keyView setDelegate: self];
	[_keyView setPlaceholder: _keyLabel];
	[_keyView setReturnKeyType: UIReturnKeyDone];
	_keyView.autocapitalizationType = UITextAutocapitalizationTypeNone;
	_keyView.autocapitalizationType = UITextAutocorrectionTypeNo;
	_keyView.spellCheckingType = UITextSpellCheckingTypeNo;

	[_keyView setBackgroundColor: [UIColor whiteColor]];
	[self addSubview: _keyView];

	/* move down an extra line */
	nextY += 2 * textHeight;

	/* now add Done button */
	tframe = _appFrame;
	tframe.origin.x = _appFrame.size.width/3 - buttonWidth/2;
	tframe.origin.y = nextY;
	tframe.size.height = buttonHeight;
	tframe.size.width = buttonWidth;
	_doneButton = [[MFANIconButton alloc] initWithFrame: tframe
					      title: @"Done"
					      color: [UIColor greenColor]
					      file: @"icon-done.png"];
	[_doneButton addCallback: self
		     withAction: @selector(donePressed:withData:)];
	[self addSubview: _doneButton];
    
	/* and cancel button */
	tframe.origin.x += _appFrame.size.width/3;
	_cancelButton = [[MFANIconButton alloc] initWithFrame: tframe
						title: @"Cancel"
						color: [UIColor redColor]
						file: @"icon-cancel.png"];
	[_cancelButton addCallback: self
		       withAction: @selector(cancelPressed:withData:)];
	[self addSubview: _cancelButton];


	[self setBackgroundColor: [UIColor colorWithHue:0.0
					   saturation: 0.0
					   brightness: 0.0
					   alpha: 0.6]];
    }
    return self;
}

- (BOOL)textFieldShouldReturn:(UITextField *) textField {
    [textField resignFirstResponder];
    return YES;
}

- (NSString *) text
{
    return _keyView.text;
}

- (UITextField *) keyView
{
    return _keyView;
}

- (void) donePressed: (id) sender withData: (NSNumber *) number
{
    NSLog(@"done popoverride name='%@'", _keyView.text);
    [self removeFromSuperview];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    [_parentView performSelector: _returnSel withObject: _keyView];
#pragma clang diagnostic pop
}

- (void) cancelPressed: (id) sender withData: (NSNumber *) number
{
    NSLog(@"cancel popoverride");
    [self removeFromSuperview];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    [_parentView performSelector: _returnSel withObject: nil];
#pragma clang diagnostic pop
}

- (void) displayFor: (UIView *) parentView sel: (SEL) returnSel parent: (UIView *) parent
{
    _parentView = parentView;
    _returnSel = returnSel;

    self.layer.zPosition = 2;
    [parent addSubview: self];
}
@end

/* ****************MFANDocEdit implementation **************** */
@implementation MFANDocEdit {
    /* get data from topEdit's scanItems */
    MFANTopDoc *_topDoc;
    NSMutableSet *_listTableSelections;	/* set of NSIndexPaths with checkmarks */
    UITableView *_tableView;
    MFANLabel *_playButton;
    MFANLabel *_deleteButton;
    MFANLabel *_renameButton;
    CGFloat _rowHeight;
    CGRect _localFrame;
    int _detailIndex;
    BOOL _playing;
    UISwipeGestureRecognizer *_swipeLeft;
    UISwipeGestureRecognizer *_swipeRight;
    MFANRenamePrompt *_renamePrompt;
    MFANPlayContext *_savedContext;
    MFANPlayContext *_popContext;
}

- (BOOL) playing
{
    return _playing;
}

- (BOOL) anySelected
{
    return ([_listTableSelections count] != 0);
}

- (void) reloadData
{
    [_tableView reloadData];
}

- (MFANDocEdit *) initWithParent:(MFANTopDoc *) edit frame: (CGRect) frame;
{

    self = [super initWithFrame: frame];
    if (self) {
	_topDoc = edit;
	_localFrame = frame;
	_detailIndex = -1;
	_playing = NO;

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

	[_topDoc addSubview: _tableView];

	_listTableSelections = [[NSMutableSet alloc] init];

	_rowHeight = _tableView.rowHeight;
	NSLog(@"tableview rowh=%f", _rowHeight);

	_playButton = [[MFANLabel alloc] initWithTarget: self
					 selector: @selector(playRow:withData:)];
	[_playButton setTitle: @"Play" forState: UIControlStateNormal];
	_playButton.backgroundColor = [UIColor blueColor];

	_deleteButton = [[MFANLabel alloc] initWithTarget: self
					   selector:@selector(deleteRow:withData:)];
	[_deleteButton setTitle: @"Delete" forState: UIControlStateNormal];
	_deleteButton.backgroundColor = [UIColor redColor];

	_renameButton = [[MFANLabel alloc] initWithTarget: self
					   selector:@selector(renameRow:withData:)];
	[_renameButton setTitle: @"Rename" forState: UIControlStateNormal];
	_renameButton.backgroundColor = [UIColor orangeColor];
    }
    return self;
}

- (UITableView *) tableView
{
    return _tableView;
}

- (void) stopPlaying
{
    MFANTopLevel *topLevel = [_topDoc topLevel];
    if (_playing) {
	/* stop playing */
	_playing = NO;
	[_popContext pause];
	[topLevel setCurrentContext: _savedContext];
	[topLevel playCurrent];
    }
}

- (void) playRow: (id) j1 withData: (id) j2
{
    MFANScanItem *scan;
    NSMutableArray *ar;
    NSMutableArray *fileItemArray;
    MFANTopLevel *topLevel = [_topDoc topLevel];

    fileItemArray = [_topDoc fileItems];
    if (_detailIndex >= 0) {
	if (!_playing) {
	    _playing = YES;
	    scan = [[MFANScanItem alloc] init];
	    scan.title = [fileItemArray[_detailIndex] name];
	    scan.secondaryKey = @"[Inet Recording]";
	    scan.scanFlags = [MFANScanItem scanRecording];
	    scan.minStars = 0;
	    scan.cloud = 0;
	    scan.items = nil;

	    _savedContext = [topLevel currentContext];
	    _popContext = [topLevel popupContext];

	    ar = [[NSMutableArray alloc] init];
	    [ar addObject: scan];
	    [_popContext setQueryInfo: ar];
	    [topLevel setCurrentContext: _popContext];
	    [_popContext play];
	}
	else {
	    [self stopPlaying];
	}
    }
}

- (void) deleteRow: (id) j1 withData: (id) j2
{
    NSMutableArray *fileItemArray;
    MFANFileItem *fileItem;
    NSString *filePath;
    NSError *error;
    BOOL status;

    NSLog(@"in deleterow");

    if (_detailIndex >= 0) {
	fileItemArray = [_topDoc fileItems];
	fileItem = fileItemArray[_detailIndex];
	filePath = fileNameForDoc([fileItem name]);

	error = nil;
	status = [[NSFileManager defaultManager] removeItemAtPath: filePath error: &error];
	if (!status) {
	    MFANWarn *warn = [[MFANWarn alloc] initWithTitle: @"Delete Failed"
					       message: @"Delete of object failed"
					       secs: 1.0];
	    warn = nil;
	}
	else {
	    [fileItemArray removeObjectAtIndex: _detailIndex];
	}

	/* and mark that we've made changes so that we'll randomize and reload 
	 * song list when Done is pressed.
	 */
	_detailIndex = -1;

	[_tableView reloadData];
    }
}

- (void) renameRow: (id) j1 withData: (id) j2
{
    CGRect newFrame;

    newFrame = _localFrame;

    /* clear selection */
    [_tableView reloadData];

    _renamePrompt = [[MFANRenamePrompt alloc] initWithFrame: newFrame keyLabel: @"New file name"];
    [_renamePrompt displayFor: self sel: @selector(renamePart2:) parent: _topDoc];
}

- (void) renamePart2: (id) parm
{
    NSMutableArray *fileItemArray;
    MFANFileItem *fileItem;
    NSString *filePath;
    NSError *error;
    NSString *newName;
    NSString *newBaseName;
    BOOL status;

    NSLog(@"in rename part 2");

    if (parm != nil && _detailIndex >= 0) {
	fileItemArray = [_topDoc fileItems];
	fileItem = fileItemArray[_detailIndex];
	filePath = fileNameForDoc([fileItem name]);
	newBaseName = [_renamePrompt text];
	newName = fileNameForDoc(newBaseName);

	error = nil;
	status = [[NSFileManager defaultManager] moveItemAtPath: filePath
						 toPath: newName
						 error: &error];
	if (!status) {
	    MFANWarn *warn = [[MFANWarn alloc] initWithTitle: @"Rename Failed"
					       message: @"Rename of object failed"
					       secs: 1.0];
	    warn = nil;
	}
	else {
	    [fileItem setName: newBaseName];
	}
    }

    /* and mark that we've made changes so that we'll randomize and reload 
     * song list when Done is pressed.
     */
    _detailIndex = -1;

    [_tableView reloadData];
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
	[self stopPlaying];
	_detailIndex = -1;
    }

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

- (void) updateView: (id) junk
{
    [_tableView reloadData];
}

- (NSInteger) tableView: (UITableView *)tview numberOfRowsInSection: (NSInteger) section
{
    return (NSInteger) [[_topDoc fileItems] count];
}

- (UITableViewCell *) tableView: (UITableView *) tview cellForRowAtIndexPath: (NSIndexPath *)path
{

    unsigned long ix;
    UITableViewCell *cell;
    NSMutableArray *fileItemArray;
    MFANFileItem *fileItem;
    UIColor *textColor;
    UILabel *tlabel;
    long long size;

    ix = [path row];
    
    fileItemArray = [_topDoc fileItems];
    if (ix >= [fileItemArray count])
	return nil;

    fileItem = [fileItemArray objectAtIndex: ix];
    if (fileItem == nil)
	return nil;

    textColor = [MFANTopSettings textColor];

    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
				    reuseIdentifier: nil];

    cell.textLabel.text = [fileItem name];
    cell.textLabel.textColor = textColor;
    cell.textLabel.font = [MFANTopSettings basicFontWithSize: 18];
    cell.textLabel.adjustsFontSizeToFitWidth = YES;

    size = [fileItem size];
    if (size >= 5*1024*1024)
	cell.detailTextLabel.text = [NSString stringWithFormat: @"%lld MB", size/(1024*1024)];
    else
	cell.detailTextLabel.text = [NSString stringWithFormat: @"%lld KB", size/1024];
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

	/* setup play button */
	[_playButton removeFromSuperview];
	_playButton.frame = CGRectMake(2*_localFrame.size.width/5,
				       0,
				       _localFrame.size.width/5,
				       height);
	tlabel = [_playButton titleLabel];
	[tlabel setFont: [MFANTopSettings basicFontWithSize: height * 0.4]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];

	[cell.contentView addSubview: _playButton];

	/* setup delete button */
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

	/* setup rename button */
	[_renameButton removeFromSuperview];
	
	_renameButton.frame = CGRectMake(3*_localFrame.size.width/5,
					 0,
					 _localFrame.size.width/5,
					 height);
	tlabel = [_renameButton titleLabel];
	[tlabel setFont: [MFANTopSettings basicFontWithSize: height * 0.4]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];

	[cell.contentView addSubview: _renameButton];
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
    NSMutableArray *fileItemArray;

    if (fromRow != toRow) {
	fileItemArray = [_topDoc fileItems];

	fromOb = [fileItemArray objectAtIndex: fromRow];
	[fileItemArray removeObjectAtIndex: fromRow];
	if (fromRow > toRow) {
	    /* moving earlier, so slide everything else down one */
	    [fileItemArray insertObject: fromOb atIndex: toRow];
	}
	else {
	    /* looks like toRow has already been adjusted to take into account
	     * that item at fromRow has been removed.
	     */
	    [fileItemArray insertObject: fromOb atIndex: toRow];
	}
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
@end /* MFANDocEdit */

@implementation MFANTopDoc {
    MFANChannelType _channelType;	/* type of channel this defines */
    NSMutableArray *_fileItems;		/* array of MFANFileItems */
    MFANIconButton *_doneButton;	/* done button for entire edit */
    MFANIconButton *_clearButton;	/* clear button for entire edit */
    MFANTopLevel *_topLevel; 
    MFANViewController *_viewCont;	/* back ptr to our view controller */
    MFANDocEdit *_docEdit;		/* to supply data to _listTableView */
    CGRect _appFrame;			/* frame for edit application */
    CGFloat _appHMargin;
    CGFloat _appVMargin;
    CGFloat _buttonHeight;
    CGFloat _buttonWidth;
    CGFloat _buttonMargin;
    NSMutableSet *_subviews;
    int _alertIndex;
}

- (id)initWithFrame:(CGRect)frame
	      level: (MFANTopLevel *) topLevel
     viewController: (MFANViewController *) viewCont
{
    CGRect tframe;
    UIColor *buttonColor;
    CGFloat nextButtonX;
    CGFloat nextButtonY;
    CGFloat consoleHeight;
    CGFloat defaultHue = 0.55;
    CGFloat defaultSat = 0.7;
    int nbuttons = 1;
    CGFloat buttonSkip;

    self = [super initWithFrame:frame];
    if (self) {
        // Initialization code
	_viewCont = viewCont;
	_topLevel = topLevel;
	_fileItems = [[NSMutableArray alloc] init];
	_appVMargin = 20.0;
	_appHMargin = 2.0;
	_buttonHeight = 64.0;
	_buttonMargin = 5.0;
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

	_docEdit = [[MFANDocEdit alloc] initWithParent: self frame: textFrame];

	[_subviews addObject: [_docEdit tableView]];

	buttonColor = [UIColor colorWithHue: defaultHue
			       saturation: defaultSat
			       brightness: 1.0
			       alpha: 1.0];

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

- (MFANTopLevel *) topLevel
{
    return _topLevel;
}

- (NSMutableArray *) fileItems
{
    return _fileItems;
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

/* apply changes */
- (void) saveEdits
{
    /* actually, everything already done */
}

- (void) donePressed: (id) sender withData: (NSNumber *) number
{
    [_docEdit stopPlaying];
    [_viewCont switchToAppByName: @"main"];
}

-(void) deactivateTop
{
    return;
}

-(void) activateTop
{
    MFANFileItem *fileItem;
    NSArray *listing;
    NSString *dirPath;
    NSString *name;
    NSString *path;
    long size;

    dirPath = fileNameForDoc(@".");

    listing = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dirPath error:NULL];
    _fileItems = [[NSMutableArray alloc] init];

    for(name in listing) {
	path = fileNameForDoc(name);
	size = (long) [[[NSFileManager defaultManager]
			   attributesOfItemAtPath: path error: nil] fileSize];
	fileItem = [[MFANFileItem alloc] initWithName: name size: size];
	[_fileItems addObject: fileItem];
    }

    /* enumerate the doc directory */
    [_docEdit reloadData];
}

@end /* MFANTopDoc */
