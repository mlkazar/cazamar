//
//  ViewController.m
//  RadioStar
//
//  Created by Michael Kazar on 11/25/25.
//

#import "ViewController.h"
#import "TopView.h"

@implementation ViewController {
    NSMutableArray *_oldViews;	// of UIView objects
    float _topMargin;
    UIColor *_backgroundColor;
}

- (void)viewDidLoad {
    UIView *view;
    CGRect rect = self.view.frame;

    NSLog(@"In viewDidLoad");
    [super viewDidLoad];

    _oldViews = [[NSMutableArray alloc] init];

    _topMargin = 50;

    self.view = [[TopView alloc] initWithFrame: rect ViewCont: self];

    _backgroundColor = [UIColor colorWithRed: 0.80
				       green:0.80
					blue:0.80
				       alpha:1.0];

    // Do any additional setup after loading the view.
    self.view.backgroundColor = _backgroundColor;
}

- (void) pushTopView: (UIView *) view {
    [_oldViews addObject: self.view];
    self.view = view;
    view.backgroundColor = _backgroundColor;
}

- (void) popTopView {
    self.view = [_oldViews lastObject];
    [_oldViews removeLastObject];
}

@end
