//
//  MFANPlayerView.m
//  MusicFan
//
//  Created by Michael Kazar on 4/26/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <MediaPlayer/MediaPlayer.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

#import "MFANPlayerView.h"
#import "MFANPlayContext.h"
#import "MFANCoreButton.h"
#import "MFANTopLevel.h"
#import "MFANSetList.h"
#import "MFANPlayerStatus.h"
#import "MFANCGUtil.h"
#import "MFANStarsView.h"
#import "MFANTopSettings.h"
#import "MFANTopHistory.h"
#import "MFANDownload.h"
#import "MFANComm.h"
#import "MFANWarn.h"
#import "MFANViewController.h"
#import "MFANIconButton.h"
#import "MarqueeLabel.h"

#import "MFANAqPlayer.h"

#include <stdlib.h>
#include <sys/stat.h>

#define MFANUseMainPlayer	1	/* MPMusicPlayerController / files + iTunes lib */
#define MFANUseAvPlayer		2	/* downloaded files */
#define MFANUseAudioPlayer	3	/* HTTP streams */

@implementation MFANMediaItem {
    int _usePlayer;

    /* fields for URL are mostly properties in MFANPlayerView.h */

    /* fields for library items */
    MPMediaItem *_item;
}

UIImage *_defaultImage;

- (NSString *) localUrl
{
    if (_url == nil || [_url length] == 0) {
	return @"";
    }
    else
	return _url;
}

/* tell whether we could play this item, if we were using the right
 * player for it; note that MFANPlayerView isPlayable is a little
 * different, since it assumes that you've already chosen a player,
 * which may not be optimal for a given item.  Specifically, an iTunes
 * library only item is generally playable, but isn't if you've
 * already decided you have to use the AV player instead of the MP
 * player.
 */
- (BOOL) isPlayable
{
    if (_usePlayer == MFANUseAvPlayer) {
	/* using the AV player, but media isn't downloaded (or even available) */
	if (_mustDownload && (_url == nil || [_url isEqualToString: @""]))
	    return NO;
    }
    else {
	/* using the MP or AV player, and have no media item */
	if (_item == nil)
	    return NO;
    }
    return YES;
}

/* return playable URL for this item, if there is one */
- (NSString *) effectiveUrl
{
    if (_url == nil || [_url length] == 0) {
	if (_mustDownload)
	    return @"";
	else
	    return _urlRemote;
    }
    else
	return _url;
}

- (BOOL) trackPlaybackTime
{
    /* if not in the library, and not streamed, then we downloaded it,
     * and so it is a podcast or a radio show.  Those are the items
     * for which we track playback times in the item itself.
     */
    return (_item == nil && ![self isWebStream]);
}

- (BOOL) useAvPlayer
{
    return (_usePlayer == MFANUseAvPlayer);
}

/* http stream */
- (BOOL) isWebStream
{
    return ( self.url != nil && [self.url length] > 4 &&
	     [[self.url substringToIndex: 4] isEqualToString: @"http"]);
}

- (BOOL) isPodcast
{
    return (_podcastDate != 0);
}

- (BOOL) useMainPlayer
{
    return (_usePlayer == MFANUseMainPlayer);
}

- (BOOL) useAudioPlayer
{
    return (_usePlayer == MFANUseAudioPlayer);
}

- (NSString *) title
{
    if (_item == nil)
	return _urlTitle;
    else
	return [_item valueForProperty: MPMediaItemPropertyTitle];
}

- (NSString *) albumTitle
{
    if (_item == nil)
	return _urlAlbumTitle;
    else
	return [_item valueForProperty: MPMediaItemPropertyAlbumTitle];
}

- (NSString *) artist
{
    if (_item == nil)
	return nil;
    else
	return [_item valueForProperty: MPMediaItemPropertyArtist];
}

/* is this the interface we want, or do we want to get a UIImage from this? */
- (UIImage *) artworkWithSize: (float) size;
{
    NSData *urlData;
    UIImage *urlImage;
    NSString *artFileUrl;
    if (_urlArtwork != nil && !MFANTopSettings_forceNoArt) {
	artFileUrl = [MFANDownload fileUrlFromMlkUrl: _urlArtwork];
	urlData = [NSData dataWithContentsOfURL: [NSURL URLWithString: artFileUrl]];
	if (urlData != nil) {
	    urlImage = [UIImage imageWithData: urlData];
	    if (urlImage != nil)
		return resizeImage(urlImage, size);
	}
    }

    return resizeImage(_defaultImage, size);
}

- (MPMediaItem *) item
{
    return _item;
}

- (MFANMediaItem *) initWithUrl: (NSString *) url
			  title: (NSString *) title
		     albumTitle: (NSString *) albumTitle
{
    self = [super init];
    if (self != nil) {
	if (_defaultImage == nil) {
	    _defaultImage = [UIImage imageNamed: @"recordfield.jpg"];
	}

	_item = nil;
	self.url = url;
	_urlTitle = title;	/* may be nil */
	_urlAlbumTitle = albumTitle;	/* may be nil */
	_urlArtwork = nil;
	_urlRemote = nil;
	_mustDownload = NO;
	if ([self isWebStream])
	    _usePlayer = MFANUseAudioPlayer;
	else
	    _usePlayer = MFANUseAvPlayer;
    }

    return self;
}

/* set flag for whether songs we can play with either AV or MP player
 * uses MP or AV player; this is initialized with an iTunes library
 * item, so we can't use the AudioPlayer with it.
 */
- (MFANMediaItem *) initWithMediaItem: (MPMediaItem *) item;
{
    NSURL *url;

    self = [super init];
    if (self != nil) {
	_item = item;
	url = [item valueForProperty:MPMediaItemPropertyAssetURL];
	_mustDownload = NO;
	if (url == nil) {
	    /* no URL, so this is a cloud item, which only MPMediaPlayer can play */
	    _usePlayer = MFANUseMainPlayer;
	    self.url = nil;
	}
	else {
	    /* we have a URL */
	    _usePlayer = MFANUseMainPlayer;
	    self.url = [url absoluteString];
	}
    }

    return self;
}
@end

/* The playerView abstraction provides the following core functions:
 * 
 * The setIndex: and setPlaybackTime: functions set the song and time
 * within the song; these must be called while the player is paused,
 * using the pause method.
 *
 * The pause and play functions do the obvious thing: pause the player and resume it.
 *
 * The setQueueWithContext establishes a new queue of music to play, and resetQueue
 * reestablishes the queue for an MP player, in case the an external player (car radio)
 * resets the queue's contents.
 *
 * The update functions updateSliderView, updatePlayerState and updateSongState update various
 * pieces of the displayed state.
 *
 * There are utilities like isPlaying.
 *
 * Above these core functions are higher level functions implemented with these.
 *
 * First we have notification functions, like avIsPlayingChanged, avMusicEnded, mpMusicChanged
 * and mpIsPlayingChanged, which are called when the real player stop or starts, or the current
 * song changes.  These functions call the update functions to do their work.
 *
 * Note that these functions may be called from our own button presses, or if the controls on
 * the systemMusicPlayer are pressed.  In the former, we want our code to handle the updates,
 * so we just watch for recent button presses.
 */
@implementation MFANPlayerView {
    MFANTopLevel *_parent;
    MFANTopHistory *_hist;
    CGRect _playerFrame;
    CGRect _artFrame;
    CGRect _labelFrame;
    CGRect _albumArtistLabelFrame;
    CGRect _cbuttonFrame;
    CGRect _cloudFrame;
    CGRect _starsFrame;
    CGRect _radioFrame1;
    MarqueeLabel *_radioLabel1;
    CGRect _radioFrame2;
    UILabel *_radioLabel2;
    BOOL _artTextVisible;
    AVPlayer *_avPlayer;
    AVPlayerItem *_avItem;
    MPMusicPlayerController *_hackPlayer;
    MPMusicPlayerController *_mpPlayer;
    AVAudioPlayer *_bkgPlayer;		/* hack when using hack player */
    MFANAqPlayer *_audioPlayer;
    BOOL _bkgPlaying; 
    UIButton *_artView;
    UIImage *_cloudImage;
    UIImageView *_cloudView;
    UILabel *_plPositionLabel;
    MarqueeLabel *_songLabel;
    MarqueeLabel *_albumArtistLabel;
    MFANPlayContext *_playContext;
    MFANCoreButton *_playButton;
    MFANCoreButton *_backButton;
    MFANCoreButton *_prevButton;
    MFANCoreButton *_fwdButton;
    MFANCoreButton *_nextButton;
    MFANStarsView *_starsView;
    UISlider *_slider;
    UIImage *_sliderThumbImage;
    UILabel *_timeLabel;
    NSTimer *_sliderUpdateTimer;	/* for periodic updates */
    NSTimer *_sliderDragTimer;		/* for operating once drag is done */
    float _sliderDragValue;
    NSTimer *_hijackTimer;
    MFANMediaItem *_lastDisplayedSong;
    MFANPlayerStatus *_playerStatus;
    CGRect _rightMarginFrame;
    CGRect _leftMarginFrame;
    MFANComm *_comm;
    NSArray *_avMediaArray;	/* of MFANMediaItem objects; always set */
    NSArray *_mpMediaArray;	/* of MPMediaItem obs; only valid !_useAvPlayer */
    long _avMediaIndex;	/* index into above of current song */
    MFANMediaItem *_avMediaItem;
    MPMediaItem *_otherMediaItem;	/* 2nd item */
    BOOL _isPlaying;
    int _usePlayer;
    BOOL _reboundToMainPlayer;	/* if only temporarily using AV player */
    BOOL _hijackInProgress;
    BOOL _forceUpdate;
    BOOL _durationValid;	/* in nowPlayingInfo */
    BOOL _forceSetQueue;	/* force setQueueWithItemCollection */ 
    NSData *_silentData;
    NSMutableDictionary *_nowPlayingInfo;
    uint32_t _lastChangeMs;	/* last time song changed */
    uint32_t _lastStateChangeMs; /* last time isPlaying changed */
    MFANViewController *_viewCon;
    uint32_t _lastHijackSecs;	/* seconds since last hijack */
    uint32_t _lastHijackCount;	/* count since last time we had a 20 second gap in hijackings */

    /* we count how many times in a row we thought we were playing,
     * but currentTime didn't change (for AV player).  This is often
     * how a bad URL, a bad network, or the wrong network, manifests
     * itself.
     */
    float _lastTime; 			/* last value of currentTime for avplayer */
    uint32_t _lastTimeUnchanged;	/* count of # of times unchanged */
}

static const float _hijackDelay = 4.0;

- (AVPlayer *) playerController
{
    return _avPlayer;
}

/* if using the MP media player, is there a duplicate MFANMediaItem preceding this
 * one?  If there is, if we try to switch to index ix, we'll end up switch to the
 * earlier one instead.  So, we'll have to use some other way of playing
 * this item.
 *
 * That means our caller will try to use the AVPlayer if it is
 * playable by the AVPlayer, i.e. not streamed from the iTunes
 * library.
 */
