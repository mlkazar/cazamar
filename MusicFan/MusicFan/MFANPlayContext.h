//
//  MFANPlayContext.h
//  MusicFan
//
//  Created by Michael Kazar on 4/26/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <MediaPlayer/MediaPlayer.h>
#import <Foundation/NSDate.h>

@class MFANPlayerView;
@class MFANSetList;
@class MFANRButton;
@class MFANTopLevel;
@class MFANIndicator;
@class MFANDownload;
@class MFANMediaItem;

@interface MFANPlayContext : NSObject
- (MFANPlayContext *) initWithButton: (MFANRButton *) button
		      withPlayerView: (MFANPlayerView *) playerView
			    withView: (UIView *) parentView
			    withFile: (NSString *) assocFile;

- (void) setCallback: (MFANTopLevel *) callback withAction: (SEL) action;

- (void) play;

- (void) pause;

- (MFANDownload *) download;

- (MFANSetList *) setList;

- (void) setSelected: (BOOL) isSel;

- (BOOL) selected;

- (long) getCurrentIndex;

- (void) setIndex: (int) ix;

- (void) restoreListFromFile;

- (BOOL) hasAnyItems;

- (BOOL) hasPodcastItems;

- (void) saveListToFile;

- (NSTimeInterval) lastSavedTime;

- (void) setQueryInfo: (NSMutableArray *) queryInfo;

- (NSMutableArray *) queryInfo;

- (uint64_t) mtime;

/* instantaneous from the player */
- (float) currentPlaybackTime;

@property NSString *associatedFile;

@property (readonly) NSNumber *data;

@property NSTimeInterval currentTime;

@property long currentIndex;

@end
