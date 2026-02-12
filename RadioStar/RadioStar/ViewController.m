//
//  ViewController.m
//  RadioStar
//
//  Created by Michael Kazar on 11/25/25.
//

#import "ViewController.h"
#import "TopView.h"

@implementation ViewController {
    UIView *_origView;
    float _topMargin;
}

- (void)viewDidLoad {
    UIView *view;
    CGRect rect = self.view.frame;

    NSLog(@"In viewDidLoad");
    [super viewDidLoad];

    _topMargin = 25;

    _origView = view = [[TopView alloc] initWithFrame: rect ViewCont: self];
    self.view = view;

    // Do any additional setup after loading the view.
    self.view.backgroundColor = [UIColor colorWithRed: 0.80
						green:0.80
						 blue:0.80
						alpha:1.0];
}

- (void) setTopView: (UIView *) view {
    self.view = view;
}

- (void) restoreTopView {
    self.view = _origView;
}

@end
