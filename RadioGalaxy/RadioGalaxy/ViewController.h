//
//  ViewController.h
//  RadioStar
//
//  Created by Michael Kazar on 11/25/25.
//

#import <UIKit/UIKit.h>

#import "AudioInt.h"
#import "TopViewInt.h"

@interface ViewController : UIViewController

@property float topMargin;
@property float bottomMargin;
@property UIColor *backgroundColor;
@property CGRect activeFrame;
@property NSObject *settings;

+ (void) splitLabel: (NSString *) label
	      group: (NSString **) group
	       song: (NSString **) song
	      album: (NSString **) album;

- (void) updateNowPlayingCenter: (NSString *) label
		      baseImage: (UIImage *) image
		    currentTime: (float) currentTime
		       duration: (float) durationTime
		      songIndex: (int32_t) songIndex;

- (void) pushTopView: (UIView<TopViewInt> *) view;

- (void) popTopView;

- (void) enterBackground;

- (void) leaveBackground;

- (bool) ok2Quit;

@end
