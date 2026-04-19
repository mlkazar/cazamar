//
//  ViewController.m
//  RadioStar
//
//  Created by Michael Kazar on 11/25/25.
//

#import "ViewController.h"
#import "TopView.h"

@implementation ViewController {
    NSMutableArray<UIView<TopViewInt> *> *_oldViews;	// of UIView objects
    float _topMargin;
    float _bottomMargin;
    UIColor *_backgroundColor;
    CGRect _activeFrame;
}

- (void)viewDidLoad {
    CGRect rect = self.view.frame;

    NSLog(@"In viewDidLoad");
    [super viewDidLoad];

    _oldViews = [[NSMutableArray alloc] init];

    _topMargin = 50;
    _bottomMargin = 50;

    self.view = [[TopView alloc] initWithFrame: rect ViewCont: self];

    _activeFrame = rect;
    _activeFrame.origin.y += _topMargin;
    _activeFrame.size.height -= _topMargin + _bottomMargin;

#if 0
    // Do any additional setup after loading the view.
    _backgroundColor = [UIColor blackColor];
    self.view.backgroundColor = _backgroundColor;
#endif
}

- (void) pushTopView: (UIView<TopViewInt> *) view {
    UIView<TopViewInt> *oldView;

    oldView = [_oldViews lastObject];
    if (oldView != nil)
	[oldView deactivateTopView];

    [_oldViews addObject: self.view];

    [view activateTopView];

    self.view = view;
    view.backgroundColor = _backgroundColor;
}

- (void) popTopView {
    UIView<TopViewInt> *newActiveView;
    UIView<TopViewInt> *oldView;

    self.view = oldView = [_oldViews lastObject];
    [_oldViews removeLastObject];
    newActiveView = [_oldViews lastObject];

    if (oldView != nil)
	[oldView activateTopView];
}

@end