- (BOOL) canSwitchToIndex: (long) ix
{
    MFANMediaItem *currentMFANItem;
    MFANMediaItem *mfanItem;
    MPMediaItem *currentItem;
    long count;
    long i;

    count = [_avMediaArray count];
    if (ix >= count)
	return 0;
    currentMFANItem = [_avMediaArray objectAtIndex: ix];
    currentItem= currentMFANItem.item;
    for(i=0;i<ix;i++) {
	mfanItem = [_avMediaArray objectAtIndex: i];
	if (currentItem == mfanItem.item)
	    return 0;
    }
    return 1;
}

- (CGRect) rightMarginFrame
{
    return _rightMarginFrame;
}

- (void) setHistory: (MFANTopHistory *) hist
{
    _hist = hist;
}

- (void) setupAudioSession: (BOOL) mix
{
    NSError *setError;

    setError = nil;

    if (mix) {
	[[AVAudioSession sharedInstance]
	    setCategory: AVAudioSessionCategoryPlayback
	    withOptions: AVAudioSessionCategoryOptionMixWithOthers
	    error: &setError];
    }
    else {
	[[AVAudioSession sharedInstance]
	    setCategory: AVAudioSessionCategoryPlayback
	    withOptions: 0
	    error: &setError];
    }

    [[NSNotificationCenter defaultCenter] addObserver: self
					  selector: @selector(audioInterruption:)
					  name: AVAudioSessionInterruptionNotification
					  object: nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
					  selector:@selector(audioRouteChanged:)
					  name:AVAudioSessionRouteChangeNotification
					  object:nil];
}

- (void) audioRouteChanged: (NSNotification *) notification
{
    NSDictionary *userInfo = [notification userInfo];
    NSNumber *reasonKey;
    long reason;

    if (_audioPlayer != nil) {
	reasonKey = (NSNumber *) userInfo[AVAudioSessionRouteChangeReasonKey];
	reason = [reasonKey longValue];
	if ( reason == AVAudioSessionRouteChangeReasonOldDeviceUnavailable) {
	    [_audioPlayer pause];
	}
    }
}

- (void) audioInterruption: (NSNotification *) notification
{
    NSDictionary *userInfo = [notification userInfo];
    NSNumber *intKey;
    NSNumber *optKey;
    long intType;

    intKey = (NSNumber *) userInfo[AVAudioSessionInterruptionTypeKey];
    optKey = (NSNumber *) userInfo[AVAudioSessionInterruptionOptionKey];

    intType = [intKey longValue];
    if (intType == AVAudioSessionInterruptionTypeEnded) {
	NSLog(@"- audio interruption ended");
	if ([optKey longValue] & AVAudioSessionInterruptionOptionShouldResume) {
	    NSLog(@"- resuming audio player");
	    if (_audioPlayer != nil) {
		if (_isPlaying) {
		    [self play];
		    [self updatePlayerState];
		}
	    }
	}
    }
    else if (intType == AVAudioSessionInterruptionTypeBegan) {
	NSLog(@"- audio interruption began");
	if (_audioPlayer != nil) {
	    if (_isPlaying) {
		[self pause];
		[self updatePlayerState];
	    }
	}
    }
    else {
	NSLog(@"! audio interruption unknown type %ld", intType);
    }
}

/* note that this function adds a number of subviews to the parent, not to itself, and
 * that parent typically has the whole screen to use.
 */
- (MFANPlayerView *) initWithParent: (MFANTopLevel *) parent
			  topMargin: (CGFloat) topMargin
			       comm: (MFANComm *) comm
			    viewCon: (MFANViewController *) viewCon
			   andFrame: (CGRect) appFrame
{
    MFANCoreButton *cbutton;
    CGRect frame;
    CGRect timeLabelFrame;
    CGFloat labelHeight;
    CGFloat heightBelowArt;
    CGFloat buttonWidth;	/* should be 2.4X height */
    CGFloat buttonGapRatio = 0.2;
    CGFloat nlines = 5.0;
    CGFloat buttonGap;
    CGRect plPositionFrame;
    CGRect sliderFrame;
    CGContextRef cx;
    CGFloat playButtonYOrigin;
    CGFloat playButtonHeight;
    CGFloat artFrameFraction;
    NSError *setError;

    /* these three must total to 3.0 */
    // CGFloat buttonWeight = 1.3;
    CGFloat sliderWeight = 0.7;
    CGFloat playWeight = 1.3;

    MPMusicPlayerController *tempPlayer;

    self = [super initWithFrame:appFrame];
    if (self) {
        // Initialization code
	_parent = parent;
	_viewCon = viewCon;
	_playerFrame = appFrame;
	_starsView = nil;
	_comm = comm;
	_avMediaItem = nil;
	_avMediaIndex = 0;
	_forceSetQueue = YES;
	_artTextVisible = NO;
	_forceUpdate = 1;
	_isPlaying = NO;
	_usePlayer = MFANUseMainPlayer;
	_reboundToMainPlayer = NO;
	_hijackInProgress = NO;
	_lastHijackSecs = 0;
	_lastHijackCount = 0;

	_mpPlayer = [MPMusicPlayerController systemMusicPlayer];

	[self setBackgroundColor: [UIColor clearColor]];

	/* the more square the screen, the smaller we want to make the art work */
	artFrameFraction = 0.98 * appFrame.size.width / appFrame.size.height;
	if (artFrameFraction > 0.62) {
	    /* shrink height / width so that we don't use more than percentage
	     * of appFrame's height.
	     */
	    _artFrame.size.width = appFrame.size.height * 0.62;
	}
	else {
	    _artFrame.size.width = 0.98 * appFrame.size.width;
	}
	_artFrame.size.height = _artFrame.size.width;

	_artFrame.origin.x = appFrame.origin.x + (appFrame.size.width - _artFrame.size.width)/2;
	_artFrame.origin.y = appFrame.origin.y + topMargin;

	_starsFrame = _artFrame;
	_starsFrame.origin.y += _artFrame.size.height/3;
	_starsFrame.size.height = _artFrame.size.height/3;

	/* we divide the remaining space under the artwork into equal
	 * pieces for the song, the album/artist, the controls, and
	 * the song#/time, with another 50% of that for the slider
	 * bar, for a total of nlines=5 lines.  We also want a
	 * buttonGap in between every line, and at the top and bottom,
	 * too.  We also weight the play button line by playWeight
	 *
	 * The math is (nlines-2) * labelHeight +
	 *  sliderWeight*labelHeight +
	 *  playWeight * labelHeight +
	 *  (nlines+1)*labelHeight*buttonGapRatio
	 *
	 * equals the vertical height.  So, factoring out labelHeight, gives us
	 * (nlines-2+playWeight+sliderWeight+(nlines+1)*buttonGapRatio) as the multiplier.
	 */
	heightBelowArt = (appFrame.size.height + appFrame.origin.y -
			  (_artFrame.origin.y+_artFrame.size.height));
	labelHeight = (heightBelowArt /
		       ((nlines-2) + playWeight + sliderWeight
			+ (nlines+1.0)*buttonGapRatio));
	/* don't let the font get too big, or too few characters fit */
	if (labelHeight > appFrame.size.height/20)
	    labelHeight = appFrame.size.height/20;
	buttonGap = labelHeight * buttonGapRatio;

	frame.origin.x = appFrame.origin.x;
	frame.origin.y = _artFrame.origin.y + _artFrame.size.height + buttonGap;
	frame.size.height = labelHeight;
	frame.size.width = appFrame.size.width;
	_labelFrame = frame;
	_songLabel = [[MarqueeLabel alloc] initWithFrame: frame];
	[_songLabel setTextColor: [MFANTopSettings textColor]];
	[_songLabel setFont: [MFANTopSettings basicFontWithSize: labelHeight * 0.9]];
	[_songLabel setTextAlignment: NSTextAlignmentCenter];
	[_parent addSubview: _songLabel];

	frame.origin.x = _labelFrame.origin.x;
	frame.origin.y = _labelFrame.origin.y + labelHeight + buttonGap;
	frame.size.height = labelHeight;
	frame.size.width = appFrame.size.width;
	_albumArtistLabelFrame = frame;
	_albumArtistLabel = [[MarqueeLabel alloc] initWithFrame: frame];
	[_albumArtistLabel setTextColor: [MFANTopSettings textColor]];
	[_albumArtistLabel setFont: [MFANTopSettings basicFontWithSize: labelHeight * 0.9]];
	[_albumArtistLabel setTextAlignment: NSTextAlignmentCenter];
	[_parent addSubview: _albumArtistLabel];

	/* In this remaining space, we need to layout the control
	 * buttons (prev, play/pause, etc), then the slider, and then
	 * the record/x of y/time line.  Each has a weight that totals
	 * 3 that is used to scale the height.
	 */
	// use maxButtonHeight somehow?? */
	buttonWidth = 2.5*labelHeight;

	playButtonYOrigin = _albumArtistLabelFrame.origin.y + labelHeight + buttonGap;
	playButtonHeight = labelHeight * playWeight;

	frame.origin.x = _albumArtistLabelFrame.origin.x;
	frame.origin.y = playButtonYOrigin;
	frame.size.width = buttonWidth;
	frame.size.height = playButtonHeight;
	_cbuttonFrame = frame;
	cbutton = [[MFANCoreButton alloc] initWithFrame: frame
					  title:@"Prev"
					  color: [MFANTopSettings baseColor]];
	[_parent addSubview: cbutton];
	[cbutton addCallback: self
		 withAction: @selector(prevPressed:withData:)];
	

	/* we want to center this button midway between a button in
	 * the middle and a button at the start.  The segment is half
	 * the width, minus a button on the left and a half button on
	 * the right.  We want to center a button so we start our
	 * button half a button more to the left, for a total of
	 * 2*buttonWidth.  Don't forget to skip the first button.
	 */
	frame.origin.x = ( _albumArtistLabelFrame.origin.x +
			   (_albumArtistLabelFrame.size.width/2 - 3*buttonWidth/2) / 2 +
			   buttonWidth/2);
	frame.origin.y = playButtonYOrigin;
	frame.size.width = buttonWidth;
	frame.size.height = playButtonHeight;
	cbutton = [[MFANCoreButton alloc] initWithFrame: frame
					  title:@"Blank"
					  color: [MFANTopSettings baseColor]];
	[cbutton setClearText: @"-20"];
	_backButton = cbutton;
	[_parent addSubview: cbutton];
	[cbutton addCallback: self
		 withAction: @selector(backPressed:withData:)];
	
	frame.origin.x = (_albumArtistLabelFrame.origin.x +
			  (_albumArtistLabelFrame.size.width - buttonWidth)/2);
	frame.origin.y = playButtonYOrigin;
	frame.size.width = buttonWidth;
	frame.size.height = playButtonHeight;
	cbutton = [[MFANCoreButton alloc] initWithFrame: frame
					  title:@"Play"
					  color: [MFANTopSettings baseColor]];
	_playButton = cbutton;
	[_parent addSubview: cbutton];
	[cbutton addCallback: self
		 withAction: @selector(playPressed:withData:)];
	
	/* we want to center this button midway between the button in
	 * the middle and a button at the end.  The segment is half
	 * the width, minus a button on the right and a half button on
	 * the left.  We want to center a button so we start our
	 * button half a button more to the left.  Note the buttonWidth/2
	 * we skip extra to skip over half the play button cancels out the
	 * buttonWidth/2 we move to the left to center our button.
	 */
	frame.origin.x = ( _albumArtistLabelFrame.origin.x +
			   _albumArtistLabelFrame.size.width / 2 +
			   (_albumArtistLabelFrame.size.width/2 - 3*buttonWidth/2) / 2);
			   
	frame.origin.y = playButtonYOrigin;
	frame.size.width = buttonWidth;
	frame.size.height = playButtonHeight;
	cbutton = [[MFANCoreButton alloc] initWithFrame: frame
					  title:@"Blank"
					  color: [MFANTopSettings baseColor]];
	[cbutton setClearText: @"+20"];
	_fwdButton = cbutton;
	[_parent addSubview: cbutton];
	[cbutton addCallback: self
		 withAction: @selector(fwdPressed:withData:)];
	
	frame.origin.x = (_albumArtistLabelFrame.origin.x +
			  _albumArtistLabelFrame.size.width - buttonWidth);
	frame.origin.y = playButtonYOrigin;
	frame.size.width = buttonWidth;
	frame.size.height = playButtonHeight;
	cbutton = [[MFANCoreButton alloc] initWithFrame: frame
					  title: @"Next"
					  color: [MFANTopSettings baseColor]];
	[_parent addSubview: cbutton];
	[cbutton addCallback: self
		  withAction: @selector(nextPressed:withData:)];


	sliderFrame.origin.x = appFrame.origin.x;
	sliderFrame.origin.y = (_cbuttonFrame.origin.y + _cbuttonFrame.size.height + buttonGap);
	sliderFrame.size.width = appFrame.size.width;
	sliderFrame.size.height = labelHeight * sliderWeight;

	CGRect sliderThumbRect;
	sliderThumbRect.origin.x = 0;
	sliderThumbRect.origin.y = 0;
	sliderThumbRect.size.width = 4;
	sliderThumbRect.size.height = sliderFrame.size.height*0.8;
	UIGraphicsBeginImageContext(sliderThumbRect.size);
	cx = UIGraphicsGetCurrentContext();
	CGContextSetFillColorWithColor(cx, [MFANTopSettings textColor].CGColor);
	CGContextFillRect(cx, sliderThumbRect);
	_sliderThumbImage = UIGraphicsGetImageFromCurrentImageContext();
	UIGraphicsEndImageContext();

	_slider = [[UISlider alloc] initWithFrame: sliderFrame];
	[_slider setContinuous: YES];
	/* is StateNormal enough? */
	[_slider setMinimumTrackTintColor: [MFANTopSettings textColor]];
	[_slider setMaximumTrackTintColor: [MFANTopSettings lightBaseColor]];

	[_slider setThumbImage: _sliderThumbImage forState: UIControlStateNormal];
	[_slider addTarget: self
		 action: @selector(sliderDragged)
		 forControlEvents:UIControlEventTouchDragInside];
	[_parent addSubview: _slider];

	timeLabelFrame.size.width = 100;
	timeLabelFrame.size.height = labelHeight;
	timeLabelFrame.origin.x = ( sliderFrame.origin.x +
				    sliderFrame.size.width -
				    timeLabelFrame.size.width);
	timeLabelFrame.origin.y = sliderFrame.origin.y + sliderFrame.size.height + buttonGap;
	_timeLabel = [[UILabel alloc] initWithFrame: timeLabelFrame];
	[_timeLabel setTextColor: [MFANTopSettings textColor]];
	[_timeLabel setFont: [MFANTopSettings basicFontWithSize: labelHeight * 0.9]];
	[_timeLabel setTextAlignment: NSTextAlignmentRight];
	[_timeLabel setAdjustsFontSizeToFitWidth: YES];
	[_timeLabel setText: @""];
	[_parent addSubview: _timeLabel];

	plPositionFrame.origin.y = timeLabelFrame.origin.y;
	plPositionFrame.size.width = 200;
	plPositionFrame.size.height = timeLabelFrame.size.height;
	plPositionFrame.origin.x = (sliderFrame.origin.x +
				    (sliderFrame.size.width - plPositionFrame.size.width)/2);
	_plPositionLabel = [[UILabel alloc] initWithFrame: plPositionFrame];
	[_plPositionLabel setTextColor: [MFANTopSettings textColor]];
	[_plPositionLabel setTextAlignment: NSTextAlignmentCenter];
	[_plPositionLabel setFont: [MFANTopSettings basicFontWithSize: labelHeight * 0.9]];
	[_parent addSubview: _plPositionLabel];

	_sliderUpdateTimer = nil;
	_lastDisplayedSong = nil;

	[self setupAudioSession: YES];

	_playerStatus = [[MFANPlayerStatus alloc] initObject: self
						  sel: @selector(mpIsPlayingChanged:)];
	tempPlayer = [MPMusicPlayerController systemMusicPlayer];
	[[NSNotificationCenter defaultCenter]
	    addObserver: self
	    selector: @selector(mpMusicChanged:)
	    name: MPMusicPlayerControllerNowPlayingItemDidChangeNotification
	    object: tempPlayer];
	[tempPlayer beginGeneratingPlaybackNotifications];
	NSLog(@"- tempPlayer=%p", tempPlayer);
	[tempPlayer setRepeatMode: MPMusicRepeatModeNone];
	[tempPlayer setShuffleMode: MPMusicShuffleModeOff];	// amylfax shuffle time

	_silentData = silentData(10);
	_bkgPlayer = [[AVAudioPlayer alloc] initWithData: _silentData error:&setError];
	_bkgPlayer.volume = 0.0;
	if ([setError code] != 0)
	    NSLog(@"! Bkgplayer init failed %d", (int) [setError code]);
	[_bkgPlayer setNumberOfLoops: -1];
	_bkgPlaying = NO;

	/* last time the isPlaying state changed */
	_lastStateChangeMs = osp_get_ms();
    }
    return self;
}

