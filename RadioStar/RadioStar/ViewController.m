//
//  ViewController.m
//  RadioStar
//
//  Created by Michael Kazar on 11/25/25.
//

#import "ViewController.h"
#import "GraphView.h"

@interface ViewController ()

@end

@implementation ViewController

- (void)viewDidLoad {
    GraphView *graph;
    CGRect rect = self.view.frame;

    NSLog(@"In viewDidLoad");
    [super viewDidLoad];
    // Do any additional setup after loading the view.
    self.view.backgroundColor = UIColor.yellowColor;

    graph = [[GraphView alloc] initWithFrame: rect ViewCont: self];

    self.view = graph;
}


@end
