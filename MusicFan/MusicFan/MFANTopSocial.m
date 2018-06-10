//
//  MFANTopSocial.m
//  DJ To Go
//
//  Created by Michael Kazar on 12/4/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANTopSocial.h"
#import "MFANCGUtil.h"
#import "MFANRButton.h"
#import "MFANTopSettings.h"
#import "MFANViewController.h"

@implementation ListSocialEntry {
    /* s1, s2, s3 are properties */
}

- (ListSocialEntry *) init
{
    self = [super init];
    if (self) {
	_s1 = nil;
	_s2 = nil;
	_s3 = nil;
    }
    return self;
}
@end

@implementation MFANListSocial {
    UIView *_parent;
    UITableView *_tableView;
    NSMutableArray *_entries;
}

- (MFANListSocial *) initWithParent:(UIView *) edit
			  tableView: (UITableView *) tview;
{
    self = [super init];
    if (self) {
	_parent = edit;
	_tableView = tview;
	_entries = [[NSMutableArray alloc] init];
    }

    return self;
}

- (NSInteger) tableView: (UITableView *)tview numberOfRowsInSection: (NSInteger) section
{
    return (NSInteger) [_entries count];
}

- (UITableViewCell *) tableView: (UITableView *) tview cellForRowAtIndexPath: (NSIndexPath *)path
{

    unsigned long ix;
    UITableViewCell *cell;
    UIColor *textColor;
    MFANCommEntry *entry;
    NSString *tstring;

    ix = [path row];
    
    if (ix >= [_entries count])
	return nil;

    entry = [_entries objectAtIndex: ix];
    if (entry == nil)
	return nil;

    textColor = [MFANTopSettings textColor];

    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
				    reuseIdentifier: nil];
    cell.textLabel.font = [MFANTopSettings basicFontWithSize: 18];
    cell.textLabel.adjustsFontSizeToFitWidth = YES;

    tstring = [entry.song stringByAppendingString: @": "];
    tstring = [tstring stringByAppendingString: entry.artist];
    cell.textLabel.text = tstring;
    cell.textLabel.textColor = textColor;

    cell.detailTextLabel.text = entry.who;
    cell.detailTextLabel.textColor = textColor;

    cell.backgroundColor = [UIColor clearColor];

    return cell;
}

- (NSMutableArray *) entries
{
    return _entries;
}

@end

@implementation MFANTopSocial {
    CGRect _appFrame;
    CGFloat _appVMargin;
    CGFloat _appHMargin;
    CGFloat _buttonWidth;
    CGFloat _buttonMargin;
    CGFloat _buttonHeight;
    UITableView *_tableView;
    MFANListSocial *_listSocial;
    MFANRButton *_doneButton;
    MFANViewController *_viewCont;
    MFANComm *_comm;
    NSTimer *_timer;
}

- (void) activateTop
{
    [self fillEntries];

    if (_timer == nil) {
	_timer = [NSTimer scheduledTimerWithTimeInterval: 3.0
			  target: self
			  selector: @selector(updateView:)
			  userInfo: nil
			  repeats: YES];
    }
}

- (void) updateView: (id) junk
{
    [self fillEntries];
}

- (void) fillEntries
{
    NSMutableArray *array;
    int32_t code;

    array = [_listSocial entries];
    [array removeAllObjects];
    code = [_comm getAllPlaying: array];
    [_tableView reloadData];
}

- (void) deactivateTop
{
    NSMutableArray *array;
    array = [_listSocial entries];
    [array removeAllObjects];

    if (_timer != nil) {
	[_timer invalidate];
	_timer = nil;
    }

    return;
}

- (id)initWithFrame:(CGRect)frame
     viewController: (MFANViewController *) viewCont
	       comm: (MFANComm *) comm
{
    CGRect tframe;
    CGRect buttonFrame;

    self = [super initWithFrame:frame];
    if (self) {
	_viewCont = viewCont;

	_appVMargin = 20.0;
	_appHMargin = 2.0;

	_comm = comm;
	_timer = nil;

	_appFrame = frame;
	_appFrame.origin.x += _appHMargin;
	_appFrame.origin.y += _appVMargin;
	_appFrame.size.width -= 2 * _appHMargin;
	_appFrame.size.height -= 2 * _appVMargin;

	_buttonHeight = 50.0;
	_buttonWidth = 100.0;
	_buttonMargin = 5.0;

	tframe = _appFrame;
	tframe.size.height *= 0.8;

	_tableView = [[UITableView alloc] initWithFrame: tframe
					  style:UITableViewStylePlain];
	_listSocial = [[MFANListSocial alloc] initWithParent: self tableView: _tableView];
	[_tableView setDelegate: _listSocial];
	[_tableView setDataSource: _listSocial];
	_tableView.backgroundColor = [UIColor clearColor];
	_tableView.separatorStyle = UITableViewCellSeparatorStyleNone;
	_tableView.separatorColor = [UIColor blackColor];

	[self addSubview: _tableView];

	buttonFrame.origin.x = (_appFrame.size.width - _buttonWidth) / 2;
	buttonFrame.origin.y = (_appFrame.origin.y + _appFrame.size.height -
				(_buttonHeight + _buttonMargin));
	buttonFrame.size.width = _buttonWidth;
	buttonFrame.size.height = _buttonHeight;

	_doneButton = [[MFANRButton alloc] initWithFrame: buttonFrame
					   title: @"Done"
					   color: [UIColor greenColor]
					   fontSize: 16];
	[self addSubview: _doneButton];
	[_doneButton addCallback: self
		     withAction: @selector(donePressed:withData:)
		     value: nil];
    }

    return self;
}

- (void) donePressed: (id) sender withData: (NSNumber *) number
{
    [_viewCont restorePlayer];
}


- (void)drawRect:(CGRect)rect
{
    drawBackground(rect);
}

@end