- (BOOL) isPlayingWithForce: (BOOL) forced
{
    if (_hackPlayer && forced) {
	/* start an asynchronous check of whether it is really playing */
	[_playerStatus stateWithForce: forced];
    }
    return _isPlaying;
}

/* Is this item not playable, either because it is an unloaded podcast, or because
 * it is a mediaItem without an MPMediaItem and we are using the MP player, or because
 * it has no URL and we're using the AV player.
 */
- (BOOL) isPlayable: (MFANMediaItem *) mfanItem
{
    MPMediaItem *item;

    item = [mfanItem item];

    if (_usePlayer == MFANUseAudioPlayer) {
	return YES;
    }
    else if (_usePlayer == MFANUseAvPlayer) {
	/* using the AV player, but media isn't downloaded (or even available) */
	if (mfanItem.mustDownload && [mfanItem.localUrl isEqualToString: @""])
	    return NO;
    }
    else {
	/* using the MP player, and have no media item */
	if (item == nil)
	    return NO;
    }
    return YES;
}

#undef MFAN_TESTHIJACK
// #define MFAN_TESTHIJACK 1
#ifdef MFAN_TESTHIJACK
- (void) testHijackTimer: (id) junk
{
    MFANMediaItem *mfanItem;
    [_hackPlayer pause];
    mfanItem = [_avMediaArray objectAtIndex: 0];
    _hackPlayer.nowPlayingItem = [mfanItem item];
    [_hackPlayer play];
}
#endif

