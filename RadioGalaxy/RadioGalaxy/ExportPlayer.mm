#import "BufferSlider.h"
#import "MFANCoreButton.h"
#import "MFANIconButton.h"
#import "MarqueeLabel.h"
#import "MFANStreamPlayer.h"
#import "Settings.h"

#import "ExportPlayer.h"

#include "osp.h"

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
    BufferSlider *_sliderView;
    Settings *_settings;
    NSString *_playingSong;
    NSMutableArray *_recordings;
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

	_fileTableView = [[UITableView alloc] initWithFrame: fileFrame
						  style:UITableViewStylePlain];
	[_fileTableView setAllowsMultipleSelection: YES];
	[_fileTableView setDataSource: self];
	[_fileTableView setDelegate: self];
	[_fileTableView setRowHeight: 1.2 * usableHeight];
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
	startFrame.size.height = usableHeight * barFraction;
	startFrame.size.width = screenFrame.size.width / 3;
	MFANCoreButton *_addButton= [[MFANCoreButton alloc]
					  initWithFrame: startFrame
						  title: @"None"
						  color: [UIColor blackColor]
					backgroundColor: [UIColor greenColor]];
	[_addButton setBackgroundColor:
		 [UIColor colorWithRed: 0.0
				 green: 0.75
				  blue:0.0
				 alpha: 1.0]];
	_addButton.layer.borderWidth = 2.0;
	_addButton.layer.borderColor = borderColor.CGColor;
	[_addButton setClearText: @"Edit Metadata"];
	[_addButton addCallback: self withAction: @selector(editPressed:)];
	[self addSubview: _addButton];

	startFrame.origin.x += screenFrame.size.width/3;
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

	startFrame.origin.x += screenFrame.size.width/3;

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
	marqueeFrame.size.height = usableHeight * barFraction;
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
	sliderFrame.size.height = usableHeight * barFraction;

#if 0
	_sliderView = [[AVPlayerSlider alloc] initWithFrame: (CGRect) sliderFrame
						   viewCont: (ViewController *) vc
						   signView: (SignView *) signView];
	[self addSubview: _sliderView];
#endif

	CGRect buttonFrame = sliderFrame;
	buttonFrame.origin.y += usableHeight * barFraction;

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

	[self populateRecordings];

	[self setBackgroundColor: [UIColor whiteColor]];

	[_vc pushTopView: self];
    }

    return self;
}

- (void) populateRecordings {
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
    [_vc popTopView];
}

- (void) skipFwdPressed:(id) junk withData: junk2 {
    NSLog(@"+20");
    // [_signView seek: +20.0 relative: true];
}

- (void) skipBackPressed:(id) junk withData: junk2 {
    NSLog(@"-20");
    // [_signView seek: -20.0 relative: true];
}

- (void) stopPressed:(id) junk withData: junk2 {
    // [_signView stopRadioResumeAtEnd];
    NSLog(@"STOP");
}

- (void) stateChanged: (id) aplayer {
    MFANStreamPlayer *player = (MFANStreamPlayer *) aplayer;
    NSLog(@"in state changed player=%p isPlaying=%d", player, [player isPlaying]);
    if ([player isPlaying])
	[_playButton setTitle:@"Pause"];
    else
	[_playButton setTitle:@"Play"];
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

- (void) songChanged: (id) asong {
#if 0
    NSString *song = (NSString *) asong;
    NSString *stationName = [_signView getPlayingStationName];
    NSString *displayName;

    if (song == nil)
	song = @"[Unknown]";

    if ([stationName length] > 18) {
	displayName = [NSString stringWithFormat: @"%@ - %@",
				[stationName substringToIndex: 18],
				song];
    } else {
	displayName = [NSString stringWithFormat: @"%@ - %@", stationName, song];
    }

    [self updateIOSCenter: displayName];

    // we dont' want to add every song we scroll past to the history
    uint64_t now = osp_time_ms();
    if (now - _sliderView.lastMusicSampleTime > 2000) {
	[_history addHistoryStation: stationName
			   withSong: song];
    }

    _playingSong = song;
    [_marquee setText: song];
#endif
}

- (void) playPressed: (id) sender withData: (NSNumber *)movement {
}

- (void) activateTopView {
    [_marquee restartLabel];
}

- (void) deactivateTopView {
    return;
}

@end
