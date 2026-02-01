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

    view = [[SignView alloc] initWithFrame: rect ViewCont: self];

    return view;
}

// We have to do this separately from view creation because scene's makeKeyAndVisible
// sets the frame to the window's.
- (void) adjustViewFrame {
    CGRect rect = self.view.frame;
    float vMargin = 20.0;

    rect.origin.y += vMargin;
    rect.size.height -= 2*vMargin;
    rect.size.height *= 0.92;

    [self.view setFrame: rect];
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