/* set the current playing item to ix; and possibly start playing it */
- (void) setIndex: (long) ix rollForward: (BOOL) forward
{
    NSString *mediaItemUrl;
    AVPlayerItem *avItem;
    BOOL reportChange;
    long avCount;
    long i;
    long loops;
    MPMediaItem *mpMediaItem;
    MPMediaItem *tempMediaItem;
    MFANMediaItem *mfanItem;
    float newPlaybackTime;
    uint32_t waitStartMs;
    MPMediaItemCollection *mediaCollection;

#ifdef MFAN_TESTHIJACK
    /* this will trigger a fake hijack 15 seconds into song 165 */
    if (_avMediaIndex != 164 && ix == 164) {
	[NSTimer scheduledTimerWithTimeInterval: 15.0
		 target:self
		 selector:@selector(testHijackTimer:)
		 userInfo:nil
		 repeats: NO];
    }
#endif

    newPlaybackTime = 0.0;
    _lastTime = 0.0;
    _lastTimeUnchanged = 0;
    _lastChangeMs = osp_get_ms();

    /* if we have an item we can't play because it isn't loaded yet,
     * see if we can find something that is loaded.  Also, if we're
     * using the MP player and we find something without an item,
     * skip it.
     */
    avCount = [_avMediaArray count];
    if (ix >= avCount) {
	return;
    }

    /* find the next media item that's playable */
    if (forward) {
	for(i=0; i<avCount; i++) {
	    _avMediaItem = [_avMediaArray objectAtIndex: ix];
	    if ( [self isPlayable: _avMediaItem])
		break;

	    /* try the next item */
	    if (++ix >=  avCount)
		ix = 0;
	    NSLog(@"- skipping unplayable/unloaded item to index %ld", ix);
	}
    }
    else {
	for(i=0; i<avCount; i++) {
	    _avMediaItem = [_avMediaArray objectAtIndex: ix];
	    if ( [self isPlayable: _avMediaItem])
		break;

	    /* try the previous item */
	    if (--ix < 0)
		ix = avCount-1;
	    NSLog(@"- skipping unplayable/unloaded item to index %ld", ix);
	}
    }

    /* figure out what to play; if we have a media item for it, save it in
     * mpMediaItem, even if we're not going to play it with MPMediaPlayer
     */
    reportChange = _forceUpdate || (ix != _avMediaIndex);
    _avMediaIndex = ix;
    if (_avMediaIndex >= avCount)
	return;
    if (_avMediaItem == nil)
	return;

    if (_usePlayer == MFANUseMainPlayer) {
	/* no URL */
	mpMediaItem = [_avMediaItem item];
	mediaItemUrl = nil;
    }
    else {
	/* use a URL */
	mediaItemUrl = [_avMediaItem effectiveUrl];
	mpMediaItem = nil;	/* might have one, but we're not using it */
    }

    /* stop whatever's playing now */
    if (_avPlayer) {
	/* remove old observers for previous song */
	[_avPlayer removeObserver: self
		   forKeyPath: @"rate"];
	[_avItem removeObserver: self
		 forKeyPath: @"status"];
	[[NSNotificationCenter defaultCenter]
	    removeObserver: self
	    name: AVPlayerItemDidPlayToEndTimeNotification
	    object: _avItem];

	[_avPlayer pause];
	_avPlayer = nil;
	_avItem = nil;
    }

    if (_audioPlayer) {
	[_audioPlayer stop];
	_audioPlayer = nil;
    }

    /* always stop the music player, since the car starts it at random */
    if (_usePlayer == MFANUseMainPlayer) {
	[_mpPlayer pause];	/* stop and pause do different things */
    }
    else {
	[_mpPlayer stop];
	_forceSetQueue = YES;	/* next time main player runs, it'll need to reset queue */
    }
    _hackPlayer = nil;

    /* mark that we're not playing anything right now */
    _isPlaying = NO;

    if ( _usePlayer == MFANUseMainPlayer) {
	NSLog(@"- setIndex: using mpmediaplayer");
	[_playerStatus enable];

	[self setupAudioSession: YES];

	/* ugh, have to play it with MPMediaPlayer, and MPMediaPlayer
	 * doesn't keep us running, so when it's done, we don't get to
	 * run again.  So, we run the background player to add in some
	 * continually playing silence to keep us running.
	 */
	_hackPlayer = _mpPlayer;

	/* now, if there are multiple entries for the same song, we'll continue
	 * at the first one, not the current one.  You can't just set the index (boo, Apple),
	 * so we try to use the AV player instead.
	 *
	 * If the song requires the MP player (no associated URL), we just skip it.
	 */
	if (![self canSwitchToIndex: ix]) {
	    NSLog(@"- setIndex: switching back to ix=%d since item is a duplicate with ix=%d",
		  (int) ix-1, (int) [_hackPlayer indexOfNowPlayingItem]);
	    for(loops=avCount; loops >= 0; loops--) {
		mfanItem = [_avMediaArray objectAtIndex: ix];
		if (mfanItem.useAvPlayer || mfanItem.url != nil) {
		    /* we can play this with the AV media player */
		    _usePlayer = MFANUseAvPlayer;
		    _reboundToMainPlayer = YES;
		    NSLog(@"- setIndex: recursing to play ix=%ld with avplayer", ix);
		    [self setIndex: ix rollForward: forward];
		    return;
		}

		/* here, this item requires the MP media player, but
		 * if we play it with that player (because
		 * canSwitchToIndex returned false), we'll switch to a
		 * random earlier spot in the playlist, so we skip it
		 * instead.  The next one might be usable with the MP
		 * player.
		 */
		if (++ix >= avCount)
		    ix = 0;

		/* if this is safely playable with the MP player, we're done */
		if ([self canSwitchToIndex: ix]) {
		    NSLog(@"- setIndex: recursing to play new ix=%ld with MPplayer", ix);
		    [self setIndex: ix rollForward: forward];
		    return;
		}
	    }
	    NSLog(@"- setIndex broke after full loop!");
	}

	/* re-set the queue if [MPMusicPlayerController stop] may have damaged the queue;
	 * this is due to a bug in ios 10.3 where 'stop' also resets the queue to all
	 * music in your library.
	 */
	if (_forceSetQueue) {
	    [_mpPlayer stop];

	    /* now set the real queue */
	    NSLog(@"- Forcequeue with %d items", (int) [_mpMediaArray count]);
	    mediaCollection = [MPMediaItemCollection collectionWithItems: _mpMediaArray];
	    [_mpPlayer setQueueWithItemCollection: mediaCollection];
	    _forceSetQueue = NO;
	}
	else {
	    NSLog(@"- forcequeue not required");
	}

	NSLog(@"- hackPlayer assigning items");
	_mpPlayer.nowPlayingItem = mpMediaItem;

	NSLog(@"- hackPlayer assigned items done");

	/* this is ridiculous -- we need to sleep long enough for the 
	 * real MP Player to receive and process the assignment to nowPlayingItem.
	 *
	 * Fortunately, it appears we can wait for it to echo back, instead of guessing
	 * how long it takes to percolate to the MP music player task.
	 */
	waitStartMs = osp_get_ms();
	while(1) {
	    tempMediaItem = _mpPlayer.nowPlayingItem;
	    if (tempMediaItem == mpMediaItem)
		break;

	    NSLog(@"- hackplayer failed to set item %p (%@) != %p",
		  tempMediaItem, [tempMediaItem valueForProperty: @"title"], mpMediaItem);

	    [NSThread sleepForTimeInterval: 0.2];
	    _mpPlayer.nowPlayingItem = mpMediaItem;

	    /* put in a timeout on this spinning */
	    if (osp_get_ms() > waitStartMs + 1200) {
		NSLog(@"* hackPlayer breaks out of loop");
		break;
	    }
	}

	_isPlaying = YES;

	/* not playing until we call play */
	[[UIApplication sharedApplication] endReceivingRemoteControlEvents];
    }
    else if (_usePlayer == MFANUseAudioPlayer) {
	NSLog(@"- setIndex: using audio stream player");
	[_playerStatus disable];
	[self setupAudioSession: NO];

	_audioPlayer = [[MFANAqPlayer alloc] init];
	[_audioPlayer setStateCallback: self sel: @selector(audioIsPlayingChanged:)];
	[_audioPlayer setSongCallback: self sel: @selector(audioNowPlayingChanged:)];
	[_audioPlayer play: mediaItemUrl];

	/* we're playing now */
	_isPlaying = YES;

	/* This has to get called each time we create a new player, or the lock screen
	 * doesn't appear.
	 */
	[[UIApplication sharedApplication] beginReceivingRemoteControlEvents];
    }
    else {
	NSLog(@"- setIndex: using avplayer");
	[_playerStatus disable];
	if ([_avMediaItem.effectiveUrl length] == 0) {
	    MFANWarn *warn = [[MFANWarn alloc] initWithTitle:@"No downloaded podcasts"
					       message:@"File not (fully) downloaded"
					       secs: 0.5];
	    warn = nil;
	    return;
	}

	[self setupAudioSession: NO];

	/* now translate our own special URLs -- we use a special one for library-relative
	 * paths, so we don't get messed up if our library directory changes (which happens
	 * when we run under Xcode, at least.
	 */
	if ([[mediaItemUrl substringToIndex: 6] isEqualToString: @"mlk://"]) {
	    mediaItemUrl = [MFANDownload fileUrlFromMlkUrl: mediaItemUrl];
	}
	else if ([[mediaItemUrl substringToIndex: 6] isEqualToString: @"mld://"]) {
	    mediaItemUrl = [MFANDownload docUrlFromMlkUrl: mediaItemUrl];
	}

	_avItem = avItem = [AVPlayerItem playerItemWithURL: [NSURL URLWithString: mediaItemUrl]];
	_avPlayer = [AVPlayer playerWithPlayerItem: avItem];

	if ( [_avMediaItem trackPlaybackTime]) {
	    newPlaybackTime = _avMediaItem.playbackTime;
	    [self setPlaybackTime: newPlaybackTime];
	}

	[[NSNotificationCenter defaultCenter]
	    addObserver: self
	    selector: @selector(avMusicEnded:)
	    name: AVPlayerItemDidPlayToEndTimeNotification
	    object: avItem];

	[_avPlayer addObserver: self
		   forKeyPath: @"rate"
		   options: 0
		   context: NULL];
	[_avItem addObserver: self
		 forKeyPath:@"status"
		 options: 0
		 context: NULL];

	/* This has to get called each time we create a new player, or the lock screen
	 * doesn't appear.
	 */
	[[UIApplication sharedApplication] beginReceivingRemoteControlEvents];
    }

    if (reportChange) {
	[self nowPlayingChanged: nil];
    }

    [self updatePlayerState];
}

/* This crap seems to be necessary when transitioning from using the
 * MPPlayer to the AVPlayer, to get a good transition to the *next*
 * song, and to get the current state displayed correctly on the lock
 * screen (and car display).  We keep the background going even after
 * switching to the AV player, because otherwise iOS will stop our app
 * if we're in the background.  Quel merde.
 *
 * Then, after a fraction of a second of the AV player running, we
 * stop the background player, and reset the AV player's mode.  And
 * for some mysterious reason, iOS won't display the player controls
 * until the next time we start playing, so we pause and continue the
 * AV player to do this.
 */
- (void) bounceTimerFired: (id) junk
{
    [_bkgPlayer stop];
    _bkgPlaying = NO;
    [self setupAudioSession: NO];

    [[UIApplication sharedApplication] beginReceivingRemoteControlEvents];

    if (_avPlayer != nil) {
	[_avPlayer pause];
	[_avPlayer play];
    }
}


- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary *)change
                       context:(void *)context {
    NSLog(@"- observevalueforkeypath (avplayer) stop/start triggered");
    [self avIsPlayingChanged];
}

- (void) audioNowPlayingChanged: (id) junk
{
    [self nowPlayingChanged: nil];

    /* this crap has to happen to get the lock screen to update after changing the
     * nowPlayingCenter (done by nowPlayingChanged).
     */
    [self setupAudioSession: NO];
}

- (void) audioIsPlayingChanged: (id) junk
{
    /* get delayed notifications sometimes, when player stops long after we've switched away
     * from it.
     */
    if (_audioPlayer == nil)
	return;

    _isPlaying = [_audioPlayer isPlaying];
    [self updatePlayerState];

    if (!_isPlaying && !_hijackInProgress) {
	NSLog(@"- Saving due to av pause");
	[_playContext saveListToFile];

	/* if we're playing an AV player item when plugged into the
	 * car, the car's audio system stops us; here's our chance to
	 * start the AV player up again.
	 */
	[self fixIfHijacked: NO];
    }

    if ([_audioPlayer stopped] && [_audioPlayer shouldRestart]) {
	/* get rid of this one */
	NSLog(@"- ostensibly freeing AqPlayer because AqPlayer stopped and restarting");
	_audioPlayer = nil;

	/* and start it up again */
	[self setIndex: _avMediaIndex rollForward: YES];
    }
}

- (void) avIsPlayingChanged
{
    NSLog(@"- avisplayingChanged rate=%f", _avPlayer.rate);
    if (_avPlayer.rate < 0.01) {
	/* stopped */
	_isPlaying = NO;
    }
    else
	_isPlaying = YES;

    [self updatePlayerState];

    if (!_isPlaying && !_hijackInProgress) {
	NSLog(@"- Saving due to av pause");
	[_playContext saveListToFile];

	/* if we're playing an AV player item when plugged into the
	 * car, the car's audio system stops us; here's our chance to
	 * start the AV player up again.
	 */
	[self fixIfHijacked: NO];
    }
}

/* called when the AVPlayer's music ends */
- (void) avMusicEnded: (id) junk
{
    long ix;
    MFANMediaItem *lastItem;

    if (_avMediaArray == nil)
	return;

    NSLog(@"- avMusicEnded called, advancing to next song");
    [self pause];

    if (_reboundToMainPlayer) {
	NSLog(@"- avMusicEnded switching back to main player");
	_reboundToMainPlayer = NO;
	_usePlayer = MFANUseMainPlayer;
    }

    lastItem = [self currentMFANItem];

    ix = _avMediaIndex+1;
    if (ix >= _avMediaArray.count)
	ix = 0;

    [self setIndex: ix rollForward: YES];
    [self nowPlayingChanged: nil];

    /* resume new player */
    [self play];

    /* if we're supposed to unload the last played item, and it is a downloaded
     * AV media file, unload that item.
     */
    if (lastItem != nil) {
	lastItem.playbackTime = 0.0;
	if ( [MFANTopSettings unloadPlayed] && (![_avMediaItem isWebStream])) {
	    [[_playContext download] unloadItem: lastItem];
	}
    }
    NSLog(@"- avMusicEnded done");
}

