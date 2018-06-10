//
//  MFANTopHelp.m
//  MusicFan
//
//  Created by Michael Kazar on 6/11/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANTopHelp.h"
#import "MFANIconButton.h"
#import "MFANViewController.h"
#import "MFANCGUtil.h"
#import "MFANTopSettings.h"

@implementation MFANTopHelp {
    MFANViewController *_viewCon;
    CGRect _appFrame;
    CGFloat _appVMargin;
    CGFloat _appHMargin;
    CGFloat _appWidth;
    CGFloat _appHeight;
    CGFloat _buttonWidth;
    CGFloat _buttonHeight;

    CGRect _webFrame;
    CGRect _doneFrame;
    CGRect _backFrame;

    MFANIconButton *_doneButton;
    MFANIconButton *_backButton;
    UIWebView *_webView;
}

- (MFANTopHelp *) initWithFrame: (CGRect) frame viewController: (MFANViewController *) viewCon;
{
    NSURL *bundleUrl;
    NSString *helpPath;
    NSURLRequest *urlRequest;
    int nbuttons = 2;
    CGFloat buttonMargin = 2;
    CGFloat buttonGap;
    CGFloat nextButtonX;
    CGFloat nextButtonY;

    self = [super initWithFrame:frame];
    if (self) {
        // Initialization code
	_viewCon = viewCon;

	_appVMargin = 20.0;
	_appHMargin = 2.0;

	_appFrame = frame;
	_appFrame.origin.x += _appHMargin;
	_appFrame.origin.y += _appVMargin;
	_appWidth = _appFrame.size.width -= 2 * _appHMargin;
	_appHeight = _appFrame.size.height -= _appVMargin;

	_buttonWidth = 64;
	_buttonHeight = _buttonWidth;

	buttonGap = _appFrame.size.width / (nbuttons+1);
	nextButtonX = _appFrame.origin.x + buttonGap - _buttonWidth/2;
	nextButtonY = _appFrame.origin.y + _appFrame.size.height - _buttonHeight;

	_backFrame.origin.x = nextButtonX;
	_backFrame.size.width = _buttonWidth;
	_backFrame.origin.y = nextButtonY;
	_backFrame.size.height = _buttonHeight;
	_backButton = [[MFANIconButton alloc] initWithFrame: _backFrame
					      title: @"Back"
					      color: [UIColor colorWithRed: 0.4
							      green: 0.4
							      blue: 1.0
							      alpha: 1.0]
					      file: @"icon-back.png"];
	[self addSubview: _backButton];
	[_backButton addCallback: self withAction:@selector(backPressed:)];
	nextButtonX += buttonGap;

	_doneFrame.origin.x = nextButtonX;
	_doneFrame.size.width = _buttonWidth;
	_doneFrame.origin.y = nextButtonY;
	_doneFrame.size.height = _buttonHeight;
	_doneButton = [[MFANIconButton alloc] initWithFrame: _doneFrame
					      title: @"Done"
					      color: [UIColor colorWithHue: 0.3
							      saturation: 1.0
							      brightness: 1.0
							      alpha: 1.0]
					      file: @"icon-done.png"];
	[self addSubview: _doneButton];
	[_doneButton addCallback: self withAction:@selector(donePressed:)];

	_webFrame.origin.x = _appFrame.origin.x;
	_webFrame.origin.y = _appFrame.origin.y;
	_webFrame.size.width = _appFrame.size.width;
	_webFrame.size.height = _appFrame.size.height - _buttonHeight - buttonMargin;

	_webView = [[UIWebView alloc] initWithFrame:_webFrame];
	_webView.layer.borderColor = [MFANTopSettings baseColor].CGColor;
	_webView.layer.borderWidth = 2.0;
	[self addSubview: _webView];

	bundleUrl = [[NSBundle mainBundle] bundleURL];
	helpPath = [[NSBundle mainBundle] pathForResource: @"help" ofType:@"html"];
	urlRequest = [NSURLRequest requestWithURL: [NSURL fileURLWithPath: helpPath]];

	[_webView loadRequest: urlRequest];

	[self setBackgroundColor: [UIColor clearColor]];
    }
    return self;
}

- (void) donePressed: (id) junk
{
    [_viewCon switchToAppByName: @"main"];
}

- (void) backPressed: (id) junk
{
    [_webView goBack];
}

-(void) activateTop
{
    return;
}

-(void) deactivateTop
{
    return;
}

// Only override drawRect: if you perform custom drawing.
// An empty implementation adversely affects performance during animation.
- (void)drawRect:(CGRect)rect
{
    drawBackground(rect);
}

@end
