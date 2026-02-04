//
//  MFANTopMenu.m
//  DJ To Go
//
//  Created by Michael Kazar on 12/4/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANTopMenu.h"
#import "MFANCGUtil.h"
#import "MFANCoreButton.h"
#import "MFANTopSettings.h"
#import "MFANViewController.h"
#import "MFANWarn.h"
#import "MFANSetList.h"

@implementation MFANTopMenu {
    CGRect _appFrame;
    CGFloat _appVMargin;
    CGFloat _appHMargin;
    CGFloat _buttonWidth;
    CGFloat _buttonHMargin;
    CGFloat _buttonVMargin;
    CGFloat _buttonHeight;
    MFANCoreButton *_musicButton;
    MFANCoreButton *_podcastButton;
    MFANCoreButton *_radioButton;
    MFANCoreButton *_upnpButton;
    MFANCoreButton *_downloadsButton;
    MFANCoreButton *_helpButton;
    MFANCoreButton *_settingsButton;
    MFANCoreButton *_showHistoryButton;
    MFANCoreButton *_doneButton;
    MFANViewController *_viewCont;
}

- (void) activateTop
{
    return;
}

- (void) deactivateTop
{
    return;
}

- (id)initWithFrame:(CGRect)frame
     viewController: (MFANViewController *) viewCont
{
    CGRect buttonFrame;

    self = [super initWithFrame:frame];
    if (self) {
	_viewCont = viewCont;

	_appVMargin = 20.0;
	_appHMargin = 2.0;

	_appFrame = frame;
	_appFrame.origin.x += _appHMargin;
	_appFrame.origin.y += _appVMargin;
	_appFrame.size.width -= 2 * _appHMargin;
	_appFrame.size.height -= 2 * _appVMargin;

	[self setBackgroundColor: [UIColor whiteColor]];

	_buttonHeight = 30.0;
	_buttonWidth = _appFrame.size.width * 0.90;
	_buttonHMargin = _appFrame.size.width * 0.05;
	_buttonVMargin = 20;

	buttonFrame.origin.x = _buttonHMargin;
	buttonFrame.origin.y = _buttonVMargin + _buttonHeight;
	buttonFrame.size.width = _buttonWidth;
	buttonFrame.size.height = _buttonHeight;

	_musicButton = [[MFANCoreButton alloc] initWithFrame: buttonFrame
					       title: @"Border"
					       color: [MFANTopSettings baseColor]
					     backgroundColor:[MFANTopSettings clearBaseColor]];
	[_musicButton setClearText: @"+ Define Music Channel"];
	[_musicButton addCallback: self withAction: @selector(musicPressed:)];
	[self addSubview: _musicButton];
	buttonFrame.origin.y += _buttonVMargin + _buttonHeight;

	_radioButton = [[MFANCoreButton alloc] initWithFrame: buttonFrame
					       title: @"Border"
					       color: [MFANTopSettings baseColor]
					     backgroundColor: [MFANTopSettings clearBaseColor]];
	[_radioButton setClearText: @"+ Define Radio Channel"];
	[_radioButton addCallback: self withAction: @selector(radioPressed:)];
	[self addSubview: _radioButton];
	buttonFrame.origin.y += _buttonVMargin + _buttonHeight;

	_podcastButton = [[MFANCoreButton alloc] initWithFrame: buttonFrame
					       title: @"Border"
					       color: [MFANTopSettings baseColor]
					       backgroundColor: [MFANTopSettings clearBaseColor]];
	[_podcastButton setClearText: @"+ Define Podcast Channel"];
	[_podcastButton addCallback: self withAction: @selector(podcastPressed:)];
	[self addSubview: _podcastButton];
	buttonFrame.origin.y += _buttonVMargin + _buttonHeight;

	_upnpButton = [[MFANCoreButton alloc] initWithFrame: buttonFrame
					       title: @"Border"
					       color: [MFANTopSettings baseColor]
					    backgroundColor: [MFANTopSettings clearBaseColor]];
	[_upnpButton setClearText: @"Setup UPnP Servers"];
	[_upnpButton addCallback: self withAction: @selector(upnpPressed:)];
	[self addSubview: _upnpButton];
	buttonFrame.origin.y += _buttonVMargin + _buttonHeight;

	_downloadsButton = [[MFANCoreButton alloc] initWithFrame: buttonFrame
							   title: @"Border"
							   color: [MFANTopSettings baseColor]
						 backgroundColor: [MFANTopSettings clearBaseColor]]	;
	[_downloadsButton setClearText: @"Manage downloaded content"];
	[_downloadsButton addCallback: self withAction: @selector(downloadsPressed:)];
	[self addSubview: _downloadsButton];
	buttonFrame.origin.y += _buttonVMargin + _buttonHeight;

	_helpButton = [[MFANCoreButton alloc] initWithFrame: buttonFrame
						      title: @"Border"
						      color: [MFANTopSettings baseColor]
					    backgroundColor: [MFANTopSettings clearBaseColor]];
	[_helpButton setClearText: @"Help pages"];
	[_helpButton addCallback: self withAction: @selector(helpPressed:)];
	[self addSubview: _helpButton];
	buttonFrame.origin.y += _buttonVMargin + _buttonHeight;

	_settingsButton = [[MFANCoreButton alloc] initWithFrame: buttonFrame
						  title: @"Border"
						  color: [MFANTopSettings baseColor]
						backgroundColor: [MFANTopSettings clearBaseColor]];
	[_settingsButton setClearText: @"Settings"];
	[_settingsButton addCallback: self withAction: @selector(settingsPressed:)];
	[self addSubview: _settingsButton];
	buttonFrame.origin.y += _buttonVMargin + _buttonHeight;

	_showHistoryButton = [[MFANCoreButton alloc] initWithFrame: buttonFrame
						     title: @"Border"
						     color: [MFANTopSettings baseColor]
						   backgroundColor: [MFANTopSettings clearBaseColor	]];
	[_showHistoryButton setClearText: @"Show History"];
	[_showHistoryButton addCallback: self withAction: @selector(showHistoryPressed:)];
	[self addSubview: _showHistoryButton];
	buttonFrame.origin.y += _buttonVMargin + _buttonHeight;

	_doneButton = [[MFANCoreButton alloc] initWithFrame: buttonFrame
					       title: @"Border"
					       color: [MFANTopSettings baseColor]
					    backgroundColor: [MFANTopSettings clearBaseColor]];
	[_doneButton setClearText: @"Done"];
	[_doneButton addCallback: self withAction: @selector(donePressed:)];
	[self addSubview: _doneButton];
	buttonFrame.origin.y += _buttonVMargin + _buttonHeight;

    }

    return self;
}

