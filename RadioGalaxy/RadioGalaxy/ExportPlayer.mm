#import "AudioSlider.h"
#import "BufferSlider.h"
#import "MFANCoreButton.h"
#import "MFANIconButton.h"
#import "MarqueeLabel.h"
#import "MFANStreamPlayer.h"
#import "Settings.h"

#import "ExportPlayer.h"

#include "osp.h"

@implementation ExportPlayerEntry

- (ExportPlayerEntry *) initWithName: (NSString *) fileName {
    self = [super init];
    if (self != nil) {
	self.fileName = fileName;
	self.playing = false;
	uint64_t tlen = [fileName length];
	if (tlen > 4)
	    self.name = [fileName substringToIndex: tlen - 4];
	else
	    self.name = fileName;
    }

    return self;
}

@end


@implementation ExportPlayer {
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
    ExportPlayerEntry *_playingEntry;
    BOOL _isPlaying;
    BOOL _isPaused;;
    int32_t _selectedRow;
    UIColor *_selectedColor;
}

- (NSString *) pathNameForFile: (NSString *) fileName {
    return [NSString stringWithFormat: @"%@/%@", _docDirName, fileName];
}

- (ExportPlayer *) initWithViewCont: (ViewController *) vc {
    self.frame = vc.activeFrame;
    CGRect frame = vc.activeFrame;

    self = [super initWithFrame: frame];
    if (self != nil) {
	CGRect screenFrame = self.frame;
	screenFrame.origin.y = 0;	// because these are relative to parent view

	NSLog(@"FRANE EXPORTEDPLAYER %fx%f@%f.%f",
	      screenFrame.size.width, screenFrame.size.height,
	      screenFrame.origin.x, screenFrame.origin.y);

	_vc = vc;
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

	_recordings = [[NSMutableArray alloc] init];

	NSArray *paths;
	paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
	_docDirName = paths[0];

	[self populateRecordings];

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
    ExportPlayerEntry *entry = _recordings[row];

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
		    [self stopEntry: entry];
		} else {
		    [self playEntry: entry];
		}
		complete(true);
	    }];
    playAction.backgroundColor = [UIColor blueColor];

    UIContextualAction *deleteAction =
	[UIContextualAction contextualActionWithStyle:UIContextualActionStyleDestructive
						title:@"Delete"
					      handler:^(UIContextualAction *action,
							UIView *sourceView,
							void (^complete)(BOOL)) {
		NSError *error = nil;
		BOOL status;

		NSLog(@"DELETE action");
		status = [[NSFileManager defaultManager]
			     removeItemAtPath: [self pathNameForFile: entry.fileName]
					error: &error];
		if (!status) {
		    NSLog(@"failed to delete main file=%@", entry.fileName);
		} else {
		    [self->_recordings removeObjectAtIndex: row];
		    self->_selectedRow = -1;
		    [self->_fileTableView reloadData];
		}
		complete(true);
	    }];
    deleteAction.backgroundColor = [UIColor redColor];

    UISwipeActionsConfiguration *config =
	[UISwipeActionsConfiguration configurationWithActions: @[playAction, deleteAction]];

    config.performsFirstActionWithFullSwipe = true;

    return config;
}

- (void) stopEntry: (ExportPlayerEntry *) entry {
    if (_player) {
	[_player stop];
	_player = nil;
	[_sliderView monitor: nil];
	entry.playing = false;
    }
}

- (void) getMetadataForEntry: (ExportPlayerEntry *) entry {
    NSString *entryPath = [NSString stringWithFormat: @"%@/%@", _docDirName, entry.fileName];
    NSURL *musicUrl = [NSURL fileURLWithPath: entryPath];
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL: musicUrl options: nil];
    NSArray *metadata = [asset commonMetadata];
    NSString *album;
    NSString *artist;
    NSString *song;

    for(AVMetadataItem *item in metadata) {
	if ([[item commonKey] isEqualToString: AVMetadataCommonKeyTitle])
	    song = (NSString *) [item value];
	else if ([[item commonKey] isEqualToString: AVMetadataCommonKeyArtist])
	    artist = (NSString *) [item value];
	else if ([[item commonKey] isEqualToString: AVMetadataCommonKeyAlbumName])
	    album = (NSString *) [item value];
    }

    entry.song = song;
    entry.artist = artist;
    entry.album = album;
}

- (void) playEntry: (ExportPlayerEntry *) entry {
    if (_playingEntry != nil) {
	[self stopEntry: _playingEntry];
	_playingEntry = nil;
    }

    NSString *entryPath = [NSString stringWithFormat: @"%@/%@", _docDirName, entry.fileName];
    NSError *error = nil;
    NSURL *musicUrl = [NSURL fileURLWithPath: entryPath];

    _player = [[AVAudioPlayer alloc] initWithContentsOfURL: musicUrl
						     error: &error];
    if (_player == nil) {
	NSLog(@"file player creation failed");
	return;
    }

    _player.delegate = self;

    [_player prepareToPlay];

    [_player play];

    [_sliderView monitor: _player];

    _player.volume = 1.0;
    _player.numberOfLoops = 0;
    _isPaused = false;
    _isPlaying = true;
    [self adjustPlayButton];

    NSString *label;
    if ([entry.album length] > 0)
	label = [NSString stringWithFormat: @"%@ - %@ - %@",
			  entry.artist, entry.song, entry.album];
    else
	label = [NSString stringWithFormat: @"%@ - %@",
			  entry.artist, entry.song];

    [_marquee setText: label];

    entry.playing = true;
    _playingEntry = entry;
}

