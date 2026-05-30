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

- (void) pushTopView: (UIView<TopViewInt> *) view;

- (void) popTopView;

- (void) setRemoteReceiver: (UIView<AudioInt> *) remoteReceiver;

- (void) enterBackground;

- (void) leaveBackground;

@end
