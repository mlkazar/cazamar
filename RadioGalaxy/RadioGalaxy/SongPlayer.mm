#import "AudioSlider.h"
#import "BaseSlider.h"
#import "BufferSlider.h"
#import "ExportEntry.h"
#import "MFANCoreButton.h"
#import "MFANIconButton.h"
#import "MarqueeLabel.h"
#import "MFANStreamPlayer.h"
#import "Settings.h"
#import "SignStation.h"
#import "SignView.h"

#import "SongPlayer.h"

#include "osp.h"

@implementation SongPlayer {
    MarqueeLabel *_marquee;
    MFANCoreButton *_playButton;
    MFANIconButton *_stopButton;
    MFANCoreButton *_skipFwdButton;
    MFANCoreButton *_skipBackButton;
    MFANCoreButton *_moreButton;
    MFANCoreButton *_doneButton;
    UITableView *_fileTableView;
    RadioHistory *_history;
    ViewController *_vc;
    NSMutableDictionary *_nowPlayingInfo;
    AudioSlider *_sliderView;
    Settings *_settings;
    NSString *_playingSong;
    NSMutableArray *_recordings;
    NSString *_docDirName;
    AVAudioPlayer *_player;
    ExportEntry *_playingEntry;
    BOOL _isPlaying;
    BOOL _isPaused;;
    int32_t _selectedRow;
    UIColor *_selectedColor;
    UIImage *_recordImage;

    // basic context info
    MFANAqStreamRecordings *_streamRecordings;
    MFANAqStreamBuffer *_buffer;
    SignStation *_station;
    SignView *_signView;
}