/* called when music stops or starts when using hackPlayer (MPMusicPlayer) */
- (void) mpMusicChanged: (id) junk
{
    MPMusicPlaybackState playbackState;
    uint32_t now;
    long newIndex;
    uint32_t msSinceChanged;

    /* record our currently best known state */
    playbackState = [_playerStatus state];
    if (playbackState == MPMusicPlaybackStatePlaying)
	_isPlaying = YES;
    else
	_isPlaying = NO;

    NSLog(@"- mpMusicChanged state=%d ct=%d avIx=%d",
	  (int) playbackState,
	  (int) [_avMediaArray count],
	  (int) _avMediaIndex);

    if (_avMediaArray == nil || _hijackInProgress)
	return;

    now = osp_get_ms();

    msSinceChanged = now - _lastChangeMs;
    NSLog(@"- mpMusicChanged %d ms after press", msSinceChanged);
    if (msSinceChanged < 1500) {
	/* music changed because we changed it recently, so just
	 * return, since state would have been updated by the guy who
	 * change the music (setIndex).
	 */
	NSLog(@"- mpMusicChanged, but right after we changed the music");
	[self nowPlayingChanged: nil];
	return;
    }

    if (_hackPlayer == nil) {
	NSLog(@"- no MP player on mpMusicChanged event");
	return;
    }

    NSLog(@"- mpMusicChanged old ix=%ld", _avMediaIndex);
    newIndex = [_hackPlayer indexOfNowPlayingItem];
    NSLog(@"- mpMusicChanged new Ix=%ld", _avMediaIndex);

    /* check to see if the car radio has taken control and changed the playing music; if
     * so, just put back our music.  Must do this before updating _avMediaIndex, so that
     * we don't record the state of a hijack.  Don't do the test too soon after we've
     * changed the actual playing item, since iOS has all sorts of random delays
     * before it reports states accurately.
     */
    if (msSinceChanged > 3000 && [self fixIfHijacked: NO]) {
	return;
    }

    if (newIndex >= 0 && newIndex < [_avMediaArray count]) {
	_avMediaIndex = newIndex;
    }
    else {
	NSLog(@"- avMediaIndex out of bounds %d/%d",
	      (int) newIndex, (int) [_avMediaArray count]);
    }
    _avMediaItem = _avMediaArray[ _avMediaIndex];

    [self nowPlayingChanged: nil];

    [self play];

    NSLog(@"- mpMusicChanged done");
}

/* callback invoked when MPMusicPlayer changes state */
- (void) mpIsPlayingChanged: (id) notification
{
    BOOL isPlaying;
    if ([_playerStatus stateWithForce: NO] == MPMusicPlaybackStatePlaying) {
	isPlaying = YES;
    }
    else {
	isPlaying = NO;
    }

    /* record the current state of playing, if the MP Music player is the actual current player */
    if (_hackPlayer != nil) {
	_isPlaying = isPlaying;
    }

    if (_audioPlayer != nil || _avPlayer != nil) {
	if (!_hijackInProgress) {
	    /* stop the MP player and start the appropriate player (or use fixIfHijacked
	     * if necessary).
	     */
	    if (isPlaying) {
		[_mpPlayer stop];
		_forceSetQueue = YES;
		[self play];
	    }
	}
    }

    [self updatePlayerState];

    if (!_isPlaying) {
	if (!_hijackInProgress) {
	    NSLog(@"- Saving due to pause");
	    [_playContext saveListToFile];
	}
    }
}

/* get duration of currently playing item; if the answer isn't set yet,
 * then return -1.0.
 */
- (float) currentPlaybackDuration: (BOOL *) validp
{
    MPMediaItem *item;
    MFANMediaItem *mfanItem;
    CMTime cmTime;
    float rval;
    BOOL valid;

    /* get MPMediaItem, if any */
    mfanItem = [self currentMFANItem];
    item = [mfanItem item];
    valid = YES;	/* by default */

    if (_avMediaArray == nil) {
	rval = 0.0;
    }
    else if (_audioPlayer) {
	/* used for infinite HTTP streams */
	rval = 300.0;
	valid = NO;
    }
    else if (_avPlayer && item == nil) {
	cmTime = [_avItem duration];
	if (CMTIME_IS_INDEFINITE(cmTime)) {
	    rval = 300.0;	/* downloadable file */
	    valid = NO;
	}
	else
	    rval = CMTimeGetSeconds(cmTime);
    }
    else {
	if (item) {
	    rval = [[item valueForProperty: MPMediaItemPropertyPlaybackDuration] floatValue];
	}
	else {
	    rval = 36000.0;
	    valid = NO;
	}
    }
    if (validp)
	*validp = valid;

    return rval;
}

/* slider dragged by user to a new position */
- (void) sliderDragged
{
    if (_avMediaArray == nil || _usePlayer == MFANUseAudioPlayer)
	return;

    if (_sliderDragTimer) {
	[_sliderDragTimer invalidate];
	_sliderDragTimer = nil;
    }

    /* remember the last value we saw during a drag */
    _sliderDragValue = [_slider value];

    _sliderDragTimer = [NSTimer scheduledTimerWithTimeInterval: 0.5
				target:self
				selector:@selector(sliderDragFired:)
				userInfo:nil
				repeats: NO];
}

- (void) sliderDragFired: (id) junk
{
    float songLength;
    float newTime;
    BOOL wasPlaying;

    _sliderDragTimer = nil;

    wasPlaying = _isPlaying;

    /* must be stopped to change info */
    [self pause];

    songLength = [self currentPlaybackDuration: NULL];
    newTime = songLength * _sliderDragValue;
    [_slider setValue: _sliderDragValue];

    [self setPlaybackTime: newTime];

    // [self updateSliderView];

    if (wasPlaying)
	[self play];
}

/* move back N seconds */
- (void) backPressed: (id) sender withData: (NSNumber *) movement
{
    float newTime;

    if (_avMediaArray == nil || [[self currentMFANItem] isWebStream])
	return;

    [self pause];

    newTime = [self currentPlaybackTime];
    if (newTime > 20.0)
	newTime -= 20.0;
    else
	newTime = 0.0;

    [self setPlaybackTime: newTime];

    [self updateSliderView];

    [self play];
}

/* move forward N seconds */
- (void) fwdPressed: (id) sender withData: (NSNumber *) movement
{
    float newTime;
    float duration;

    if (_avMediaArray == nil || [[self currentMFANItem] isWebStream])
	return;

    [self pause];

    duration = [self currentPlaybackDuration: NULL];
    newTime = [self currentPlaybackTime] + 20.0;
    if (newTime > duration)
	newTime = duration;

    [self setPlaybackTime: newTime];

    [self updateSliderView];

    [self play];
}

/* switch to previous song */
- (void) prevPressed: (id) sender withData: (NSNumber *) movement
{
    long count;

    count = [_avMediaArray count];
    if (count <= 0)
	return;

    [self pause];

    if ([_avMediaItem isWebStream]) {
	/* don't mess with playbackTimes for audio streams */
	if (_avMediaIndex == 0)
	    [self setIndex: count-1 rollForward: NO];
	else
	    [self setIndex: _avMediaIndex-1 rollForward: NO];
	[self updateSliderView];
    }
    else {
	if ([self currentPlaybackTime] < 4.0) {
	    if (_avMediaIndex == 0)
		[self setIndex: count-1 rollForward: NO];
	    else
		[self setIndex: _avMediaIndex-1 rollForward: NO];
	}
	else {
	    [self setPlaybackTime: 0.0];
	    [self updateSliderView];
	}
    }

    [self play];
}

- (void) playPressed: (id) sender withData: (NSNumber *) movement
{
    if (_isPlaying) {
	[self pause];
    }
    else {
	[self play];
    }

    [self updatePlayerState];
}

- (void) nextPressed: (id) sender withData: (NSNumber *) movement
{
    long count;
    long newIndex;

    count = [_avMediaArray count];
    if (count == 0)
	return;

    [self pause];

    newIndex = _avMediaIndex+1;

    if (newIndex >= count)
	[self setIndex: 0 rollForward: YES];
    else
	[self setIndex: newIndex rollForward: YES];

    [self play];
}

/* redo the last setQueueWithContext, if we're using the MP player, since the external
 * controllers (like car audio) reset the queue.  This must be called with the 
 * player paused.
 */
- (void) resetQueue
{
    if ( _usePlayer == MFANUseMainPlayer && [_mpMediaArray count] > 0) {
	_forceSetQueue = YES;
    }
    _forceUpdate = YES;
}

/* caller is responsible for doing a setIndex call after this */
- (void) setQueueWithContext: (MFANPlayContext *) playContextp
{
    MFANSetList *setList;
    int count=0;
    int mustUseAv=0;
    int mustUseMp=0;
    int mustUseAudio = 0;
    MFANMediaItem *mfanItem;
    NSMutableArray *tempArray;
    MPMediaItem *mpItem;
    MPMediaItem *baseItem;
    int baseIx=0;
    int i;
    MFANChannelType channelType;

    _playContext = playContextp;
    setList = [playContextp setList];
    baseItem = nil;
    channelType = MFANChannelMusic;
    if (![setList isEmpty]) {
	_avMediaArray = [[playContextp setList] itemArray];

	/* walk through the items, seeing if there are any that require MPMediaPlayer
	 * or AVMediaPlayer.  If we find some that require the former, remember one
	 * such item, so we can use it as filler in the playlist. 
	 */
	for(mfanItem in _avMediaArray) {
	    if ([mfanItem item] == nil) {
		/* must use AV or Audio player for this */
		if ([mfanItem isWebStream]) {
		    channelType = MFANChannelRadio;
		    mustUseAudio++;
		}
		else if ([mfanItem isPodcast]) {
		    channelType = MFANChannelPodcast;
		    mustUseAv++;
		}
		else {
		    channelType = MFANChannelMusic;
		    mustUseAv++;
		}
	    }
	    else {
		/* may use MP player here */
		channelType = MFANChannelMusic;
		if ([mfanItem localUrl].length == 0) {
		    mustUseMp++;
		}
		if (baseItem == nil)
		    baseItem = [mfanItem item];
	    }
	    count++;
	}

	/* if we have any items that must be played with AV player, use the
	 * AV player; we won't be able to play the items that require MP player in this
	 * case, but the user can always download them.  Otherwise, use the MP player.
	 */
	if (mustUseAv > 0) {
	    _usePlayer = MFANUseAvPlayer;
	}
	else if (mustUseAudio > 0) {
	    _usePlayer = MFANUseAudioPlayer;
	}
	else {	/* used to be if mustUseMp > 0 to get AV player by default */
	    _usePlayer = MFANUseMainPlayer;
	    /* and setup the media array */
	    i=0;
	    tempArray = [NSMutableArray arrayWithCapacity: count];
	    for(mfanItem in _avMediaArray) {
		mpItem =  [mfanItem item];
		if (mpItem != nil)
		    [tempArray addObject: mpItem];
		else {
		    /* fill in with junk, but playable, item, to keep indices in this
		     * array matching those in _avMediaArray.
		     */
		    [tempArray addObject: baseItem];
		    baseIx = i;
		}
		i++;
	    }
	    _mpMediaArray = tempArray;
	    _forceSetQueue = YES;
	}
#if 0
	else {
	    _usePlayer = MFANUseAvPlayer;
	}
#endif
	NSLog(@"- setQueue mustuseav=%d mustusemp=%d total=%d", mustUseAv, mustUseMp, count);

	[_viewCon setChannelType: channelType];
    }
    else {
	_usePlayer = MFANUseMainPlayer;
	_avMediaArray = nil;
    }
    _forceUpdate = YES;
}

