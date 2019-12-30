//
//  MFANPopEdit.m
//  MusicFan
//
//  Created by Michael Kazar on 6/5/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANPopEdit.h"
#import "MFANStarsView.h"
#import "MFANIconButton.h"
#import "MFANMediaSel.h"
#import "MFANCGUtil.h"
#import "MFANTopSettings.h"
#import "MFANMetalButton.h"
#import "MFANWarn.h"
#import "MFANCoreButton.h"

#include <stdlib.h>

static CGFloat _baseSaturation = 0.7;
static UIColor *_backgroundColor;

@implementation MFANPopEditOverride {
    CGRect _appFrame;
    NSString *_keyLabel;
    NSString *_valueLabel;
    NSString *_key;
    NSString *_value;
    SEL _returnSel;
    MFANIconButton *_doneButton;
    MFANIconButton *_cancelButton;
    UIView *_parentView;
    UITextField *_keyView;
    UITextField *_valueView;
}

- (MFANPopEditOverride *) initWithFrame: (CGRect) appFrame
			      keyLabel: (NSString *) keyLabel
			    valueLabel: (NSString *) valueLabel
{
    CGFloat buttonWidth;
    CGFloat buttonHeight;
    CGFloat textWidth;
    CGFloat textHeight;	/* used for label and text */
    CGRect tframe;
    CGFloat inputWidth;
    CGFloat nextY;
    CGFloat fontSizeScale;

    self = [super initWithFrame: appFrame];
    if (self) {
	_appFrame = appFrame;
	_keyLabel = keyLabel;
	_valueLabel = valueLabel;

	buttonWidth = 0.25 * _appFrame.size.width;
	buttonHeight = 0.1 * _appFrame.size.height;
	textWidth = 0.4 * _appFrame.size.width;
	inputWidth = 0.9 * _appFrame.size.width;
	textHeight = 0.1 * _appFrame.size.height;
	fontSizeScale = 0.9;

	nextY = _appFrame.size.height * 0.05;

	/* add first text box */
	tframe = _appFrame;
	tframe.origin.x += _appFrame.size.width * 0.05;
	tframe.size.width = inputWidth;
	tframe.size.height = textHeight;
	_keyView = [[UITextField alloc] initWithFrame: tframe];
	[_keyView setDelegate: self];
	[_keyView setPlaceholder: @"Radio Station Name"];
	[_keyView setReturnKeyType: UIReturnKeyDone];

	[_keyView setBackgroundColor: [UIColor whiteColor]];
	[self addSubview: _keyView];

	/* add second text box */
	tframe.origin.y += textHeight + _appFrame.size.height * 0.05;
	_valueView = [[UITextField alloc] initWithFrame: tframe];
	[_valueView setDelegate: self];
	[_valueView setPlaceholder: @"Radio Station URL"];
	[_valueView setReturnKeyType: UIReturnKeyDone];
	[_valueView setAutocapitalizationType: UITextAutocapitalizationTypeNone];
	[_valueView setAutocorrectionType: UITextAutocorrectionTypeNo];
	[_valueView setKeyboardType: UIKeyboardTypeURL];
	_valueView.text = @"http://";

	[_valueView setBackgroundColor: [UIColor whiteColor]];
	[self addSubview: _valueView];

	/* now add Done button */
	tframe = _appFrame;
	tframe.origin.y = 0.9*_appFrame.size.height;
	tframe.size.height = 0.1*_appFrame.size.height;
	tframe.origin.x = _appFrame.size.width/3 - buttonWidth/2;
	tframe.size.width = buttonWidth;
	_doneButton = [[MFANIconButton alloc] initWithFrame: tframe
					      title: @"Done"
					      color: [UIColor colorWithHue: 0.4
							      saturation: _baseSaturation
							      brightness: 1.0
							      alpha: 1.0]
					      file: @"icon-done.png"];
	[_doneButton addCallback: self
		     withAction: @selector(donePressed:withData:)];
	[self addSubview: _doneButton];
    
	/* and cancel button */
	tframe = _appFrame;
	tframe.origin.y = 0.9*_appFrame.size.height;
	tframe.size.height = 0.1*_appFrame.size.height;
	tframe.origin.x = 2*_appFrame.size.width/3 - buttonWidth/2;
	tframe.size.width = buttonWidth;
	_cancelButton = [[MFANIconButton alloc] initWithFrame: tframe
						title: @"Cancel"
						color: [UIColor colorWithHue: 0.02
								saturation: _baseSaturation
								brightness: 1.0
								alpha: 1.0]
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

- (UITextField *) keyView
{
    return _keyView;
}

- (UITextField *)valueView
{
    return _valueView;
}

- (void) donePressed: (id) sender withData: (NSNumber *) number
{
    NSLog(@"done popoverride name='%@' url='%@'", _keyView.text, _valueView.text);
    [self removeFromSuperview];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    [_parentView performSelector: _returnSel withObject: nil];
#pragma clang diagnostic pop
}

- (void) cancelPressed: (id) sender withData: (NSNumber *) number
{
    NSLog(@"cancel popoverride");
    [self removeFromSuperview];
}

- (void) displayFor: (UIView *) parentView sel: (SEL) returnSel
{
    _parentView = parentView;
    _returnSel = returnSel;

    self.layer.zPosition = 2;
    [parentView addSubview: self];
}

- (void) helpKeyLabel: (id) key withData: (id) data
{
    NSLog(@"- help");
}

@end

@implementation MFANPopEdit {
    id<MFANAddScan> _scanHolder;
    CGRect _appFrame;
    UITableView *_popTableView;
    UIView *_popBackView;		/* white background */
    NSMutableArray *_popTableSelections;/* set of NSIndexPaths with checkmarks */
    MFANIconButton *_popDoneButton;	/* done button for popup */
    MFANIconButton *_popCancelButton;	/* cancel button for popup */
    MFANIconButton *_addButton;		/* add button for popup */
    MFANMetalButton *_popCloudButton;	/* button for enabling cloud */
    id<MFANMediaSel> _popMediaSel;	/* current media selector */
    MFANStarsView *_starsView;		/* stars */
    CGFloat _buttonWidth;
    CGFloat _buttonHeight;
    CGFloat _buttonMargin;
    UISearchBar *_searchBar;
    MFANPopEditOverride *_override;

    int *_mapArrayp;			/* array of index maps for search */
    int _mapAllocSize;			/* size of array */
    int _mapCount;			/* count of # of valid entries in array */

    UIAlertController *_alertController; /* view for search status alert */
    UIAlertAction *_alertAction;	/* action for pressing cancel button */
    NSTimer *_alertTimer;		/* timer for search status */
    BOOL _finishedSearch;		/* set when we know the search is done */

    id _callbackObj;
    SEL _finishedSel;
    SEL _nextSel;

    CGFloat _scaledDefaultImageSize;
    UIImage *_defaultImage;
    UIImage *_scaledDefaultImage;

    MFANViewController *_viewCont;

    /* first sectionExtras sections contain sectionSize+1 rows, and
     * the rest have exactly sectionSize rows.  The _sectionCount is
     * either 1 or _maxSectionCount, depending upon whether we have
     * enough to have sections at all.
     */
    long _sectionCount;
    long _sectionSize;
    long _maxSectionCount;
    long _sectionExtras;
}

- (void) dealloc
{
    if (_mapArrayp) {
	free(_mapArrayp);
	_mapArrayp = NULL;
    }
}

/* can be called to reinitialize map as well */
- (void) setupDefaultMap
{
    int newCount;
    int i;

    newCount = (int) [_popMediaSel count];
    if (newCount > _mapAllocSize) {
	if (_mapArrayp) {
	    free(_mapArrayp);
	    _mapArrayp = NULL;
	    _mapAllocSize = 0;
	}
	_mapArrayp = malloc(newCount * sizeof(int));
	_mapAllocSize = newCount;
    }

    for(i=0;i<newCount;i++) {
	_mapArrayp[i] = i;
    }
    _mapCount = newCount;
}

- (MFANPopEdit *) initWithFrame: (CGRect) appFrame
		     scanHolder: (id<MFANAddScan>) scanHolder
		       mediaSel: (id<MFANMediaSel>) mediaSel
		 viewController: (MFANViewController *) viewCont
{
    self = [super init];
    if (self) {
	CGRect tframe;
	_buttonHeight = 50;
	_buttonWidth = 2 * _buttonHeight;
	_buttonMargin = 4.0;
	CGFloat nextButtonY;
	UIColor *buttonColor;

	/* initialize fields */
	_scaledDefaultImageSize = 0.0;
	_defaultImage = [UIImage imageNamed: @"record-152.png"];
	_scaledDefaultImage = nil;
	_viewCont = viewCont;

	/* we set our frame in our parent's coordinate system, and then adjust
	 * appFrame so that it describes our coordinate space to our children.
	 */
	[self setFrame: appFrame];
	_appFrame = appFrame;
	_appFrame.origin.x = 0;
	_appFrame.origin.y = 0;
	_scanHolder = scanHolder;

	_maxSectionCount = 26;

	/* how to center 4 buttons on screen */
	// float col1of4 = _appFrame.origin.x + _appFrame.size.width/8 - _buttonWidth/2;
	// float col2of4 = _appFrame.origin.x + 3*_appFrame.size.width/8 - _buttonWidth/2;
	// float col3of4 = _appFrame.origin.x + 5*_appFrame.size.width/8 - _buttonWidth/2;
	// float col4of4 = _appFrame.origin.x + 7*_appFrame.size.width/8 - _buttonWidth/2;

	float col1of2 = _appFrame.origin.x + _appFrame.size.width/4 - _buttonWidth/2;
	float col2of2 = _appFrame.origin.x + 3*_appFrame.size.width/4 - _buttonWidth/2;

	_popTableSelections = [[NSMutableArray alloc] init];

	_popBackView = [[UIView alloc] init];
	_popBackView.backgroundColor = [UIColor whiteColor];
	_popBackView.frame = _appFrame;
	[self addSubview: _popBackView];

	nextButtonY = _appFrame.origin.y;

	tframe.origin.y = nextButtonY;
	tframe.size.height = _buttonHeight;
	tframe.size.width = _buttonWidth;
	tframe.origin.x = col2of2;
	_popCancelButton = [[MFANIconButton alloc] initWithFrame: tframe
						   title: @"Cancel"
						   color: [UIColor colorWithHue: 0.02
								   saturation: _baseSaturation
								   brightness: 1.0
								   alpha: 1.0]
						   file: @"icon-cancel.png"];
	[self addSubview: _popCancelButton];
	[_popCancelButton addCallback: self
			  withAction: @selector(popCancelPressed:withData:)];

	tframe.origin.x = col1of2;
	/* everything else in tframe remains unchanged */
	_popDoneButton = [[MFANIconButton alloc] initWithFrame: tframe
						 title: @"Done"
						 color: [UIColor colorWithHue: 0.4
								 saturation: _baseSaturation
								 brightness: 1.0
								 alpha: 1.0]
						 file: @"icon-done.png"];
	[self addSubview: _popDoneButton];
	[_popDoneButton addCallback: self
			withAction: @selector(popDonePressed:withData:)];

	nextButtonY += _buttonHeight + _buttonMargin;

	tframe.origin.x = _appFrame.origin.x;
	tframe.origin.y = nextButtonY;
	tframe.size.width = _appFrame.size.width;
	tframe.size.height = _buttonHeight;
	_searchBar = [[UISearchBar alloc] initWithFrame: tframe];
	_searchBar.showsCancelButton = YES;
	_searchBar.delegate = self;
	[self addSubview: _searchBar];

	nextButtonY += _buttonHeight + _buttonMargin;

	_override = [[MFANPopEditOverride alloc] initWithFrame:_appFrame
						 keyLabel: @"Station name"
						 valueLabel: @"URL"];

	/* leave room for one more row of buttons */
	tframe.origin.x = _appFrame.origin.x;
	tframe.origin.y = nextButtonY;
	tframe.size.width = _appFrame.size.width;
	tframe.size.height = (_appFrame.size.height - nextButtonY -
			      (_buttonHeight + _buttonMargin));
	_popTableView = [[UITableView alloc] initWithFrame: tframe style:UITableViewStylePlain];
	[_popTableView setAllowsMultipleSelection: YES];
	[self addSubview: _popTableView];
	[_popTableView setDataSource: self];
	[_popTableView setDelegate: self];
	[_popTableView setRowHeight: [mediaSel rowHeight]];
	[_popTableView setSectionIndexMinimumDisplayRowCount: 40];
	[_popTableView setBackgroundColor: [UIColor whiteColor]];
	_popTableView.sectionIndexBackgroundColor = [UIColor clearColor];
	[_popTableView setSeparatorStyle: UITableViewCellSeparatorStyleNone];
	_popMediaSel = mediaSel;

	nextButtonY += tframe.size.height + _buttonMargin;

	if ([mediaSel supportsLocalAdd]) {
	    /* and the +Add button */
	    tframe.origin.x = _appFrame.origin.x + (_appFrame.size.width - _buttonWidth)/2;
	    tframe.origin.y = nextButtonY;
	    tframe.size.height = 40;
	    tframe.size.width = _buttonWidth;
	    _addButton = [[MFANIconButton alloc] initWithFrame: tframe
						 title: @"Add Custom"
						 color: [UIColor colorWithHue: 0.4
								 saturation: 1.0
								 brightness: 1.0
								 alpha: 1.0]
						 file: @"icon-add.png"];
	    [self addSubview: _addButton];
	    [_addButton addCallback: self
			withAction: @selector(addPressed:withData:)];
	}
	else {
	    tframe.origin.x = _appFrame.origin.x;
	    tframe.origin.y = nextButtonY;
	    tframe.size.height = _buttonHeight;
	    tframe.size.width = _appFrame.size.width/2;
	    _starsView = [[MFANStarsView alloc] initWithFrame: tframe
						background: NO];
	    [self addSubview: _starsView];

	    /* and the cloud button */
	    buttonColor = [MFANTopSettings baseColor];
	    tframe.origin.x = _appFrame.origin.x + _appFrame.size.width - _buttonWidth;
	    tframe.origin.y = nextButtonY;
	    tframe.size.height = 40;
	    tframe.size.width = _buttonWidth;
	    _popCloudButton = [[MFANMetalButton alloc] initWithFrame: tframe
						       title: @"Cloud"
						       color: buttonColor
						       fontSize: 0];
	    [self addSubview: _popCloudButton];
	    [_popCloudButton addCallback: self
			     withAction: @selector(cloudPressed:withData:)
			     value:nil];
	    if ([MFANTopSettings useCloudDefault])
		[_popCloudButton setSelected: YES];
	}

	nextButtonY += _buttonHeight + _buttonMargin;

	_mapArrayp = NULL;
	_mapAllocSize = 0;
	_mapCount = 0;
	[self setupDefaultMap];

	/* figure out how many sections we have; don't section things at all unless we have
	 * at least 200 items.
	 */
	[self updatePopViewInfo];
    }
    return self;
}

- (void) cloudPressed: (id) sender withData: (NSNumber *) number
{
    if ([_popCloudButton isSelected]) {
	[_popCloudButton setSelected: NO];
    }
    else {
	[_popCloudButton setSelected: YES];
    }
}

- (void) addPressed: (id) sender withData: (NSNumber *) number
{
    NSLog(@"***in addPressed");
    [_override displayFor: self sel:@selector(addedEntry:)];
}

- (void) addedEntry: (id) junk
{
    NSLog(@"in addedEntry name='%@' url='%@'",
	  _override.keyView.text, _override.valueView.text);
    [_popMediaSel localAddKey: _override.keyView.text value: _override.valueView.text];

    [self updateSearchResults];
    // [_popTableView reloadData];
}

- (BOOL)tableView:(UITableView *)tableView 
canEditRowAtIndexPath:(NSIndexPath *)indexPath
{
    long row;

    row = [indexPath row];
    if (row >= [_popMediaSel localCount])
	return NO;
    else
	return YES;
}

- (void) tableView: (UITableView *) tview
commitEditingStyle: (UITableViewCellEditingStyle) style
 forRowAtIndexPath: (NSIndexPath *) path
{
    long row;
    BOOL success;

    if (style == UITableViewCellEditingStyleDelete) {
	row = [path row];
	NSLog(@"**remove item at row %d", (int) row);

	success = [_popMediaSel localRemoveEntry: row];
	if (!success) {
	    MFANWarn *warn = [[MFANWarn alloc] initWithTitle:@"Failed removal"
					       message:@"Can't delete built-in entries"
					       secs: 1.0];
	    warn = nil;	/* self destructs */
	    NSLog(@"remove failed for slot %d", (int) row);
	}
	else {
	    // [_popTableView reloadData];
	    [self updateSearchResults];
	}
    }
}

- (void) searchBarCancelButtonClicked:(UISearchBar *)searchBar
{
    _searchBar.text = @"";
    [_searchBar resignFirstResponder];

    /* save current selections, since we're about to change table */
    [self saveSelections];

    [self updateSearchResults];
}

- (void) displayStatus
{
    NSString *status;

    status = [_popMediaSel getStatus];
    NSLog(@"DS: status %@", status);

    if (status == nil) {
	_finishedSearch = YES;
	[_alertController dismissViewControllerAnimated: YES completion: nil];
	_alertController = nil;
	_alertAction = nil;
	NSLog(@"DS: returning");
	[self updateSearchResults];
	return;
    }

    if (_alertController == nil) {
	_alertController = [UIAlertController 
			       alertControllerWithTitle:@"Search in progress"
						message:status
					 preferredStyle: UIAlertControllerStyleAlert];
	_alertAction = [UIAlertAction actionWithTitle: @"Cancel" 
						style: UIAlertActionStyleDefault
					      handler: ^(UIAlertAction *act) {
		[_popMediaSel abortSearch];
	    }];
	[_alertController addAction: _alertAction];
	[_viewCont presentViewController:_alertController animated:YES completion: nil];
	NSLog(@"DS: did show for %p", _alertController);
    }
    else {
	_alertController.message = status;
    }

    _alertTimer = [NSTimer scheduledTimerWithTimeInterval: 1.0
						   target:self
						 selector:@selector(displayStatusTimer:)
						 userInfo:nil
						  repeats: NO];
}

- (void) displayStatusTimer: (id) junk
{
    NSLog(@"DS: in timer");
    _alertTimer = nil;

    [self displayStatus];
}

- (void) searchBarSearchButtonClicked:(UISearchBar *)searchBar
{
    BOOL isAsync;

    _finishedSearch = NO;

    [_searchBar resignFirstResponder];

    /* save current selections, since we're about to change table */
    [self saveSelections];

    if ([_popMediaSel populateFromSearch: _searchBar.text async: &isAsync]) {
	[self setupDefaultMap];
	if (!isAsync) {
	    [self updatePopViewInfo];
	}
	else {
	    [self displayStatus];
	}
    }
    else {
	[self updateSearchResults];
    }
}

- (void) addCallback: (id) callbackObj finishedSel: (SEL) finishedSel nextSel: (SEL) nextSel
{
    _callbackObj = callbackObj;
    _finishedSel = finishedSel;
    _nextSel = nextSel;
}

- (void) updateSearchResults
{
    NSString *searchText;
    int realCount;
    int searchLength;
    NSString *name;
    NSString *details;
    NSRange nameRange;
    NSRange detailsRange;
    int i;

    searchText = [_searchBar text];
    searchLength = (int) [searchText length];

    /* update the mapArrayp to match the selected items */
    if (1 /*searchLength == 0*/) {
	[self setupDefaultMap];
    }
    {
	realCount = (int) [_popMediaSel count];
	_mapCount = 0;
	for(i=0;i<realCount;i++) {
	    name = [_popMediaSel nameByIx: i];
	    details = [_popMediaSel subtitleByIx: i];

	    if ([name length] >= searchLength) {
		nameRange = [name rangeOfString: searchText
				  options:NSCaseInsensitiveSearch];
	    }
	    detailsRange.location = NSNotFound;
	    if (details != nil && [details length] >= searchLength) {
		detailsRange = [details rangeOfString: searchText
					options: NSCaseInsensitiveSearch];
	    }

	    if (nameRange.location != NSNotFound ||
		(details != nil && detailsRange.location != NSNotFound)) {
		_mapArrayp[_mapCount] = i;
		_mapCount++;
	    }
	} 
    }

    [self updatePopViewInfo];
}

- (int) map: (int) ix
{
    if (ix < _mapCount)
	return _mapArrayp[ix];
    else {
	NSLog(@"!Bad index in map %d vs %d", ix, _mapCount);
	return -1;
    }
}

- (void) saveSelections
{
    NSIndexPath *path;
    MFANScanItem *scan;
    int row;
    int section;
    long ix;
    id<MFANAddScan> scanHolder;

    scanHolder = _scanHolder;

    /* collect selected items */
    if (_popTableSelections != nil) {
	for(path in _popTableSelections) {
	    row = (int) [path row];
	    section = (int) [path section];
	    ix = [self indexBySection: section row: row];
	    scan = [_popMediaSel scanItemByIx: (int) [self map: (int) ix]];

	    /* add in cloud and rating filters */
	    scan.minStars = [_starsView enabledCount];
	    scan.cloud = [_popCloudButton isSelected];

	    [scanHolder addScanItem: scan];
	    [_popTableView deselectRowAtIndexPath: path animated: NO];
	}
	_popTableSelections = [[NSMutableArray alloc] init];
    }
}

- (void) popDonePressed: (id) sender withData: (NSNumber *) number
{
    [self saveSelections];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    [_callbackObj performSelector: _finishedSel withObject: nil];
#pragma clang diagnostic pop
}

- (void) popCancelPressed: (id) sender withData: (NSNumber *) number
{
    [self setupDefaultMap];
    _popTableSelections = [[NSMutableArray alloc] init];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    [_callbackObj performSelector: _finishedSel withObject: nil];
#pragma clang diagnostic pop
}

- (void) updatePopViewInfo
{
    long count;

    count = _mapCount;
    if (count > 200) {
	_sectionCount = _maxSectionCount;
	_sectionSize = count / _sectionCount;
	_sectionExtras = count - _sectionSize * _sectionCount;
    }
    else {
	_sectionCount = 1;
	_sectionSize = count;
	_sectionExtras = 0;
    }

    [_popTableView reloadData];
}

- (void) tableView: (UITableView *) tview didSelectRowAtIndexPath: (NSIndexPath *) path
{
    long row;
    long section;
    long ix;

    row = [path row];
    section = [path section];
    ix = [self indexBySection: (int) section row: (int) row];

    if ([_popTableSelections containsObject: path])
	[_popTableSelections removeObject: path];
    else
	[_popTableSelections addObject: path];

    [_popTableView reloadRowsAtIndexPaths: [NSArray arrayWithObject: path]
		   withRowAnimation: UITableViewRowAnimationNone];
}

- (NSInteger) numberOfSectionsInTableView:(UITableView *) tview
{
    return _sectionCount;
}

- (NSInteger) tableView: (UITableView *)tview numberOfRowsInSection: (NSInteger) section
{
    /* the first _sectionExtras have an extra element */
    if (section < _sectionExtras)
	return _sectionSize+1;
    else
	return _sectionSize;
}

- (NSArray *) sectionIndexTitlesForTableView:(UITableView *) tview
{
    int i;
    int ix;
    NSMutableArray *ar;
    NSString *tstring;

    if (_sectionCount == 1)
	return nil;
    else {
	ar = [[NSMutableArray alloc] init];
	for(i=0;i<_sectionCount;i++) {
	    ix = [self indexBySection: i row: 0];
	    tstring = [_popMediaSel nameByIx: [self map: ix]];
	    if (tstring.length > 3)
		tstring = [tstring substringToIndex: 3];
	    [ar addObject: [tstring uppercaseString]];
	}
	return ar;
    }
}

/* return unmap index into uitableview's data */
- (unsigned int) indexBySection: (int) section row: (int) row
{
    unsigned int ix;

    ix = (int) (section * _sectionSize +
		(section < _sectionExtras? section : _sectionExtras) + row);

    return ix;
}

- (void) tableView: (UITableView *) tview
accessoryButtonTappedForRowWithIndexPath: (NSIndexPath *) path
{
    int ix;
    int row;
    int section;
    id<MFANMediaSel> childSel;

    row = (int) [path row];
    section = (int) [path section];
    ix = [self indexBySection: section row: row];
    ix = [self map: ix];	/* convert to mediaSel's space */

    if ([_popMediaSel hasChildByIx: ix]) {
	childSel = [_popMediaSel childByIx: ix];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
	/* typically, this calls [topEdit pushPopupMedia] */
	[_callbackObj performSelector: _nextSel withObject: childSel];
#pragma clang diagnostic pop
    }
}

- (UITableViewCell *) tableView: (UITableView *) tview cellForRowAtIndexPath: (NSIndexPath *)path
{
    unsigned int row;
    unsigned int section;
    unsigned int ix;
    UITableViewCell *cell;
    UIImage *image;
    UIView *backgroundView;
    NSString *details;
    CGFloat imageHeight;
    int realIx;
    CGFloat rowHeight;

    /* lookup section and row within section, all zero-based.  We
     * compute ix as the total depth into the combined array.  The
     * variable section gives the # of complete sections we have.
     */
    rowHeight = [_popMediaSel rowHeight];
    section = (int) [path section];
    row = (int) [path row];
    ix = (int) (section * _sectionSize +
		(section < _sectionExtras? section : _sectionExtras) + row);
    ix = [self indexBySection: section row: row];
    if (ix >= _mapCount)
	return nil;
    realIx = [self map: ix];

    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
				     reuseIdentifier: nil];
    backgroundView = [[UIView alloc] init];
    backgroundView.backgroundColor = [UIColor clearColor];
    cell.multipleSelectionBackgroundView = backgroundView;
    cell.textLabel.text = [_popMediaSel nameByIx: realIx];
    cell.textLabel.textColor = [MFANTopSettings textColor];
    cell.textLabel.font = [MFANTopSettings basicFontWithSize: 18];
    cell.textLabel.adjustsFontSizeToFitWidth = YES;

    details = [_popMediaSel subtitleByIx: realIx];
    if (details != nil) {
	cell.detailTextLabel.text = details;
	cell.detailTextLabel.textColor = [MFANTopSettings textColor];
    }

    if ([_popTableSelections containsObject: path]) {
	cell.accessoryType = UITableViewCellAccessoryCheckmark;
    }
    else if ([_popMediaSel hasChildByIx: realIx]) {
	CGRect frame;
	MFANCoreButton *iview;
	frame.size.height = rowHeight/3;
	frame.size.width = rowHeight/3;
	frame.origin.x = 0;
	frame.origin.y = 0;
	cell.accessoryType = UITableViewCellAccessoryNone;
	iview = [[MFANCoreButton alloc] initWithFrame: frame
					title: @"Chevron"
					color: [UIColor colorWithRed: 0.37
							green: 0.55
							blue: 1.0
							alpha: 1.0]];
	iview.userInteractionEnabled = YES;
	[iview addCallback: self withAction: @selector(nextPressed:)];
	[iview addCallbackContext: path];
	cell.accessoryView = iview;
    }
    else {
	cell.accessoryType = UITableViewCellAccessoryNone;
    }

    imageHeight = 8.0 * [_popMediaSel rowHeight] / 10;
    image = [_popMediaSel imageByIx: realIx size: imageHeight];
    if (image == nil) {
	if (imageHeight != _scaledDefaultImageSize || _scaledDefaultImage == nil) {
	    _scaledDefaultImage = resizeImage(_defaultImage, imageHeight);
	    _scaledDefaultImageSize = imageHeight;
	}
	image = _scaledDefaultImage;
    }

    /* make cell clear */
    cell.contentView.backgroundColor = [UIColor clearColor];
    cell.backgroundView.backgroundColor = [UIColor clearColor];
    cell.multipleSelectionBackgroundView.backgroundColor = [UIColor clearColor];
    cell.selectedBackgroundView.backgroundColor = [UIColor clearColor];
    cell.backgroundColor = [UIColor clearColor];

    [[cell imageView] setImage: image];
    return cell;
}

- (void) nextPressed: (id) context
{
    NSIndexPath *path = (NSIndexPath *) context;
    int ix;
    int row;
    int section;
    id<MFANMediaSel> childSel;

    row = (int) [path row];
    NSLog(@"NEXT PRESSED %d", (int) row);
    section = (int) [path section];
    ix = [self indexBySection: section row: row];
    ix = [self map: ix];	/* convert to mediaSel's space */

    if ([_popMediaSel hasChildByIx: ix]) {
	childSel = [_popMediaSel childByIx: ix];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
	/* typically, this calls [topEdit pushPopupMedia] */
	[_callbackObj performSelector: _nextSel withObject: childSel];
#pragma clang diagnostic pop
    }
}

@end /* MFANPopEdit */
