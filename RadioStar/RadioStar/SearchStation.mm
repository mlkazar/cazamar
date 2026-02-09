#import "SearchStation.h"
#import "MFANCGUtil.h"

@implementation SearchStation {
    UISearchBar *_searchBar;
    UITableView *_stationTable;
    float _rowHeight;
    UIImage *_genericImage;
    UIImage *_scaledGenericImage;
    bool _canceled;

    id _callbackObj;
    SEL _callbackSel;
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
	searchFrame = frame;
	searchFrame.size.height *= 0.1;
	_rowHeight = 72.0;

	_searchBar = [[UISearchBar alloc] initWithFrame: searchFrame];
	_searchBar.showsCancelButton = YES;
	_searchBar.delegate = self;
	[self addSubview: _searchBar];

	tableFrame = frame;
	tableFrame.origin.y = searchFrame.origin.y + searchFrame.size.height;
	tableFrame.size.height *= 0.9;

	_genericImage = [UIImage imageNamed: @"radio-icon.png"];
	_scaledGenericImage = resizeImage(_genericImage, 32);

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

- (void) searchBarCancelButtonClicked:(UISearchBar *)searchBar {
    NSLog(@"search canceled");
    _canceled = YES;
    [_searchBar resignFirstResponder];
    [self removeFromSuperview];
    [self doNotify];
}

- (void) searchBarSearchButtonClicked:(UISearchBar *)searchBar {
    [_searchBar resignFirstResponder];
    _canceled = NO;
    NSLog(@"search test is %@", _searchBar.text);
    [self removeFromSuperview];
    [self doNotify];
}

- (void) setCallback: (id) object WithSel: (SEL) selector {
    _callbackObj = object;
    _callbackSel = selector;
}

- (BOOL)tableView:(UITableView *)tableView 
canEditRowAtIndexPath:(NSIndexPath *)indexPath {
    long row;
    row = [indexPath row];
    NSLog(@"canEditRow %d", row);
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
    }
}

// Called when someome selects a row
- (void) tableView: (UITableView *) tview didSelectRowAtIndexPath: (NSIndexPath *) path
{
    long row;
    long section;
    long ix;

    row = [path row];

    [_stationTable reloadRowsAtIndexPaths: [NSArray arrayWithObject: path]
			 withRowAnimation: UITableViewRowAnimationNone];
}

- (NSInteger) numberOfSectionsInTableView:(UITableView *) tview
{
    return 1;
}

- (NSInteger) tableView: (UITableView *)tview numberOfRowsInSection: (NSInteger) section
{
    return 3;
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
    NSLog(@"in tapped accessory for row %d", [path row]);
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

    /* lookup section and row within section, all zero-based.  We
     * compute ix as the total depth into the combined array.  The
     * variable section gives the # of complete sections we have.
     */
    section = (int) [path section];
    row = (int) [path row];

    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
				     reuseIdentifier: nil];
    backgroundView = [[UIView alloc] init];
    backgroundView.backgroundColor = [UIColor clearColor];
    cell.multipleSelectionBackgroundView = backgroundView;
    if (row == 2)
	cell.textLabel.text = @"This is a very long label that has a lot of text in it.  Really lots.";
    else
	cell.textLabel.text = @"this is a label";
    cell.textLabel.textColor = [UIColor blueColor];
    cell.textLabel.font = [UIFont fontWithName: @"Arial-BoldMT" size: 32];
    cell.textLabel.adjustsFontSizeToFitWidth = YES;

    cell.detailTextLabel.text = @"details";
    cell.detailTextLabel.font = [UIFont fontWithName: @"Arial-BoldMT" size: 16];
    cell.detailTextLabel.textColor = [UIColor redColor];

    if (row == 1) {
	cell.accessoryType = UITableViewCellAccessoryCheckmark;
    } else {
	cell.accessoryType = UITableViewCellAccessoryNone;
    }

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
    [[cell imageView] setImage: _scaledGenericImage];
#endif
    /* make cell clear */
    cell.contentView.backgroundColor = [UIColor clearColor];
    cell.backgroundView.backgroundColor = [UIColor clearColor];
    cell.multipleSelectionBackgroundView.backgroundColor = [UIColor clearColor];
    cell.selectedBackgroundView.backgroundColor = [UIColor clearColor];
    cell.backgroundColor = [UIColor clearColor];

    return cell;
}

@end
