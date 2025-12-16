//
//  ViewController.m
//  RadioStar
//
//  Created by Michael Kazar on 11/25/25.
//

#import "ViewController.h"
#import "GraphTest.h"

@interface ViewController ()

@end

@implementation ViewController

- (void)viewDidLoad {
    GraphTest *graph;
    CGRect rect = self.view.frame;

    NSLog(@"In viewDidLoad");
    [super viewDidLoad];
    // Do any additional setup after loading the view.
    self.view.backgroundColor = UIColor.yellowColor;

    graph = [[GraphTest alloc] initWithFrame: rect ViewCont: self];

    self.view = graph;
}


@end
