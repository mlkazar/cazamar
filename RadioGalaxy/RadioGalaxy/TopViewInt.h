//
//  ViewController.h
//  RadioStar
//
//  Created by Michael Kazar on 04/22/2026
//

@protocol TopViewInt
- (void) tvActivate;

- (void) tvDeactivate;

@optional
- (bool) tvOk2Quit;

// used by play/pause button
- (bool) tvPlayPauseSong;

- (bool) tvNextSong;

- (bool) tvPrevSong;

- (bool) tvIsPlaying;

// these two are used for audio interruptions
- (bool) tvPause;

- (bool) tvResume;
@end