- (void) audioPlayerDidFinishPlaying: (AVAudioPlayer *) player
			successfully: (BOOL) success {
    uint64_t count = [_recordings count];
    uint64_t i;

    if (_playingEntry != nil) {
	for(i = 0;i<count;i++) {
	    if (_recordings[i] == _playingEntry)
		break;
	}

	if (++i >= count)
	    i = 0;
	ExportPlayerEntry *entry = (ExportPlayerEntry *) _recordings[i];
	[self playEntry: entry];
    }
}

- (void) populateRecordings {
    NSArray *dirArray;
    NSString *entry;

    dirArray = [[NSFileManager defaultManager]
		   contentsOfDirectoryAtPath: _docDirName
				       error:nil];
    if (dirArray != nil) {
	for(entry in dirArray) {
	    if ( [entry hasSuffix: @".mp3"] ||
		 [entry hasSuffix: @".m4a"] ||
		 [entry hasSuffix: @".aac"]) {

		ExportPlayerEntry *exportEntry = [[ExportPlayerEntry alloc] initWithName: entry];
		[_recordings addObject: exportEntry];

		[self getMetadataForEntry: exportEntry];
	    }
	}
    }

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

    /* lookup section and row within section, all zero-based.  We
     * compute ix as the total depth into the combined array.  The
     * variable section gives the # of complete sections we have.
     */
    section = (int) [path section];
    row = (int) [path row];

    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
				     reuseIdentifier: nil];
    ExportPlayerEntry *entry = _recordings[row];
    cell.textLabel.text = (NSString *) entry.song;
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

    if ([entry.album length] > 0)
	details = [NSString stringWithFormat: @"%@ - %@",
			    entry.artist, entry.album];
    else
	details = entry.artist;

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

- (void) editPressed: (id) junk {
}

- (void) donePressed: (id) junk {
    if (_playingEntry != nil) {
	[self stopEntry: _playingEntry];
	_playingEntry = nil;
    }
    [_vc popTopView];
}

- (void) skipFwdPressed:(id) junk withData: junk2 {
    NSLog(@"+20");
    if (_player != nil) {
	float current = _player.currentTime;
	current += 20.0;
	if (current >= _player.duration)
	    current = _player.duration;
	_player.currentTime = current;
    }
}

- (void) skipBackPressed:(id) junk withData: junk2 {
    NSLog(@"-20");
    if (_player != nil) {
	float current = _player.currentTime;
	current -= 20.0;
	if (current < 0.0)
	    current = 0.0;
	_player.currentTime = current;
    }
}

- (void) stopPressed:(id) junk withData: junk2 {
    if (_playingEntry != nil) {
	[self stopEntry: _playingEntry];
	_playingEntry = nil;
    }
    _isPaused = false;
    _isPlaying = false;
    [self adjustPlayButton];
    NSLog(@"STOP");
}

- (void) updateIOSCenter: (NSString *) song {
#if 0
    _nowPlayingInfo = [[NSMutableDictionary alloc] init];
    [_nowPlayingInfo setObject: [NSNumber numberWithDouble: 1.0]
			forKey: MPNowPlayingInfoPropertyPlaybackRate];

    MFANStreamPlayer *player = [_signView getCurrentPlayer];

    [_nowPlayingInfo setObject: [NSNumber numberWithFloat: 0.0]
			forKey: MPNowPlayingInfoPropertyElapsedPlaybackTime];
    [_nowPlayingInfo setObject: [NSNumber numberWithFloat: 300.0]
			forKey: MPMediaItemPropertyPlaybackDuration];

    [_nowPlayingInfo setObject: [NSNumber numberWithUnsignedInt: 1]
			forKey: MPNowPlayingInfoPropertyPlaybackQueueIndex];
    /* internet radio, queue should be really large, so that
     * we never have queueIndex bigger than queueCount.
     */
    [_nowPlayingInfo setObject: [NSNumber numberWithUnsignedInt:
					      (int) 2000]
			forKey: MPNowPlayingInfoPropertyPlaybackQueueCount];
    if (song != nil)
	[_nowPlayingInfo setObject: song forKey: MPMediaItemPropertyTitle];

    MPNowPlayingInfoCenter *infoCenter = [MPNowPlayingInfoCenter defaultCenter];
    [infoCenter setNowPlayingInfo: _nowPlayingInfo];

    if ([player isPaused]) {
	infoCenter.playbackState =  MPNowPlayingPlaybackStatePaused;
    } else {
	infoCenter.playbackState =  MPNowPlayingPlaybackStatePlaying;
    }
#endif
}

// This is really handling both play and pause
- (void) playPressed: (id) sender withData: (NSNumber *)movement {
    if (_isPlaying) {
	_isPaused = true;
	_isPlaying = false;
	[_player pause];
    } else if (_isPaused) {
	_isPaused = false;
	_isPlaying = true;
	[_player play];
    } else {
	if (_selectedRow >= 0) {
	    ExportPlayerEntry *entry;
	    entry = _recordings[_selectedRow];
	    [self playEntry: entry];
	}
    }
    [self adjustPlayButton];
}

- (void) adjustPlayButton {
    if (_isPlaying)
	[_playButton setTitle:@"Pause"];
    else
	[_playButton setTitle:@"Play"];
}

- (void) activateTopView {
    [_marquee restartLabel];
}

- (void) deactivateTopView {
    return;
}

- (bool) ok2Quit {
    return false;
}

@end