- (SongPlayer *) initWithStation: (SignStation *) station
			  buffer: (MFANAqStreamBuffer *) buffer
			signView: (SignView *) signView
			viewCont: (ViewController *) vc {
    self.frame = vc.activeFrame;
    CGRect frame = vc.activeFrame;

    self = [super initWithFrame: frame];
    if (self != nil) {
	CGRect screenFrame = self.frame;
	screenFrame.origin.y = 0;	// because these are relative to parent view

	NSLog(@"FRANE EXPORTEDPLAYER %fx%f@%f.%f",
	      screenFrame.size.width, screenFrame.size.height,
	      screenFrame.origin.x, screenFrame.origin.y);

	// establish our context
	_station = station;
	_buffer = buffer;
	_streamRecordings = _buffer.streamRecordings;
	_recordings = _streamRecordings.recordings;
	_vc = vc;
	_signView = signView;

	_isPlaying = false;
	_isPaused = false;
	_selectedRow = -1;
	_selectedColor = [UIColor colorWithRed: 1.0
					 green: 1.0
					  blue: 0.8
					 alpha: 1.0];

	CGRect fileFrame;

	// We reserve vertMargin at the top and bottom of the screen.
	// Then the signFrame gets 90% of the remaining space, the
	// marquee gets the next 5% and the control buttons get the
	// last 10%
	float usableHeight = screenFrame.size.height;

	fileFrame = screenFrame;
	fileFrame.size.height = usableHeight * 0.80;

	// remainder from signFrame height, divided by # of bars
	float barFraction = (1.0-0.80) / 4;
	float lineHeight = barFraction * usableHeight;

	_fileTableView = [[UITableView alloc] initWithFrame: fileFrame
						  style:UITableViewStylePlain];
	[_fileTableView setAllowsMultipleSelection: YES];
	[_fileTableView setDataSource: self];
	[_fileTableView setDelegate: self];
	[_fileTableView setRowHeight: 1.8 * lineHeight];
	[_fileTableView setSectionIndexMinimumDisplayRowCount: 20];
	[_fileTableView setBackgroundColor: [UIColor whiteColor]];
	_fileTableView.sectionIndexBackgroundColor = [UIColor clearColor];
	[_fileTableView setSeparatorStyle: UITableViewCellSeparatorStyleNone];
	[self addSubview: _fileTableView];

	UIColor *borderColor = [UIColor colorWithRed: 0.0
					       green: 0.5
						blue: 0.0
					       alpha: 1.0];

	CGRect startFrame = screenFrame;
	startFrame.origin.y = fileFrame.size.height;
	startFrame.size.height = lineHeight;
	startFrame.size.width = screenFrame.size.width / 2;

	MFANCoreButton *_doneButton= [[MFANCoreButton alloc]
					     initWithFrame: startFrame
						     title: @"None"
						     color: [UIColor blackColor]
					   backgroundColor: [UIColor greenColor]];
	[_doneButton setBackgroundColor:
		      [UIColor colorWithRed: 0.0
				      green: 0.75
				       blue:0.0
				      alpha: 1.0]];
	_doneButton.layer.borderWidth = 2.0;
	_doneButton.layer.borderColor = borderColor.CGColor;
	[_doneButton setClearText: @"Done"];
	[_doneButton addCallback: self withAction: @selector(donePressed:)];
	[self addSubview: _doneButton];

	startFrame.origin.x += screenFrame.size.width / 2;

	MFANCoreButton *_moreButton= [[MFANCoreButton alloc]
					     initWithFrame: startFrame
						     title: @"None"
						     color: [UIColor blackColor]
					   backgroundColor: [UIColor greenColor]];
	[_moreButton setBackgroundColor:
		   [UIColor colorWithRed: 0.0
				   green: 0.75
				    blue:0.0
				   alpha: 1.0]];
	_moreButton.layer.borderWidth = 2.0;
	_moreButton.layer.borderColor = borderColor.CGColor;
	[_moreButton setClearText: @"More..."];
	[_moreButton addCallback: self withAction: @selector(morePressed:)];
	[self addSubview: _moreButton];

	CGRect marqueeFrame = screenFrame;
	marqueeFrame.origin.y = startFrame.origin.y + startFrame.size.height;
	marqueeFrame.size.height = lineHeight;
	MarqueeLabel *marquee = [[MarqueeLabel alloc] initWithFrame: marqueeFrame];
	_marquee = marquee;
	[marquee setTextColor: [UIColor blackColor]];
	[marquee setTextAlignment: NSTextAlignmentCenter];
	[marquee setFont: [UIFont fontWithName: @"Arial-BoldMT" size: 30]];
	[self addSubview: marquee];
	NSLog(@"setting marquee frame to y=%f height=%f",
	      marqueeFrame.origin.y, marqueeFrame.size.height);

	// and put something there.
	[marquee setNeedsDisplay];

	CGRect sliderFrame;
	sliderFrame = marqueeFrame;
	sliderFrame.origin.y = marqueeFrame.origin.y + marqueeFrame.size.height;
	sliderFrame.size.height = lineHeight;

	_sliderView = [[AudioSlider alloc] initWithFrame: (CGRect) sliderFrame
						   apply: ^(float value) {
		// get rid of this if we don't use it
		return;
	    }
						viewCont: (ViewController *) vc];
	[self addSubview: _sliderView];

	CGRect buttonFrame = sliderFrame;
	buttonFrame.origin.y += lineHeight;

	float smallButtonWidth = buttonFrame.size.height;
	float largeButtonWidth = 2*buttonFrame.size.height;

	buttonFrame.size.width = largeButtonWidth;
	buttonFrame.origin.x = screenFrame.size.width/5 - largeButtonWidth/2;

	_skipBackButton = [[MFANCoreButton alloc]
			      initWithFrame: buttonFrame
				      title: @"Blank"
				      color: [UIColor blackColor]];
	[_skipBackButton addCallback: self withAction:@selector(skipBackPressed:withData:)];
	[_skipBackButton setClearText: @"-20"];
	[self addSubview: _skipBackButton];

	buttonFrame.size.width = smallButtonWidth;
	buttonFrame.origin.x = 2*screenFrame.size.width/5 - smallButtonWidth/2;
	MFANCoreButton *playButton = [[MFANCoreButton alloc]
					      initWithFrame: buttonFrame
						      title:@"Play"
						      color: [UIColor blackColor]];
	_playButton = playButton;
	[playButton addCallback: self withAction:@selector(playPressed:withData:)];
	[self addSubview: playButton];

	buttonFrame.size.width = smallButtonWidth;
	buttonFrame.origin.x = 3*screenFrame.size.width/5 - smallButtonWidth/2;
	_stopButton = [[MFANIconButton alloc]
			  initWithFrame: buttonFrame
				  title: @""
				  color: [UIColor blackColor]
				   file:@"icon-stop.png"];
	[_stopButton addCallback: self withAction: @selector(stopPressed:withData:)];
	[self addSubview: _stopButton];

	buttonFrame.size.width = largeButtonWidth;
	buttonFrame.origin.x = 4*screenFrame.size.width/5 - largeButtonWidth/2;
	_skipFwdButton = [[MFANCoreButton alloc]
			      initWithFrame: buttonFrame
				      title: @"Blank"
				      color: [UIColor blackColor]];
	[_skipFwdButton addCallback: self withAction:@selector(skipFwdPressed:withData:)];
	[_skipFwdButton setClearText: @"+20"];
	[self addSubview: _skipFwdButton];

	_history = [[RadioHistory alloc] initWithViewController:vc];
	[_history setCallback: self WithSel: @selector(historyDone:)];

	_recordImage = [UIImage imageNamed: @"record-152.png"];

	NSArray *paths;
	paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
	_docDirName = paths[0];

	[self setBackgroundColor: [UIColor whiteColor]];

	[_vc pushTopView: self];
    }

    return self;
}

