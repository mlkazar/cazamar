//
//  ViewController.m
//  RadioStar
//
//  Created by Michael Kazar on 11/25/25.
//

#import "ViewController.h"
#import "GraphView.h"
#import "SignView.h"

@interface ViewController ()

@end

@implementation ViewController

- (UIView *) setupSignView {
    SignView *view;
    CGRect rect = self.view.frame;
    float hMargin = 15.0;

    rect.origin.y += hMargin;
    rect.size.height -= 2*hMargin;
    rect.size.height *= 0.92;
    view = [[SignView alloc] initWithFrame: rect ViewCont: self];

    return view;
}

- (UIView *) setupGraphView {
    GraphView *view;
    CGRect rect = self.view.frame;

    view = [[GraphView alloc] initWithFrame: rect ViewCont: self];

    return view;
}

- (void)viewDidLoad {
    UIView *view;

    NSLog(@"In viewDidLoad");
    [super viewDidLoad];
    // Do any additional setup after loading the view.
    self.view.backgroundColor = UIColor.yellowColor;

    view = [self setupSignView];
    self.view = view;
}


@end
