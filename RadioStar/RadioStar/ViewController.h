//
//  ViewController.h
//  RadioStar
//
//  Created by Michael Kazar on 11/25/25.
//

#import <UIKit/UIKit.h>

@protocol TopViewInt
- (void) activateTopView;

- (void) deactivateTopView;
@end

@interface ViewController : UIViewController

@property float topMargin;
@property float bottomMargin;
@property UIColor *backgroundColor;

- (void) pushTopView: (UIView<TopViewInt> *) view;

- (void) popTopView;

@end

