//
//  MFANPopStatus.m
//  DJ To Go
//
//  Created by Michael Kazar on 3/15/2016
//  Copyright (c) 2016 Mike Kazar. All rights reserved.
//

#import "MFANPopStatus.h"
#import "MFANIconButton.h"
#import "MFANTopSettings.h"

@implementation MFANPopStatus {
    MFANViewController __weak *_viewCon;
    CGRect _appFrame;
    CGFloat _appVMargin;
    CGFloat _appHMargin;
    CGFloat _appWidth;
    CGFloat _appHeight;
    CGFloat _buttonWidth;
    BOOL _canceled;
    NSString *_msg;

    CGRect _msgFrame;
    UILabel *_msgLabel;
    CGRect _doneFrame;

    MFANIconButton *_doneButton;
    UIView __weak *_parentView;
}

- (MFANPopStatus *) initWithFrame: (CGRect) frame
			      msg: (NSString *) msg
		       parentView: (UIView *) parentView
{
    self = [super initWithFrame:frame];
    if (self) {
        // Initialization code
	_msg = msg;
	_parentView = parentView;
	_canceled = NO;

	_appVMargin = frame.size.height/12;
	_appHMargin = 0;

	_appFrame = frame;
	_appFrame.origin.x += _appHMargin;
	_appFrame.origin.y += _appVMargin;
	_appWidth = _appFrame.size.width -= 2 * _appHMargin;
	_appHeight = _appFrame.size.height -= 2 * _appVMargin;

	_buttonWidth = _appWidth/2.75;

	_doneFrame.origin.x = (_appFrame.origin.x + 0.50 * _appFrame.size.width -
			       _buttonWidth/2);
	_doneFrame.size.width = _buttonWidth;
	_doneFrame.origin.y = _appFrame.origin.y + 0.92 * _appFrame.size.height;
	_doneFrame.size.height = (1.0 - 0.92) * _appFrame.size.height;
	_doneButton = [[MFANIconButton alloc] initWithFrame: _doneFrame
					      title: @"Cancel"
					      color: [UIColor redColor]
					      file: @"icon-cancel.png"];
	[self addSubview: _doneButton];
	[_doneButton addCallback: self withAction:@selector(donePressed:)];

	_msgFrame.origin.x = _appFrame.origin.x;
	_msgFrame.origin.y = _appFrame.origin.y;
	_msgFrame.size.width = _appFrame.size.width;
	_msgFrame.size.height = 0.80*_appFrame.size.height;

	_msgLabel = [[UILabel alloc] initWithFrame:_msgFrame];
	_msgLabel.backgroundColor = [UIColor whiteColor];
	_msgLabel.textAlignment = NSTextAlignmentCenter;
	_msgLabel.layer.borderColor = [MFANTopSettings baseColor].CGColor;
	_msgLabel.layer.borderWidth = 2.0;
	_msgLabel.text = _msg;
	[self addSubview: _msgLabel];

	[self setBackgroundColor: [UIColor colorWithHue:0.0
					   saturation: 0.0
					   brightness: 0.0
					   alpha: 0.6]];
    }
    return self;
}

- (void) show
{
    self.layer.zPosition = 2;
    [_parentView addSubview: self];
}

- (void) donePressed: (id) junk
{
    _canceled = YES;
    [self removeFromSuperview];
}

- (void) stop
{
    _canceled = YES;
    [self removeFromSuperview];
}

- (BOOL) canceled
{
    return _canceled;
}

- (void) updateMsg: (NSString *) msg
{
    _msg = msg;
    _msgLabel.text = msg;
}

/*
// Only override drawRect: if you perform custom drawing.
// An empty implementation adversely affects performance during animation.
- (void)drawRect:(CGRect)rect {
    // Drawing code
}
*/

@end
