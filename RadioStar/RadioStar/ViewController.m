//
//  ViewController.m
//  RadioStar
//
//  Created by Michael Kazar on 11/25/25.
//

#import "ViewController.h"
#import "TopView.h"

@interface ViewController ()

@end

@implementation ViewController

- (void)viewDidLoad {
    UIView *view;
    CGRect rect = self.view.frame;

    NSLog(@"In viewDidLoad");
    [super viewDidLoad];

    view = [[TopView alloc] initWithFrame: rect ViewCont: self];
    self.view = view;

    // Do any additional setup after loading the view.
    self.view.backgroundColor = [UIColor colorWithRed: 0.80
						green:0.80
						 blue:0.80
						alpha:1.0];
}

@end