/* internal utility to ensure the music quiet player is working; we use this player
 * to keep the app alive when playing a type of audio (MPMusicPlayer) that doesn't
 * require the app to supply data.  IOS stops us if we're playing that type of audio,
 * but we need to keep running to update state, so we merge in a completely silent
 * audio stream.
 */
- (void) startBkgPlayer
{
    if (![_bkgPlayer play]) {
	NSLog(@"!Play failed");
    }
    else {
	_bkgPlaying = YES;
    }
}

/* start the music going */
- (void) play
{
    NSLog(@"- in play");

    if (_avMediaArray == nil) {
	[self pause];
    }
    else {
	/* if we're switching to any other player than MPMusicPlayer,
	 * and the background player is still playing silence, we turn
	 * it off in a little bit.
	 */
	if (_hackPlayer == nil && _bkgPlaying) {
	    [NSTimer scheduledTimerWithTimeInterval: 0.25
		     target:self
		     selector:@selector(bounceTimerFired:)
		     userInfo:nil
		     repeats: NO];
	}
	if (_avPlayer) {
	    NSLog(@"- play starts avplayer");
	    [_avPlayer play];
	}
	else if (_audioPlayer) {
	    NSLog(@"- play resumes audioplayer");

	    if ([_audioPlayer stopped]) {
		/* can't just resume, must restart */
		_audioPlayer = nil;
		[self setIndex: _avMediaIndex rollForward: YES];
	    }
	    else {
		[_audioPlayer resume];
	    }
	}
	else if (_hackPlayer) {
	    [_hackPlayer play];
	    [_playerStatus setStateHint: MPMusicPlaybackStatePlaying];
	    [self startBkgPlayer];
	}
	_isPlaying = YES;
    }

    [self updatePlayerState];
}

/* pause the player; necessary before we can change the song, or the position within the
 * song.
 */
- (void) pause
{
    
    if (_avPlayer) {
	if ([self isPlayingWithForce: NO]) {
	    [_avPlayer pause];
	}
    }
    else if (_audioPlayer) {
	[_audioPlayer pause];
    }

    /* always stop the MP player, since the car starts it randomly */
    [_mpPlayer pause];
    [_playerStatus setStateHint: MPMusicPlaybackStatePaused];
    [_bkgPlayer stop];
    _bkgPlaying = NO;

    _isPlaying = NO;

    [self updatePlayerState];
}

/* generic function after music playing changes, to update UI, and do other housekeeping */
- (void) nowPlayingChanged: (id) notification
{
    MFANMediaItem *songp;

    NSLog(@"- in nowPlayingChanged");

    /* if someone presses a next/back key, the song will change and we
     * need to remove the stars, if any.  Can't update rating, since
     * this isn't the rated song.
     */
    [self removeStars: NO];

    [self updateSongState];

    songp = [self currentMFANItem];
    if (songp != _lastDisplayedSong) {
	if (!_hijackInProgress) {
	    /* every time the song changes, save the file */
	    NSLog(@"- Saving playcontext to file --start");
	    [_playContext saveListToFile];
	    NSLog(@"- Saving playcontext to file --end");
	}
	_lastDisplayedSong = songp;
    }
}

/* generic function called when we switch between playing and not playing */
- (void) updatePlayerState
{
    _lastStateChangeMs = osp_get_ms();

    NSLog(@"- updatePlayerState isplaying=%d", _isPlaying);

    if (_isPlaying) {
	[self startSliderTimer];
	[_playButton setTitle: @"Pause"];

	/* if the player just started via an external command, we may not have had
	 * a chance to start the bkgPlayer if necessary, so do so now.
	 */
	if (_usePlayer == MFANUseMainPlayer && !_bkgPlaying) {
	    /* MP Player is playing, but bkg player isn't, so start it */
	    [self startBkgPlayer];
	}
    }
    else {
	[_playButton setTitle: @"Play"];
    }
}

- (void) updateSliderView
{
    float currentTime;
    float songLength;
    int remainingSecs;
    BOOL durationValid;

    currentTime = [self currentPlaybackTime];
    songLength = [self currentPlaybackDuration: &durationValid];

    if (_usePlayer == MFANUseAudioPlayer) {
	/* internet radio, center bar */
	[_slider setValue: 0.5];
	[_timeLabel setText: @"--:--"];

	/* and if text is visible, update it */
	if (_radioLabel1 && _audioPlayer) {
	    [_radioLabel1 setText: [_audioPlayer getStreamUrl]];
	    [_radioLabel1 setNeedsDisplay];

	    [_radioLabel2 setText: [NSString stringWithFormat: @"%@: %6.1f KB/s",
					     [_audioPlayer getEncodingType],
					     [_audioPlayer dataRate]/1000.0]];
	    [_radioLabel2 setNeedsDisplay];
	}
    }
    else {
	[_slider setValue: (currentTime / songLength)];
	remainingSecs = (int) (songLength - currentTime);
	[_timeLabel setText: [NSString stringWithFormat:
					   @"%d:%02d", remainingSecs/60, remainingSecs%60]];
    }
}

- (void) sliderTimerFired: (id) junk
{
    float currentTime;
    NSTimeInterval now;
    MFANMediaItem *mfanItem;
    long count;
    long nextIx;

    if (!_isPlaying) {
	if (osp_get_ms() - _lastStateChangeMs > 3000) {
	    /* the player has been been stopped for at least 3 seconds, so turn off
	     * the slider timer and the background player.  We can't turn off bkgPlayer when we
	     * first stop playing, since if we're in the background, and the stop event is
	     * just a quick on/off/on transition, ios will stop our app from running instantly.
	     * So, we're debouncing the 'off' transition by waiting for the player to be stopped
	     * for a few seconds before actually stopping the timers and players.
	     */
	    [self stopSliderTimer];
	    if (_bkgPlaying) {
		[_bkgPlayer stop];
		_bkgPlaying = NO;
	    }
	}
    }

    currentTime = [self currentPlaybackTime];

    /* again, the stupid car starts us randomly, and this will stop it if
     * it starts.
     */
    if (_audioPlayer) {
	/* is this expensive? */
	if ([_mpPlayer playbackState] == MPMusicPlaybackStatePlaying)
	    [_mpPlayer pause];
    }
    else if (_avPlayer) {
	// [_mpPlayer pause];

	if (_isPlaying && currentTime == _lastTime) {
	    _lastTimeUnchanged++;
	    if (_lastTimeUnchanged > 20) {
		MFANWarn *warn = [[MFANWarn alloc] initWithTitle:@"Song unavailable"
						   message:@"Trying next song"
						   secs: 1.0];
		warn = nil;	/* self-destructs */

		_lastTimeUnchanged = 0;
		_isPlaying = NO;
		count = [_avMediaArray count];
		nextIx = _avMediaIndex+1;
		if (nextIx >= count)
		    nextIx = 0;
		[self setIndex: nextIx rollForward: YES];
		[self play];
		return;
	    }
	}
	else {
	    _lastTimeUnchanged = 0;
	    _lastTime = currentTime;
	}

	[self updateNowPlayingCenter];
    }

    [self updateSliderView];

    mfanItem = [self currentMFANItem];
    if ([mfanItem trackPlaybackTime]) {
	[mfanItem setPlaybackTime: currentTime];
    }

    now = [NSDate timeIntervalSinceReferenceDate];
    if (now - [_playContext lastSavedTime] > 60.0) {
	if (!_hijackInProgress) {
	    NSLog(@"- Saving playcontext to file (timerbased) --start");
	    [_playContext saveListToFile];
	    NSLog(@"- Saving playcontext to file (timerbased) --stop");
	}
    }
}

- (void) startSliderTimer
{
    if (_sliderUpdateTimer)
	return;

    _sliderUpdateTimer = [NSTimer scheduledTimerWithTimeInterval: 0.5
				  target:self
				  selector:@selector(sliderTimerFired:)
				  userInfo:nil
				  repeats: YES];
}

- (void) stopSliderTimer
{
    if (_sliderUpdateTimer) {
	[_sliderUpdateTimer invalidate];
	_sliderUpdateTimer = nil;
    }
}

- (float) currentPlaybackTime
{
    float rval;

    if (_avMediaArray == nil) {
	NSLog(@"!CPT has null array causing playbackTime to return 0!");
	rval = 0.0;
    }
    else if (_avPlayer) {
	rval = CMTimeGetSeconds([_avPlayer currentTime]);
    }
    else if (_hackPlayer) {
	//rval = [_hackPlayer currentPlaybackTime];
	rval = [_playerStatus pbtGetTime];
    }
    else
	rval = 0.0;

    return rval;
}

- (void) setPlaybackTime: (float) newTime
{
    CMTime newCMTime;

    /* nothing to do for audio stream */
    if (_avPlayer) {
	newCMTime = CMTimeMake(newTime*100, 100);
	[_avPlayer seekToTime: newCMTime];
    }
    else if (_hackPlayer) {
	/* the next few lines are what it takes to get this to work for iOS 11.1;
	 * every fucking release changes the state that's required of the MPMediaPlayer
	 * before a setCurrentPlaybackTime call will actually do anything.
	 */
	[_hackPlayer play];
	[_hackPlayer pause];
	[NSThread sleepForTimeInterval: 0.2];

	[_hackPlayer setCurrentPlaybackTime: newTime];
	[_playerStatus pbtSetTime: newTime];
    }
}

- (long) currentIndex
{
    return _avMediaIndex;
}

- (MFANMediaItem *) currentMFANItem
{
    MFANMediaItem *mfanItem;

    if (_avMediaIndex >= [_avMediaArray count])
	return nil;

    mfanItem = [_avMediaArray objectAtIndex: _avMediaIndex];
    return mfanItem;
}

/* returns nil if no MPMediaItem for this array element */
- (MPMediaItem *) currentItem
{
    MFANMediaItem *mfanItem;

    if (_avMediaIndex >= [_avMediaArray count])
	return nil;

    mfanItem = [_avMediaArray objectAtIndex: _avMediaIndex];
    return [mfanItem item];
}

/* check to see if we're hijacked and fix it if we are; if force is set, 
 * skip the check and assume caller already determined we're hijacked.
 */
- (BOOL) fixIfHijacked: (BOOL) force
{
    BOOL wasPlaying;
    MFANWarn *warn;

    /* don't start this state machine again until it has finished */
    if (_hijackInProgress)
	return NO;

    /* also, sometimes the stupid car radio starts playing the
     * first song in the song list (not our current playlist);
     * don't let it do so unless we think that's the song we
     * should be playing.
     *
     * In this case, the ipod player is actually given a new queue
     * of songs to play, so we have to reset its queue as well.
     */
    if ( _playContext != nil && (force || [self hijacked])) {
	wasPlaying = _isPlaying;

	NSLog(@"- fixIfhijacked says yes, so resetting songs");
	warn = [[MFANWarn alloc] initWithTitle:@"Car hijack"
				 message: @"Car audio tried to hijack iPhone playlist"
				 secs: _hijackDelay];

	/* stop the MPMusicPlayer, since sometimes it starts running
	 * even when we're running the AVPlayer.
	 */
	[_mpPlayer stop];
	/* we'll set forceSetQueue after resuming */

	/* mark that a hijack is going on, so that we don't accidentally save
	 * the hijacked state in our playlist.
	 */
	_hijackInProgress = YES;

	/* we pause long enough for the car to think it succeeded
	 * at changing the playlist.  Then we stomp it.
	 */
	NSLog(@"- sleeping for hijack");
	if (_hijackTimer == nil) {
	    _hijackTimer = [NSTimer scheduledTimerWithTimeInterval: _hijackDelay
				    target:self
				    selector:@selector(resumeAfterHijacked:)
				    userInfo:nil
				    repeats: NO];
	}

	return YES;
    }

    return NO;
}
    
