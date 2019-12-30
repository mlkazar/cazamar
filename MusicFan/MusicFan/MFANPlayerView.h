//
//  MFANPlayerView.h
//  MusicFan
//
//  Created by Michael Kazar on 4/26/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>
#import "MFANComm.h"

@class MFANTopLevel;
@class MFANPlayContext;
@class MFANTopHistory;
@class MFANViewController;

@interface MFANMediaItem : NSObject

@property BOOL mustDownload;	/* true if you must download remote before playing */
@property long podcastDate;
@property NSString *urlRemote;
@property NSString *urlArtwork;
@property NSString *urlTitle;
@property NSString *details;
@property float playbackTime;
@property NSString *urlAlbumTitle;
@property NSString *url;

- (MFANMediaItem *) initWithUrl: (NSString *) url
			  title: (NSString *) title
		     albumTitle: (NSString *) albumTitle;

- (MFANMediaItem *) initWithMediaItem: (MPMediaItem *) item;

- (BOOL) useAvPlayer;

- (BOOL) trackPlaybackTime;

- (BOOL) useMainPlayer;

- (MPMediaItem *) item;

- (BOOL) isWebStream;

- (BOOL) isPodcast;

- (NSString *) title;

- (NSString *) albumTitle;

- (UIImage *) artworkWithSize: (float) size;

- (NSString *) artist;

- (NSString *) effectiveUrl;

- (NSString *) localUrl;

- (void) setUrl: (NSString *) url;

- (BOOL) isPlayable;

@end

@interface MFANPlayerView : UIView

- (MFANPlayerView *) initWithParent: (MFANTopLevel *) parent
			  topMargin: (CGFloat) topMargin
			       comm: (MFANComm *) comm
			    viewCon: (MFANViewController *)viewCon
			   andFrame: (CGRect) frame;

- (void) setQueueWithContext: (MFANPlayContext *) playContext;

- (void) setPlaybackTime: (float) newTime;

- (long) currentIndex;

- (MFANMediaItem *) currentMFANItem;

- (float) currentPlaybackTime;

- (void) setIndex: (long) ix rollForward: (BOOL) forward;

- (void) play;

- (void) pause;

- (float) currentPlaybackDuration: (BOOL *) validp;

- (BOOL) isPlayingWithForce: (BOOL) force;

- (void)remoteControlReceivedWithEvent:(UIEvent *)receivedEvent;

- (CGRect) rightMarginFrame;

- (BOOL) hijacked;

- (void) resumeAfterHijacked: (id) junk;

- (void) stopRecording;

- (int) startRecordingFor: (id) who sel: (SEL) selector;

- (void) setHistory: (MFANTopHistory *) hist;

@end
