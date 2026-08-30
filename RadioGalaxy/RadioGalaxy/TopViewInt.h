//
//  ViewController.h
//  RadioStar
//
//  Created by Michael Kazar on 04/22/2026
//

@protocol TopViewInt
- (void) activateTopView;

- (void) deactivateTopView;

@optional
- (bool) ok2Quit;

- (bool) playPauseSong;

- (bool) nextSong;

- (bool) prevSong;
@end
