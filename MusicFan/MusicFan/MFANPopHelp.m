//
//  MFANPopHelp.m
//  DJ To Go
//
//  Created by Michael Kazar on 2/25/15.
//  Copyright (c) 2015 Mike Kazar. All rights reserved.
//

#import "MFANPopHelp.h"
#import "MFANIconButton.h"
#import "MFANTopSettings.h"

@implementation MFANPopHelp {
    MFANViewController *_viewCon;
    CGRect _appFrame;
    CGFloat _appVMargin;
    CGFloat _appHMargin;
    CGFloat _appWidth;
    CGFloat _appHeight;
    CGFloat _buttonWidth;
    NSString *_helpFileName;

    CGRect _webFrame;
    CGRect _doneFrame;
    CGRect _noMoreFrame;

    MFANIconButton *_doneButton;
    MFANIconButton *_noMoreButton;
    UIWebView *_webView;
    UIView __weak *_parentView;

    int _warningFlag;
}

- (MFANPopHelp *) initWithFrame: (CGRect) frame
		       helpFile: (NSString *) helpFileName
		     parentView: (UIView *) parentView
		    warningFlag: (int) warningFlag
{
    NSURL *bundleUrl;
    NSString *helpPath;
    NSURLRequest *urlRequest;

    self = [super initWithFrame:frame];
    if (self) {
        // Initialization code
	_helpFileName = helpFileName;
	_parentView = parentView;
	_warningFlag = warningFlag;

	_appVMargin = frame.size.height/12;
	_appHMargin = 0;

	/* _appFrame is the frame, relative to our own frame, within
	 * which we want to position the help data's web frame, and
	 * the done button.
	 */
	_appFrame = frame;
	_appFrame.origin.x = _appHMargin;
	_appFrame.origin.y = _appVMargin;
	_appWidth = _appFrame.size.width -= 2 * _appHMargin;
	_appHeight = _appFrame.size.height -= 2 * _appVMargin;

	_buttonWidth = _appWidth/2.75;

	_doneFrame.origin.x = (_appFrame.origin.x + 0.50 * _appFrame.size.width -
			       _buttonWidth/2);
	_doneFrame.size.width = _buttonWidth;
	_doneFrame.origin.y = _appFrame.origin.y + 0.92 * _appFrame.size.height;
	_doneFrame.size.height = (1.0 - 0.92) * _appFrame.size.height;
	_doneButton = [[MFANIconButton alloc] initWithFrame: _doneFrame
					      title: @"Done"
					      color: [UIColor greenColor]
					      file: @"icon-done.png"];
	[self addSubview: _doneButton];
	if (warningFlag != 0)
	    [_doneButton addCallback: self withAction:@selector(noMorePressed:)];
	else
	    [_doneButton addCallback: self withAction:@selector(donePressed:)];

	_webFrame.origin.x = _appFrame.origin.x;
	_webFrame.origin.y = _appFrame.origin.y;
	_webFrame.size.width = _appFrame.size.width;
	_webFrame.size.height = 0.80*_appFrame.size.height;

	_webView = [[UIWebView alloc] initWithFrame:_webFrame];
	_webView.layer.borderColor = [MFANTopSettings baseColor].CGColor;
	_webView.layer.borderWidth = 2.0;
	[self addSubview: _webView];

	bundleUrl = [[NSBundle mainBundle] bundleURL];
	helpPath = [[NSBundle mainBundle] pathForResource: _helpFileName ofType:@"html"];
	urlRequest = [NSURLRequest requestWithURL: [NSURL fileURLWithPath: helpPath]];

	[_webView loadRequest: urlRequest];

	[self setBackgroundColor: [UIColor colorWithHue:0.0
					   saturation: 0.0
					   brightness: 0.0
					   alpha: 0.6]];
    }
    return self;
}

- (BOOL) shouldShow
{
    return !([MFANTopSettings warningFlags] & _warningFlag);
}

- (void) checkShow
{
    if ([self shouldShow]) {
	self.layer.zPosition = 2;
	[_parentView addSubview: self];
    }
}

- (void) show
{
    self.layer.zPosition = 2;
    [_parentView addSubview: self];
}

- (void) donePressed: (id) junk
{
    [self removeFromSuperview];
}

- (void) noMorePressed: (id) junk
{
    [MFANTopSettings setWarningFlag: _warningFlag];
    [self removeFromSuperview];
}

/*
// Only override drawRect: if you perform custom drawing.
// An empty implementation adversely affects performance during animation.
- (void)drawRect:(CGRect)rect {
    // Drawing code
}
*/

@end