- (void) resumeAfterHijacked: (id) junk
{
    _hijackTimer = nil;

    /* the bogus song is playing, but it isn't the song that we think we should be
     * playing.  Switch to what we think we should be playing.
     */
    _isPlaying = YES;
    if (_usePlayer == MFANUseMainPlayer) {
	[_mpPlayer stop];
	_forceSetQueue = YES;	/* since we stopped mpPlayer */

	/* if we're using the MPMusicPlayerController, reset the queue */
	[self resetQueue];
    }

    [self setIndex: [_playContext getCurrentIndex] rollForward: YES];

    [self setPlaybackTime: [_playContext currentTime]];
	
    /* used to check wasPlaying here -- probably worthless to do */
    [self play];

    _hijackInProgress = NO;
}

/* called to update the AV Player's display center; has side effect of
 * setting flag indicating that the info was actually available.  This
 * info doesn't show up unless we call AVAudioSession to set the
 * session type.
 */
- (void) updateNowPlayingCenter
{
    float playbackDuration;

    if (_durationValid || (_avPlayer == nil && _audioPlayer == nil))
	return;

    /* see if playback duration is valid now */
    playbackDuration = [self currentPlaybackDuration: &_durationValid];
    if (!_durationValid)
	return;

    NSLog(@"- duration now valid at %f", playbackDuration);

    [_nowPlayingInfo setObject: [NSNumber numberWithFloat: playbackDuration]
		     forKey: MPMediaItemPropertyPlaybackDuration];
    [[MPNowPlayingInfoCenter defaultCenter] setNowPlayingInfo: _nowPlayingInfo];

    [self setupAudioSession: NO];
}

- (void) updateSongState
{
    MPMediaItem *songp;
    MPMediaItemArtwork *art;
    UIImage *artImage;
    UIButton *newArtView;
    NSString *songTitle;
    NSString *albumTitle;
    NSString *artistTitle;
    CGRect artRect;
    uint32_t songType;
    NSString *plPosition;
    NSNumber *isCloudBool;
    unsigned int firstUsed;
    int songIndex;
    BOOL shouldShowRecord=NO;

    NSLog(@"- updateSongState");
    if (_avMediaArray == nil) {
	songp = nil;
    }
    else {
	songp = [_avMediaItem item];
    }
    NSLog(@"- updateSongState to songp=%p title=%@", songp, [songp valueForProperty:@"title"]);

    /* turn off _forceUpdate, which may have gotten us here */
    _forceUpdate = NO;

    if (songp) {
	/* used to check [[songp valueForProperty:
	 * MPMediaItemPropertyIsCloudItem] boolValue], but only if
	 * songp exists and has a URL are we using the local storage.
	 */
	isCloudBool = [songp valueForProperty: MPMediaItemPropertyIsCloudItem];
	
	albumTitle = [songp valueForProperty: MPMediaItemPropertyAlbumTitle];
	if (albumTitle == nil || [albumTitle length] == 0)
	    albumTitle = @"[No Album]";
	
	art = [songp valueForProperty: MPMediaItemPropertyArtwork];
	artRect = [art bounds];
	artImage = [art imageWithSize: artRect.size];
	if (artImage == nil || MFANTopSettings_forceNoArt) {
	    artImage = [UIImage imageNamed: @"recordfield.jpg"];
	    artImage = resizeImage2(artImage, _artFrame.size);
	}
	newArtView = [[UIButton alloc] init];
	[self augmentArtView: newArtView];
	[newArtView setBackgroundImage: artImage forState: UIControlStateNormal];
	[newArtView setBackgroundImage: artImage forState: UIControlStateHighlighted];

	isCloudBool = [songp valueForProperty: MPMediaItemPropertyIsCloudItem];
	newArtView.layer.borderWidth = 3.0;
	if ([isCloudBool boolValue]) {
	    newArtView.layer.borderColor= [UIColor blueColor].CGColor;
	}
	else {
	    newArtView.layer.borderColor= [UIColor blackColor].CGColor;
	}

	songTitle = [songp valueForProperty: MPMediaItemPropertyTitle];
	[_songLabel setText: songTitle];
	[_songLabel setNeedsDisplay];
	
	albumTitle = [songp valueForProperty: MPMediaItemPropertyAlbumTitle];
	artistTitle = [songp valueForProperty: MPMediaItemPropertyArtist];
	
	if ([artistTitle length] > 0 && [albumTitle length] > 0) {
	    artistTitle = [artistTitle stringByAppendingString: @" - "];
	    artistTitle = [artistTitle stringByAppendingString: albumTitle];
	    [_albumArtistLabel setText: artistTitle];
	}
	else if ([albumTitle length] > 0) {
	    [_albumArtistLabel setText: albumTitle];
	}
	else if ([artistTitle length] > 0){
	    [_albumArtistLabel setText: artistTitle];
	}
	else {
	    [_albumArtistLabel setText: @"[None]"];
	}
	[_albumArtistLabel setNeedsDisplay];
	
	songType = (uint32_t) [[songp valueForProperty: MPMediaItemPropertyMediaType]
				  integerValue];
	
	/* save position and song in playContext, so it will remember our state */
	if (!_hijackInProgress) {
	    [_playContext setCurrentTime: [self currentPlaybackTime]];
	    [_playContext setCurrentIndex: _avMediaIndex];
	}
    }
    else if (_avMediaArray != nil) {
	/* no song, make up our own image, and use URL from _avMediaItem */
	songType = ([_avMediaItem isWebStream]? 101 : 100);
	artImage = [_avMediaItem artworkWithSize: _artFrame.size.width];

	if (_audioPlayer != nil) {
	    songTitle = [_audioPlayer getCurrentPlaying]; /* song name */
	    NSLog(@"- playerview audioplayer %p returns song %p", _audioPlayer, songTitle);
	    albumTitle = @"via Internet";
	    artistTitle = [_avMediaItem title];
	    [_hist addHistoryStation: artistTitle withSong: songTitle];
	    shouldShowRecord = YES;
	}
	else {
	    songTitle = [_avMediaItem title];
	    artistTitle = [_avMediaItem albumTitle];
	}

	[_songLabel setText: songTitle];
	[_songLabel setNeedsDisplay];

	[_albumArtistLabel setText: artistTitle];
	[_albumArtistLabel setNeedsDisplay];

	newArtView = [[UIButton alloc] init];
	[self augmentArtView: newArtView];
	[newArtView setBackgroundImage: artImage forState: UIControlStateNormal];
	[newArtView setBackgroundImage: artImage forState: UIControlStateHighlighted];
    }
    else {
	/* no active media right now */
	songType = ([_avMediaItem isWebStream]? 101 : 100);
	artImage = [UIImage imageNamed: @"recordfield.jpg"];
	artImage = resizeImage2(artImage, _artFrame.size);

	[_songLabel setText: @"- - - -"];
	[_songLabel setNeedsDisplay];

	[_albumArtistLabel setText: @"- - - -"];
	[_albumArtistLabel setNeedsDisplay];

	newArtView = [[UIButton alloc] init];
	[self augmentArtView: newArtView];
	[newArtView setBackgroundImage: artImage forState: UIControlStateNormal];
	[newArtView setBackgroundImage: artImage forState: UIControlStateHighlighted];
    }
    
    plPosition = [NSString stringWithFormat: @"%ld of %ld",
			   (long) _avMediaIndex+1,
			   (long) [_avMediaArray count]];
    [_plPositionLabel setText: plPosition];
    [_plPositionLabel setNeedsDisplay];
	
    firstUsed = [MFANTopSettings firstUsed];

    if ([MFANTopSettings sendUsage]) {
	[_comm announceWho: @"Anon"
	       song: @""
	       artist: @""
	       songType: songType
	       acl: firstUsed];
    }
	
    if (_avPlayer != nil || _audioPlayer != nil) {
	if (_audioPlayer != nil) {
	    songIndex = [_audioPlayer getSongIndex]+1; /* player's index is 0 based */
	}
	else {
	    songIndex = (int) [_playContext getCurrentIndex];
	}
	_nowPlayingInfo = [[NSMutableDictionary alloc] init];
	[_nowPlayingInfo setObject: [NSNumber numberWithDouble: 1.0]
			 forKey: MPNowPlayingInfoPropertyPlaybackRate];

	[_nowPlayingInfo setObject: [NSNumber numberWithFloat: [self currentPlaybackTime]]
			 forKey: MPNowPlayingInfoPropertyElapsedPlaybackTime];
	[_nowPlayingInfo setObject: [NSNumber numberWithFloat:
						  [self currentPlaybackDuration: &_durationValid]]
			 forKey: MPMediaItemPropertyPlaybackDuration];

	[_nowPlayingInfo setObject: [NSNumber numberWithUnsignedInt: songIndex]
			 forKey: MPNowPlayingInfoPropertyPlaybackQueueIndex];
	if (_audioPlayer != nil) {
	    /* internet radio, queue should be really large, so that
	     * we never have queueIndex bigger than queueCount.
	     */
	    [_nowPlayingInfo setObject: [NSNumber numberWithUnsignedInt:
						      (int) 2000]
			     forKey: MPNowPlayingInfoPropertyPlaybackQueueCount];
	}
	else {
	    /* normal player */
	    [_nowPlayingInfo setObject: [NSNumber numberWithUnsignedInt:
						      (int) [_avMediaArray count]]
			     forKey: MPNowPlayingInfoPropertyPlaybackQueueCount];
	}
	if (art != nil)
	    [_nowPlayingInfo setObject: art forKey: MPMediaItemPropertyArtwork];
	if (songTitle != nil)
	    [_nowPlayingInfo setObject: songTitle forKey: MPMediaItemPropertyTitle];
	if (artistTitle != nil)
	    [_nowPlayingInfo setObject: artistTitle forKey: MPMediaItemPropertyArtist];
	if (albumTitle != nil)
	    [_nowPlayingInfo setObject: albumTitle forKey: MPMediaItemPropertyAlbumTitle];
	[[MPNowPlayingInfoCenter defaultCenter] setNowPlayingInfo: _nowPlayingInfo];
    }

    NSLog(@"- updateSongState about to update art view");
    [self updateArtView: newArtView];
    
    NSLog(@"- updateSongState done songp=%p", songp);
}

/* setup artView properties */
- (void) augmentArtView: (UIButton *) artView
{
    [artView addTarget: self
	     action:@selector(artPressed:withEvent:)
	     forControlEvents: UIControlEventTouchUpInside];
    [artView setFrame: _artFrame];
}

