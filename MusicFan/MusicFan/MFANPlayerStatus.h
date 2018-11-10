//
//  MFANPlayerStatus.h
//  MusicFan
//
//  Created by Michael Kazar on 5/21/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <Foundation/Foundation.h>

@interface MFANPlayerStatus : NSObject

- (MFANPlayerStatus *) initObject: (id) obj sel: (SEL) selector;

/* check the current state */
- (MPMusicPlaybackState) state;

- (MPMusicPlaybackState) stateWithForce: (BOOL) force;

- (float) pbtGetTime;

- (float) pbtRecentTime: (float *) errorp;

- (void) setStateHint: (MPMusicPlaybackState) newState;

- (void) pbtSetTime: (NSTimeInterval) newTime;

- (void) pbtInit: (id) junk;

- (void) isPlayingChanged: (id) notification;

- (void) disable;

- (void) enable;

@end
