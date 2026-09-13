#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import "ViewController.h"

@interface SongPlayer : UIView<TopViewInt, UITableViewDelegate, UITableViewDataSource,
				     AVAudioPlayerDelegate>

- (SongPlayer *) initWithStation: (SignStation *) station
			  buffer: (MFANAqStreamBuffer *) buffer
			signView: (SignView *) signView
			viewCont: (ViewController *) vc;

- (void) tvActivate;

- (void) tvDeactivate;

@end