- (void) musicPressed: (id) sender
{
    MFANTopEdit *edit;

    [_viewCont setChannelType: MFANChannelMusic];
    edit = (MFANTopEdit *) [_viewCont getTopAppByName: @"edit"];
    [edit setScanFilter: MFANTopEdit_keepMusic];
    [_viewCont switchToAppByName: @"edit"];
}

- (void) podcastPressed: (id) sender
{
    MFANTopEdit *edit;

    [_viewCont setChannelType: MFANChannelPodcast];
    edit = (MFANTopEdit *) [_viewCont getTopAppByName: @"edit"];
    [edit setScanFilter: MFANTopEdit_keepPodcast];
    [_viewCont switchToAppByName: @"edit"];
}

- (void) radioPressed: (id) sender
{
    MFANTopEdit *edit;

    [_viewCont setChannelType: MFANChannelRadio];
    edit = (MFANTopEdit *) [_viewCont getTopAppByName: @"edit"];
    [edit setScanFilter: MFANTopEdit_keepRadio];
    [_viewCont switchToAppByName: @"edit"];
}

- (void) editDone: (id) junk
{
    [_viewCont switchToAppByName: @"list"];
}

- (void) upnpPressed: (id) sender
{
    [_viewCont switchToAppByName: @"upnp"];
}

- (void) downloadsPressed: (id) sender
{
    [_viewCont switchToAppByName: @"doc"];
}

- (void) showHistoryPressed: (id) sender
{
    [_viewCont switchToAppByName: @"history"];
}

- (void) helpPressed: (id) sender
{
    [_viewCont switchToAppByName: @"help"];
}

- (void) settingsPressed: (id) sender
{
    [_viewCont switchToAppByName: @"settings"];
}

- (void) donePressed: (id) sender
{
    [_viewCont restorePlayer];
}

//- (void)drawRect:(CGRect)rect
//{
//    drawBackground(rect);
//}

@end