- (void) tableView: (UITableView *) tview
didSelectRowAtIndexPath:(NSIndexPath *) path {
    long row = [path row];
    if (row == _selectedRow)
	_selectedRow = -1;
    else
	_selectedRow = (uint32_t) row;

#if 0
    [_fileTableView reloadRowsAtIndexPaths:
		  [NSArray arrayWithObject: path]
			  withRowAnimation: UITableViewRowAnimationAutomatic];
#else
    [_fileTableView reloadData];
#endif

    NSLog(@"did selection row=%ld", row);
}

- (UISwipeActionsConfiguration *) tableView: (UITableView *) tview
trailingSwipeActionsConfigurationForRowAtIndexPath: (NSIndexPath *) path
{
    long row = [path row];
    ExportEntry *entry = _recordings[row];

    _selectedRow = (uint32_t) row;

    NSString *playString;
    if (entry.playing) {
	playString = @"Stop Playing";
    } else {
	playString = @"Play";
    }

    UIContextualAction *playAction =
	[UIContextualAction contextualActionWithStyle:UIContextualActionStyleNormal
						title:playString
					      handler:^(UIContextualAction *action,
							UIView *sourceView,
							void (^complete)(BOOL)) {
		NSLog(@"PLAY action");
		if (entry.playing) {
		    [self->_signView stopRadioForceReset: false fromCarPlay: false];
		} else {
		    [self->_signView seek: entry.start relative: false];
		}
		complete(true);
	    }];
    playAction.backgroundColor = [UIColor blueColor];

    UISwipeActionsConfiguration *config =
	[UISwipeActionsConfiguration configurationWithActions: @[playAction]];

    config.performsFirstActionWithFullSwipe = true;

    return config;
}

- (void) stopEntry: (ExportEntry *) entry {
    [_signView stopRadioForceReset: false fromCarPlay: false];
    entry.playing = false;
}

- (void) playEntry: (ExportEntry *) entry {
    return;
}

- (void) historyDone: (id) junk {
    [_vc popTopView];
}

- (void) morePressed: (id) junk {
}

