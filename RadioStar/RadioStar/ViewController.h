//
//  ViewController.h
//  RadioStar
//
//  Created by Michael Kazar on 11/25/25.
//

#import <UIKit/UIKit.h>

#import "TopViewInt.h"

@interface ViewController : UIViewController

@property float topMargin;
@property float bottomMargin;
@property UIColor *backgroundColor;
@property CGRect activeFrame;

- (void) pushTopView: (UIView<TopViewInt> *) view;

- (void) popTopView;

@end