- (void) updateArtView: (UIButton *) viewp
{
    if (viewp == _artView) {
	NSLog(@"- updateArtView noop");
	return;
    }

    if (_artView) {
	[_artView removeFromSuperview];
	_artView = nil;
    }

    if (viewp != nil) {
	_artView = viewp;
	[_parent addSubview: _artView];
	[_artView setNeedsDisplay];
    }
}

/* return true if the MPMusicPlayer is playing and is playing the
 * wrong song, or if it is playing and it shouldn't be at all.
 * Sometimes the car starts the music player going for no real reason, and
 * we have to stop it.
 *
 * Note that this is called with _avMediaIndex potentially one song
 * old.
 */
- (BOOL) hijacked
{
    MPMediaItem *shouldBeItem;
    MPMediaItem *nowPlayingItem;
    long shouldBeIx;
    long nowPlayingIx;
    long avCount;
    long jumpDistance;

    if ([_avMediaArray count] == 0)
	return NO;

    if (_usePlayer != MFANUseMainPlayer) {
	/* if the AVPlayer should be playing, but the background
	 * player is playing, then we've been hijacked, otherwise we
	 * haven't (the car radio never messes with the AVPlayer).
	 */
	if ([_playerStatus stateWithForce: NO] == MPMusicPlaybackStatePlaying)
	    return YES;
	else
	    return NO;
    }
    else {
	avCount = [_mpMediaArray count];

	/* we're playing iTunes music from the cloud and so are using the MPMusicPlayer
	 * instead of AVPlayer.  We've been hijacked if the song playing now isn't the
	 * one that should be playing (or off by one).
	 */
	nowPlayingIx = [_hackPlayer indexOfNowPlayingItem];

	/* shouldBeIx is a sanity checked version of _avMediaIndex, which may be one
	 * song old, since after a music changed event, it won't have been updated.
	 */
	if (_avMediaIndex < avCount)
	    shouldBeIx = _avMediaIndex;
	else
	    shouldBeIx = 0;

	/* compute the absolute value of the distance between the
	 * index playing and the index that should be playing, modulo
	 * avCount.  We want the value to be within avCount/2 of 0.
	 */
	jumpDistance = nowPlayingIx - shouldBeIx;
	if (jumpDistance < (-avCount/2))
	    jumpDistance += avCount;
	else if (jumpDistance > (avCount / 2))
	    jumpDistance -= avCount;
	if (jumpDistance < 0)
	    jumpDistance = -jumpDistance;

	/* for safety, we maintain a count of how many hijackings
	 * occur in a row, meaning without a gap of 20 seconds or more
	 * in the stream of hijackings.  If this count gets > 5, we
	 * stop detecting hijackings, since there may be a bug causing
	 * them to be found too often.
	 *
	 * That's what happend when IOS started wrapping some
	 * MPMediaItems with a proxy structure, so that the comparison
	 * to see if the song playing is the right one.  We'd rather
	 * have a fail safe mechanism here.
	 */
	if (osp_get_sec() - _lastHijackSecs > 60) {
	    _lastHijackCount = 0;
	}
	if (_lastHijackCount >= 4)
	    return NO;

	/* removed check for "nowPlayingIx <= 1" since the stupid
	 * music player is now sometimes reseting us back a moderate
	 * amount, instead of our having only to deal with the car
	 * resetting us to the first or second song.
	 */
	if (nowPlayingIx <= 1) {
	    /* watch for us playing a significantly different index
	     * than we expect to be playing (our code in setIndex: to
	     * change currently playing item updates the expected item
	     * as well).  But allow for a song's worth of skew, just
	     * in case an update gets delayed.  And watch for moving
	     * back from the first song to the very last with a "back"
	     * command.  All this is handled by the computation above.
	     */
	    if (jumpDistance > 1) {
		NSLog(@"- hijacked really playing %ld when shouldBeIx=%ld",
		      (long) nowPlayingIx, (long) shouldBeIx);
		_lastHijackCount++;
		_lastHijackSecs = osp_get_sec();
		return YES;
	    }

	    /* if the song playing doesn't match our playlist's idea of the song
	     * playing, then we've had a new playlist installed under us.
	     */
	    nowPlayingItem = [_hackPlayer nowPlayingItem];
	    if (nowPlayingIx < avCount)
		shouldBeItem = _mpMediaArray[nowPlayingIx];
	    else
		shouldBeItem = nil;

	    if ( shouldBeItem != nil && nowPlayingItem != nil && 
		 [shouldBeItem persistentID] != [nowPlayingItem persistentID]) {
		NSLog(@"- hijack avIx=%d nowPlayingIx=%d", (int) _avMediaIndex, (int) shouldBeIx);
		NSLog(@"- hijacked mpItem=%llx nowPlayingItem=%llx",
		      [shouldBeItem persistentID], [nowPlayingItem persistentID]);
		_lastHijackCount++;
		_lastHijackSecs = osp_get_sec();
		return YES;
	    }
	    return NO;
	}
	else {
	    /* this is leftover from the check for nowPlayingIx <= 1 that's been removed */
	    return NO;
	}
    }
}

- (void) removeStars: (BOOL) update
{
    MPMediaItem *item;
    long currentStars;
    long newStars;

    if (_starsView != nil) {
	if (update) {
	    item = [self currentItem];
	    if (item != nil) {
		currentStars = [[item valueForProperty: MPMediaItemPropertyRating]
				   unsignedIntegerValue];

		newStars = _starsView.enabledCount;
		if (newStars != currentStars) {
		    [item setValue:[NSNumber numberWithInteger: newStars] forKey:@"rating"];
		}
	    }
	}

	[_starsView clearCallback];
	[_starsView removeFromSuperview];
	_starsView = nil;
    }

    if (_radioLabel1 != nil) {
	[_radioLabel1 removeFromSuperview];
	_radioLabel1 = nil;
    }

    if (_radioLabel2 != nil) {
	[_radioLabel2 removeFromSuperview];
	_radioLabel2 = nil;
    }
}

- (void) stopRecording
{
    if (_audioPlayer != nil)
	[_audioPlayer stopRecording];
}

- (int) startRecordingFor: (id) who sel: (SEL) selector
{
    if (_audioPlayer != nil)
	[_audioPlayer startRecordingFor: who sel: selector];
    else {
	MFANWarn *warn;
	warn = [[MFANWarn alloc] initWithTitle:@"Can't Record"
				 message: @"Only radio stations can be recorded"
				 secs: 1.5];
	return -1;
    }

    return 0;
}

- (void)
artPressed: (id) button withEvent: (UIEvent *)event
{
    MPMediaItem *item;
    long currentStars;
    MFANChannelType channelType;
    CGFloat totalHeight = _artFrame.size.height;
    
    channelType = [_viewCon channelType];

    if (!_artTextVisible) {
	_artTextVisible = YES;
	if (channelType == MFANChannelMusic) {
	    item = [self currentItem];
	    if (item != nil) {
		_starsView = [[MFANStarsView alloc] initWithFrame: _starsFrame
						    background: YES];
		_starsView.layer.borderColor = [MFANTopSettings baseColor].CGColor;
		_starsView.layer.borderWidth = 2.0;
		[_starsView addCallback: self withAction: @selector(starsPressed:)];

		currentStars = [[item valueForProperty: MPMediaItemPropertyRating]
				   unsignedIntegerValue];
		_starsView.enabledCount = currentStars;
		[_parent addSubview: _starsView];
	    }
	}
	else if (channelType == MFANChannelRadio) {
	    CGFloat textHeight = totalHeight / 6;	/* nboxes + 1 */

	    _radioFrame1.size.height = textHeight;
	    _radioFrame1.size.width = _artFrame.size.width * 0.9;
	    _radioFrame1.origin = _artFrame.origin;
	    _radioFrame1.origin.x += _radioFrame1.size.width * .05; /* center */
	    _radioFrame1.origin.y += textHeight/2;

	    _radioLabel1 = [[MarqueeLabel alloc] initWithFrame: _radioFrame1];
	    [_radioLabel1 setTextColor: [MFANTopSettings textColor]];
	    [_radioLabel1 setFont: [MFANTopSettings basicFontWithSize: textHeight * 0.7]];
	    [_radioLabel1 setTextAlignment: NSTextAlignmentCenter];
	    [_radioLabel1 setBackgroundColor: [UIColor colorWithRed: 0.8
						       green: 0.8
						       blue: 0.8
						       alpha: 1.0]];

	    _radioLabel1.baselineAdjustment = UIBaselineAdjustmentAlignCenters;
	    _radioLabel1.layer.borderColor = [MFANTopSettings baseColor].CGColor;
	    _radioLabel1.layer.borderWidth = 2.0;
	    [_parent addSubview: _radioLabel1];

	    _radioFrame2 = _radioFrame1;
	    _radioFrame2.origin.y += 3*textHeight/2;
	    _radioLabel2 = [[UILabel alloc] initWithFrame: _radioFrame2];
	    [_radioLabel2 setTextColor: [MFANTopSettings textColor]];
	    [_radioLabel2 setFont: [MFANTopSettings basicFontWithSize: textHeight * 0.7]];
	    [_radioLabel2 setTextAlignment: NSTextAlignmentCenter];
	    [_radioLabel2 setAdjustsFontSizeToFitWidth: YES];
	    [_radioLabel2 setBackgroundColor: [UIColor colorWithRed: 0.8
						       green: 0.8
						       blue: 0.8
						       alpha: 1.0]];

	    _radioLabel2.baselineAdjustment = UIBaselineAdjustmentAlignCenters;
	    _radioLabel2.layer.borderColor = [MFANTopSettings baseColor].CGColor;
	    _radioLabel2.layer.borderWidth = 2.0;
	    [_parent addSubview: _radioLabel2];
	}
    }
    else {
	_artTextVisible = NO;
	[self removeStars: YES];
    }
}

- (void)
starsPressed: (id) junk
{
    [self removeStars: YES];
}

- (void)remoteControlReceivedWithEvent:(UIEvent *)receivedEvent
{
    NSLog(@"- remotecontrolev = %d", (int) receivedEvent.type);
    if (receivedEvent.type == UIEventTypeRemoteControl) {
        switch (receivedEvent.subtype) {
            case UIEventSubtypeRemoteControlPlay:
            case UIEventSubtypeRemoteControlPause:
            case UIEventSubtypeRemoteControlTogglePlayPause:
                [self playPressed: nil withData: nil];
                break;
 
            case UIEventSubtypeRemoteControlPreviousTrack:
                [self prevPressed: nil withData:nil];
                break;
 
            case UIEventSubtypeRemoteControlNextTrack:
                [self nextPressed: nil withData: nil];
                break;
 
            default:
		NSLog(@"!RMT mystery pressed %d", (int) receivedEvent.subtype);
                break;
        }

	[[UIApplication sharedApplication] beginReceivingRemoteControlEvents];
    }
}

/*
// Only override drawRect: if you perform custom drawing.
// An empty implementation adversely affects performance during animation.
- (void)drawRect:(CGRect)rect
{
    // Drawing code
}
*/

@end