- (UITableViewCell *) tableView: (UITableView *) tview cellForRowAtIndexPath: (NSIndexPath *)path
{
    unsigned int row;
    unsigned int section;
    UITableViewCell *cell;
    UIView *backgroundView;
    NSString *details;
    NSString *song;
    NSString *album;
    NSString *artist;

    /* lookup section and row within section, all zero-based.  We
     * compute ix as the total depth into the combined array.  The
     * variable section gives the # of complete sections we have.
     */
    section = (int) [path section];
    row = (int) [path row];

    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
				     reuseIdentifier: nil];
    ExportEntry *entry = _recordings[row];
    [ViewController splitLabel: entry.label
			 group: &artist
			  song: &song
			 album: &album];
    cell.textLabel.text = song;
    cell.textLabel.font = [UIFont fontWithName: @"Arial-BoldMT" size: 24];
    cell.textLabel.adjustsFontSizeToFitWidth = YES;
    cell.textLabel.textColor = [UIColor blackColor];

    if (_selectedRow == row) {
	cell.contentView.backgroundColor = _selectedColor;
    } else {
	cell.contentView.backgroundColor = [UIColor whiteColor];
    }

    backgroundView = [[UIView alloc] init];
    backgroundView.backgroundColor = [UIColor clearColor];
    cell.multipleSelectionBackgroundView = backgroundView;

    details = [NSString stringWithFormat: @"%@ - %@",
			[BaseSlider stringFromTime: (entry.end-entry.start)],
			artist];

    cell.detailTextLabel.text = details;
    cell.detailTextLabel.font = [UIFont fontWithName: @"Arial-BoldMT" size: 14];
    cell.detailTextLabel.textColor = [UIColor colorWithRed: 0.0
						     green: 0.5
						      blue: 0.0
						     alpha: 1.0];
    cell.detailTextLabel.adjustsFontSizeToFitWidth = YES;

    return cell;
}

- (NSInteger) numberOfSectionsInTableView:(UITableView *) tview {
    return 1;
}

- (NSInteger) tableView: (UITableView *)tview numberOfRowsInSection: (NSInteger) section {
    // return count of # of rows of data we have
    return [_recordings count];
}

- (void) donePressed: (id) junk {
    [_vc popTopView];
}

- (void) skipFwdPressed:(id) junk withData: junk2 {
    NSLog(@"+20");
    [_signView seek: 20 relative: true];
}

- (void) skipBackPressed:(id) junk withData: junk2 {
    NSLog(@"-20");
    [_signView seek: -20 relative: true];
}

- (void) stopPressed:(id) junk withData: junk2 {
    [_signView stopRadioForceReset: false fromCarPlay: false];
    NSLog(@"STOP");
}

// This is really handling both play and pause
- (void) playPressed: (id) sender withData: (NSNumber *)movement {
    [self adjustPlayButton];
    [_signView tvPlayPauseSong];
}

- (void) adjustPlayButton {
    if (_isPlaying)
	[_playButton setTitle:@"Pause"];
    else
	[_playButton setTitle:@"Play"];
}

- (void) tvActivate {
    [_marquee restartLabel];
}

- (void) tvDeactivate {
    return;
}

- (bool) tvOk2Quit {
    return false;
}

- (uint64_t) getCurrentIndex {
    if (_playingEntry != nil) {
	uint64_t count;
	uint64_t ix;
	ExportEntry *entry;

	count = [_recordings count];
	for(ix = 0; ix<count; ix++) {
	    entry = _recordings[ix];
	    if (entry == _playingEntry)
		return ix;
	}
	return 0;
    } else {
	return 0;
    }
}

- (bool) tvPlayPauseSong {
    [self playPressed: nil withData: nil];
    return false;
}

- (bool) tvNextSong {
    uint64_t count = [_recordings count];
    int64_t ix = [self getCurrentIndex];
    if (++ix >= count)
	ix = 0;
    if (_playingEntry != nil) {
	[self stopEntry: _playingEntry];
	_playingEntry = nil;
    }

    ExportEntry *entry = _recordings[ix];
    [self playEntry: entry];

    return false;
}

- (bool) tvPrevSong {
    uint64_t count = [_recordings count];
    int64_t ix = [self getCurrentIndex];
    if (ix == 0)
	ix = count-1;
    else
	ix--;

    if (_playingEntry != nil) {
	[self stopEntry: _playingEntry];
	_playingEntry = nil;
    }

    ExportEntry *entry = _recordings[ix];
    [self playEntry: entry];

    return false;
}

- (bool) tvIsPlaying {
    if (_player != nil && !_isPaused)
	return true;
    else
	return false;
}

- (bool) tvPause {
    if (_player != nil) {
	[_player pause];
    }
    return false;
}

- (bool) tvResume {
    if (_player != nil) {
	[_player play];
    }
    return false;
}

@end
