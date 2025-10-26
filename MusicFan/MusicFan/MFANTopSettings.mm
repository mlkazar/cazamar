//
//  MFANTopSettings.m
//  MusicFan
//
//  Created by Michael Kazar on 6/11/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANTopSettings.h"
#import "MFANMetalButton.h"
#import "MFANViewController.h"
#import "MFANSetList.h"
#import "MFANCGUtil.h"
#import "MFANIconButton.h"
#import "MFANDownload.h"
#import "MFANWarn.h"
#import "MFANTopRStar.h"
#import "MFANTopHistory.h"
#import "MFANFileWriter.h"

#include "xgml.h"
#include "upnp.h"
#include <string>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/stat.h>

/* MFANLabels are actually UIButtons, so you can use them as such for
 * adjust appearances.
 */
@implementation MFANLabel {
    id _target;
    SEL _sel;
}

- (MFANLabel *) initWithFrame: (CGRect) frame target: (id) target selector:(SEL) selector
{
    self = [super initWithFrame: frame];
    if (self) {
	_target = target;
	_sel = selector;

	[self addTarget: self
	      action: @selector(buttonPressed:withEvent:)
	      forControlEvents: UIControlEventAllTouchEvents];
    }

    return self;
}

- (MFANLabel *) initWithTarget: (id) target selector:(SEL) selector
{
    self = [super init];
    if (self) {
	_target = target;
	_sel = selector;

	[self addTarget: self
	      action: @selector(buttonPressed:withEvent:)
	      forControlEvents: UIControlEventAllTouchEvents];
    }

    return self;
}

- (void) buttonPressed: (id) button withEvent: (UIEvent *) event
{
    NSSet *allTouches;
    NSEnumerator *en;
    UITouch *touch;
    UITouchPhase phase;

    if (![event respondsToSelector:@selector(allTouches)]) {
	/* BOGUS event */
	return;
    }

    allTouches = [event allTouches];
    en = [allTouches objectEnumerator];
    phase = UITouchPhaseBegan;
    while( touch = [en nextObject]) {
	phase = [touch phase];
    }

    if (phase == UITouchPhaseBegan) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
	[_target performSelector: _sel withObject: nil];
#pragma clang diagnostic pop
    }
}
@end

@implementation MFANTopSettings {
    MFANViewController *_viewCon;
    CGRect _appFrame;
    CGFloat _appVMargin;
    CGFloat _appHMargin;
    CGFloat _appWidth;
    CGFloat _appHeight;
    CGFloat _buttonWidth;
    NSString *_associatedFile;
    MFANLabel *_label1;
    MFANMetalButton *_button1;
    MFANLabel *_label2;
    MFANMetalButton *_button2;
    MFANLabel *_label3;
    MFANMetalButton *_button3;
    MFANLabel *_label4;
    MFANMetalButton *_button4;
    MFANLabel *_label5;
    MFANMetalButton *_button5;
    MFANLabel *_label6;
    MFANMetalButton *_button6;
    MFANLabel *_label7;
    MFANMetalButton *_button7;
    MFANLabel *_label8;
    MFANMetalButton *_button8;
    MFANLabel *_label9;
    MFANMetalButton *_button9;
    MFANLabel *_label10;
    MFANMetalButton *_button10;
    MFANLabel *_label11;
    MFANMetalButton *_button11;
    MFANLabel *_label12;
    MFANMetalButton *_button12;
    MFANLabel *_label13;
    MFANMetalButton *_button13;
    MFANLabel *_label14;
    MFANMetalButton *_button14;
    MFANLabel *_label15;
    MFANMetalButton *_button15;

    CGRect _doneFrame;
    CGRect _cancelFrame;

    MFANIconButton *_doneButton;
    MFANIconButton *_cancelButton;
    UIWebView *_webView;
    int _useCloudDefault;
    int _sendUsage;
    int _autoDim;
    int _useCellForDownloads;
    int _autoDownload;
    int _neverUseCellData;
    int _warningFlags;
    int _unloadPlayed;
    unsigned int _firstUsed;
    unsigned int _licensed;
    UIColor *_baseColor;
    UIColor *_lightBaseColor;
    UIColor *_clearBaseColor;
    UIColor *_textColor;
    UIColor *_selectedBackgroundColor;
}

MFANTopSettings *_globalSettings;

- (MFANTopSettings *) initWithFrame: (CGRect) frame viewController: (MFANViewController *) viewCon;
{
    CGRect labelFrame;
    CGRect buttonFrame;
    UILabel *tlabel;
    int buttonHeight;
    float fontSizeScale;
    float buttonPct = 0.10;
    BOOL doSave = NO;

    self = [super initWithFrame:frame];
    if (self) {
        // Initialization code
	_globalSettings = self;
	_associatedFile = [self settingsName];
	_firstUsed = 0;
	_licensed = 0;

	[self reloadSettings];

	/* persistently record the first time through here */
	if (_firstUsed == 0) {
	    _firstUsed = osp_get_sec();
	    doSave = YES;
	}
	if (_licensed == 0) {
	    _licensed = 1;
	    doSave = YES;
	}
	if (doSave)
	    [self saveSettings];

	/* do this early so we can use it during initialization */
	_baseColor = [UIColor colorWithHue: 0.62
			      saturation: 1.0
			      brightness: 1.0
			      alpha: 1.0];
	_lightBaseColor = [UIColor colorWithHue: 0.54
				   saturation: 0.5
				   brightness: 1.0
				   alpha: 1.0];
	_clearBaseColor = [UIColor colorWithHue: 0.54
				   saturation: 0.5
				   brightness: 1.0
				   alpha: 0.2];

	fontSizeScale = 0.6;

	_textColor = [UIColor blackColor];
	_selectedBackgroundColor = [UIColor colorWithRed: 0.6
						   green: 0.8
						    blue:1.0
						   alpha: 1.0];

	_viewCon = viewCon;

	_appVMargin = 20.0;
	_appHMargin = 2.0;

	_appFrame = frame;
	_appFrame.origin.x += _appHMargin;
	_appFrame.origin.y += _appVMargin;
	_appWidth = _appFrame.size.width -= 2 * _appHMargin;
	_appHeight = _appFrame.size.height -= 2 * _appVMargin;

	/* divide the available space by one more than the # of buttons; note
	 * that we also multiply buttonHeight by 1.3 for a gap, so take that into
	 * account as well.
	 */
	buttonHeight = ((1.0-buttonPct) * _appFrame.size.height) / 1.3 / 12;

	labelFrame = _appFrame;
	labelFrame.size.height = buttonHeight;
	labelFrame.size.width = _appWidth * 0.78;

	/* ================================================================ */

	/* setup next settings button */
	buttonFrame = labelFrame;
	buttonFrame.origin.x += _appWidth * 0.80;
	buttonFrame.size.width = _appWidth * 0.20;
	_label3 = [[MFANLabel alloc] initWithFrame: labelFrame
				     target: self
				     selector: @selector(helpResyncLibrary:withData:)];
	tlabel = [_label3 titleLabel];
	_label3.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
	[_label3 setTitle: @"Force Library Resync" forState: UIControlStateNormal];
	[_label3 setTitleColor: [MFANTopSettings textColor] forState: UIControlStateNormal];
	[_label3 setSelected: YES];
	[tlabel setFont: [MFANTopSettings basicFontWithSize:
					      labelFrame.size.height * fontSizeScale]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];
	[self addSubview: _label3];
	_button3 = [[MFANMetalButton alloc] initWithFrame: buttonFrame
					    title: nil
					    color: [UIColor blueColor]
					    fontSize: 0];
	[_button3 addCallback: self
		  withAction: @selector(resyncLibrary:)
		  value: nil];
	[self addSubview: _button3];
	labelFrame.origin.y += labelFrame.size.height*1.3;
	[_button3 setSelected: YES];

	/* cloud default button */
	buttonFrame = labelFrame;
	buttonFrame.origin.x += _appWidth * 0.80;
	buttonFrame.size.width = _appWidth * 0.20;
	_label4 = [[MFANLabel alloc] initWithFrame: labelFrame
				     target: self
				     selector: @selector(helpUseCloudDefault:withData:)];
	tlabel = [_label4 titleLabel];
	_label4.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
	[_label4 setTitle: @"Use cloud (default)" forState: UIControlStateNormal];
	[_label4 setTitleColor: [MFANTopSettings textColor] forState: UIControlStateNormal];
	[tlabel setFont: [MFANTopSettings basicFontWithSize:
					      labelFrame.size.height * fontSizeScale]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];
	[self addSubview: _label4];
	_button4 = [[MFANMetalButton alloc] initWithFrame: buttonFrame
					    title: nil
					    color: [UIColor greenColor]
					    fontSize: 0];
	[_button4 addCallback: self
		  withAction: @selector(useCloudDefault:)
		  value: nil];
	[self addSubview: _button4];
	labelFrame.origin.y += labelFrame.size.height*1.3;
	[_button4 setSelected: _useCloudDefault];

	/* debugging button */
	buttonFrame = labelFrame;
	buttonFrame.origin.x += _appWidth * 0.80;
	buttonFrame.size.width = _appWidth * 0.20;
	_label6 = [[MFANLabel alloc] initWithFrame: labelFrame
				     target: self
				     selector:@selector(helpSendUsage:withData:)];
	[_label6 setTitle: @"Send AudioGalaxy anon info" forState: UIControlStateNormal];
	[_label6 setTitleColor: [MFANTopSettings textColor] forState: UIControlStateNormal];
	tlabel = [_label6 titleLabel];
	_label6.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
	[tlabel setFont: [MFANTopSettings basicFontWithSize:
					      labelFrame.size.height * fontSizeScale]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];
	[self addSubview: _label6];
	_button6 = [[MFANMetalButton alloc] initWithFrame: buttonFrame
					    title: nil
					    color: [UIColor greenColor]
					    fontSize: 0];
	[_button6 addCallback: self
		  withAction: @selector(sendUsage:)
		  value: nil];
	[self addSubview: _button6];
	labelFrame.origin.y += labelFrame.size.height*1.3;
	[_button6 setSelected: [MFANTopSettings sendUsage]];

	/* auto download button */
	buttonFrame = labelFrame;
	buttonFrame.origin.x += _appWidth * 0.80;
	buttonFrame.size.width = _appWidth * 0.20;
	_label8 = [[MFANLabel alloc] initWithFrame: labelFrame
				     target: self
				     selector:@selector(helpAutoDownload:withData:)];
	[_label8 setTitle: @"Auto download podcasts" forState: UIControlStateNormal];
	[_label8 setTitleColor: [MFANTopSettings textColor] forState: UIControlStateNormal];
	tlabel = [_label8 titleLabel];
	_label8.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
	[tlabel setFont: [MFANTopSettings basicFontWithSize:
					      labelFrame.size.height * fontSizeScale]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];
	[self addSubview: _label8];
	_button8 = [[MFANMetalButton alloc] initWithFrame: buttonFrame
					    title: nil
					    color: [UIColor greenColor]
					    fontSize: 0];
	[_button8 addCallback: self
		  withAction: @selector(autoDownload:)
		  value: nil];
	[self addSubview: _button8];
	labelFrame.origin.y += labelFrame.size.height*1.3;
	[_button8 setSelected: [MFANTopSettings autoDownload]];

	/* purge contents button */
	buttonFrame = labelFrame;
	buttonFrame.origin.x += _appWidth * 0.80;
	buttonFrame.size.width = _appWidth * 0.20;
	_label9 = [[MFANLabel alloc] initWithFrame: labelFrame
				     target: self
				     selector:@selector(helpUnloadAllPodcasts:withData:)];
	[_label9 setTitle: @"Unload all podcasts" forState: UIControlStateNormal];
	[_label9 setTitleColor: [MFANTopSettings textColor] forState: UIControlStateNormal];
	tlabel = [_label9 titleLabel];
	_label9.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
	[tlabel setFont: [MFANTopSettings basicFontWithSize:
					      labelFrame.size.height * fontSizeScale]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];
	[self addSubview: _label9];
	_button9 = [[MFANMetalButton alloc] initWithFrame: buttonFrame
					    title: nil
					    color: [UIColor blueColor]
					    fontSize: 0];
	[_button9 addCallback: self
		  withAction: @selector(unloadAllPodcasts:)
		  value: nil];
	[self addSubview: _button9];
	labelFrame.origin.y += labelFrame.size.height*1.3;
	[_button9 setSelected: YES];

	/* reset warnings button */
	buttonFrame = labelFrame;
	buttonFrame.origin.x += _appWidth * 0.80;
	buttonFrame.size.width = _appWidth * 0.20;
	_label10 = [[MFANLabel alloc] initWithFrame: labelFrame
				     target: self
				     selector:@selector(helpResetWarnings:withData:)];
	[_label10 setTitle: @"Reset help/warnings" forState: UIControlStateNormal];
	[_label10 setTitleColor: [MFANTopSettings textColor] forState: UIControlStateNormal];
	tlabel = [_label10 titleLabel];
	_label10.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
	[tlabel setFont: [MFANTopSettings basicFontWithSize:
					      labelFrame.size.height * fontSizeScale]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];
	[self addSubview: _label10];
	_button10 = [[MFANMetalButton alloc] initWithFrame: buttonFrame
					     title: nil
					     color: [UIColor blueColor]
					     fontSize: 0];
	[_button10 addCallback: self
		  withAction: @selector(resetWarnings:)
		  value: nil];
	[self addSubview: _button10];
	labelFrame.origin.y += labelFrame.size.height*1.3;
	[_button10 setSelected: YES];

	/* reset warnings button */
	buttonFrame = labelFrame;
	buttonFrame.origin.x += _appWidth * 0.80;
	buttonFrame.size.width = _appWidth * 0.20;
	_label11 = [[MFANLabel alloc] initWithFrame: labelFrame
				      target: self
				      selector:@selector(helpHelp:withData:)];
	[_label11 setTitle: @"Help with Application" forState: UIControlStateNormal];
	[_label11 setTitleColor: [MFANTopSettings textColor] forState: UIControlStateNormal];
	tlabel = [_label11 titleLabel];
	_label11.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
	[tlabel setFont: [MFANTopSettings basicFontWithSize:
					      labelFrame.size.height * fontSizeScale]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];
	[self addSubview: _label11];
	_button11 = [[MFANMetalButton alloc] initWithFrame: buttonFrame
					     title: nil
					     color: [UIColor blueColor]
					     fontSize: 0];
	[_button11 addCallback: self
		  withAction: @selector(help:)
		  value: nil];
	[self addSubview: _button11];
	labelFrame.origin.y += labelFrame.size.height*1.3;
	[_button11 setSelected: YES];

	/* unload podcasts when done button */
	buttonFrame = labelFrame;
	buttonFrame.origin.x += _appWidth * 0.80;
	buttonFrame.size.width = _appWidth * 0.20;
	_label12 = [[MFANLabel alloc] initWithFrame: labelFrame
				      target: self
				      selector:@selector(helpUnloadPlayed:withData:)];
	[_label12 setTitle: @"Unload podcast when done" forState: UIControlStateNormal];
	[_label12 setTitleColor: [MFANTopSettings textColor] forState: UIControlStateNormal];
	tlabel = [_label12 titleLabel];
	_label12.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
	[tlabel setFont: [MFANTopSettings basicFontWithSize:
					      labelFrame.size.height * fontSizeScale]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];
	[self addSubview: _label12];
	_button12 = [[MFANMetalButton alloc] initWithFrame: buttonFrame
					     title: nil
					     color: [UIColor blueColor]
					     fontSize: 0];
	[_button12 addCallback: self
		  withAction: @selector(unloadPlayed:)
		  value: nil];
	[self addSubview: _button12];
	labelFrame.origin.y += labelFrame.size.height*1.3;
	[_button12 setSelected: NO];

	/* configure DLNA */
	buttonFrame = labelFrame;
	buttonFrame.origin.x += _appWidth * 0.80;
	buttonFrame.size.width = _appWidth * 0.20;
	_label13 = [[MFANLabel alloc] initWithFrame: labelFrame
				      target: self
				      selector:@selector(helpUpnp:withData:)];
	[_label13 setTitle: @"Setup UPNP Music Servers" forState: UIControlStateNormal];
	[_label13 setTitleColor: [MFANTopSettings textColor] forState: UIControlStateNormal];
	tlabel = [_label13 titleLabel];
	_label13.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
	[tlabel setFont: [MFANTopSettings basicFontWithSize:
					      labelFrame.size.height * fontSizeScale]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];
	[self addSubview: _label13];
	_button13 = [[MFANMetalButton alloc] initWithFrame: buttonFrame
					     title: nil
					     color: [UIColor blueColor]
					     fontSize: 0];
	[_button13 addCallback: self
		  withAction: @selector(upnp:)
		  value: nil];
	[self addSubview: _button13];
	labelFrame.origin.y += labelFrame.size.height*1.3;
	[_button13 setSelected: YES];

	/* enable autodim */
	buttonFrame = labelFrame;
	buttonFrame.origin.x += _appWidth * 0.80;
	buttonFrame.size.width = _appWidth * 0.20;
	_label14 = [[MFANLabel alloc] initWithFrame: labelFrame
				      target: self
				      selector:@selector(helpAutoDim:withData:)];
	[_label14 setTitle: @"Autodim after a minute" forState: UIControlStateNormal];
	[_label14 setTitleColor: [MFANTopSettings textColor] forState: UIControlStateNormal];
	tlabel = [_label14 titleLabel];
	_label14.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
	[tlabel setFont: [MFANTopSettings basicFontWithSize:
					      labelFrame.size.height * fontSizeScale]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];
	[self addSubview: _label14];
	_button14 = [[MFANMetalButton alloc] initWithFrame: buttonFrame
					     title: nil
					     color: [UIColor greenColor]
					     fontSize: 0];
	[_button14 setSelected: [MFANTopSettings autoDim]];
	[_button14 addCallback: self
		  withAction: @selector(autoDim:)
		  value: nil];
	[self addSubview: _button14];
	labelFrame.origin.y += labelFrame.size.height*1.3;

	/* configure DLNA */
	buttonFrame = labelFrame;
	buttonFrame.origin.x += _appWidth * 0.80;
	buttonFrame.size.width = _appWidth * 0.20;
	_label15 = [[MFANLabel alloc] initWithFrame: labelFrame
				      target: self
				      selector:@selector(helpHistory:withData:)];
	[_label15 setTitle: @"View History" forState: UIControlStateNormal];
	[_label15 setTitleColor: [MFANTopSettings textColor] forState: UIControlStateNormal];
	tlabel = [_label15 titleLabel];
	_label15.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
	[tlabel setFont: [MFANTopSettings basicFontWithSize:
					      labelFrame.size.height * fontSizeScale]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];
	[self addSubview: _label15];
	_button15 = [[MFANMetalButton alloc] initWithFrame: buttonFrame
					     title: nil
					     color: [UIColor blueColor]
					     fontSize: 0];
	[_button15 addCallback: self
		  withAction: @selector(showHistory:)
		  value: nil];
	[self addSubview: _button15];
	labelFrame.origin.y += labelFrame.size.height*1.3;
	[_button15 setSelected: YES];

	/* ================================================================ */

	_buttonWidth = _appWidth/5;

	_cancelFrame.origin.x = _appFrame.origin.x + 0.67 * _appFrame.size.width - _buttonWidth/2;
	_cancelFrame.size.width = _buttonWidth;
	_cancelFrame.origin.y = _appFrame.origin.y + (1.0 - buttonPct) * _appFrame.size.height;
	_cancelFrame.size.height = buttonPct * _appFrame.size.height;
	_cancelButton = [[MFANIconButton alloc] initWithFrame: _cancelFrame
						title: @"Cancel"
						color: [UIColor redColor]
						file: @"icon-cancel.png"];
	[self addSubview: _cancelButton];
	[_cancelButton addCallback: self withAction:@selector(cancelPressed:)];

	_doneFrame.origin.x = _appFrame.origin.x + 0.33 * _appFrame.size.width - _buttonWidth/2;
	_doneFrame.size.width = _buttonWidth;
	_doneFrame.origin.y = _appFrame.origin.y + (1.0 - buttonPct) * _appFrame.size.height;
	_doneFrame.size.height = buttonPct * _appFrame.size.height;
	_doneButton = [[MFANIconButton alloc] initWithFrame: _doneFrame
					      title: @"Done"
					      color: [UIColor colorWithHue: 0.3
							      saturation: 1.0
							      brightness: 1.0
							      alpha: 1.0]
					      file: @"icon-done.png"];
	[self addSubview: _doneButton];
	[_doneButton addCallback: self withAction:@selector(donePressed:)];

	[self setBackgroundColor: [UIColor whiteColor]];
    }
    return self;
}

- (void) helpRandomizeMusic: (id) junk withData: (id) junk2
{
    UIAlertView *alert = [[UIAlertView alloc]
			     initWithTitle:@"Randomize Music"
			     message:@"Music channels are randomized when created."
			     delegate:nil 
			     cancelButtonTitle:@"OK"
			     otherButtonTitles: nil];
    [alert show];
}

- (void) helpHistory: (id) junk withData: (id) junk2
{
    UIAlertView *alert = [[UIAlertView alloc]
			     initWithTitle:@"View History"
			     message:@"Visit history viewer"
			     delegate:nil 
			     cancelButtonTitle:@"OK"
			     otherButtonTitles: nil];
    [alert show];
}

- (void) helpUseCloudDefault: (id) junk withData: (id) junk2
{
    UIAlertView *alert = [[UIAlertView alloc]
			     initWithTitle:@"Use cloud items by default"
			     message:@"When defining channels, by default cloud resident\nitems are included.\nThis choice can be changed when defining a channel."
			     delegate:nil 
			     cancelButtonTitle:@"OK"
			     otherButtonTitles: nil];
    [alert show];
}

- (void) helpResyncLibrary: (id) junk withData: (id) junk2
{
    UIAlertView *alert = [[UIAlertView alloc]
			     initWithTitle:@"Resynchronize Library"
			     message:@"Force application to use latest version of iTunes library.\nThe library will include latest version of cloud-resident items.\nThis is normally not required, as iTunes library will be checked each time a channel is defined.\nChecking library doesn't force immediate changes to existing channels."
			     delegate:nil 
			     cancelButtonTitle:@"OK"
			     otherButtonTitles: nil];
    [alert show];
}

- (void) helpSendUsage: (id) junk withData: (id) junk2
{
    UIAlertView *alert = [[UIAlertView alloc]
			     initWithTitle:@"Send Usage"
			     message:@"Send anonymous media info to help developers prioritize feature development.  Only type of media (podcast, radio station, music) being played is sent.  No user identification information is sent.\n"
			     delegate:nil 
			     cancelButtonTitle:@"OK"
			     otherButtonTitles: nil];
    [alert show];
}

- (void) helpNeverUseCellData: (id) junk withData: (id) junk2
{
    UIAlertView *alert = [[UIAlertView alloc]
			     initWithTitle:@"Never use cell data"
			     message:@"Never use cellular data, even for downloading cloud music, or playing podcasts that aren't downloaded.  Items will be silently skipped."
			     delegate:nil 
			     cancelButtonTitle:@"OK"
			     otherButtonTitles: nil];
    [alert show];
}

- (void) helpResetWarnings: (id) junk withData: (id) junk2
{
    UIAlertView *alert = [[UIAlertView alloc]
			     initWithTitle:@"Reset Warnings"
			     message:@"Reset warnings -- per-screen help will show up again"
			     delegate:nil 
			     cancelButtonTitle:@"OK"
			     otherButtonTitles: nil];
    [alert show];
}

- (void) helpUpnp: (id) junk withData: (id) junk2
{
    UIAlertView *alert = [[UIAlertView alloc]
			     initWithTitle:@"Configure UPNP"
			     message:@"Search for / Configure UPNP Media Servers"
			     delegate:nil 
			     cancelButtonTitle:@"OK"
			     otherButtonTitles: nil];
    [alert show];
}

- (void) helpAutoDim: (id) junk withData: (id) junk2
{
    UIAlertView *alert = [[UIAlertView alloc]
			     initWithTitle:@"Automatic Dim"
			     message:@"Dim screen (don't lock) after 60 seconds"
			     delegate:nil 
			     cancelButtonTitle:@"OK"
			     otherButtonTitles: nil];
    [alert show];
}

- (void) helpUseCellForDownloads: (id) junk withData: (id) junk2
{
    UIAlertView *alert = [[UIAlertView alloc]
			     initWithTitle:@"Use Cell Data for Downloads"
			     message:@"Use cell data for background downloads of podcasts.  Even if disabled, if you play an unloaded podcast, app will stream it over cell data."
			     delegate:nil 
			     cancelButtonTitle:@"OK"
			     otherButtonTitles: nil];
    [alert show];
}

- (void) helpAutoDownload: (id) junk withData: (id) junk2
{
    UIAlertView *alert = [[UIAlertView alloc]
			     initWithTitle:@"Auto download podcasts"
			     message:@"Automatically download podcasts mentioned in your channels."
			     delegate:nil 
			     cancelButtonTitle:@"OK"
			     otherButtonTitles: nil];
    [alert show];
}

- (void) helpUnloadAllPodcasts: (id) junk withData: (id) junk2
{
    UIAlertView *alert = [[UIAlertView alloc]
			     initWithTitle:@"Unload all podcasts"
			     message:@"Delete all downloaded contents, freeing storage.\nPlaylists stay the same, but content must be downloaded again."
			     delegate:nil 
			     cancelButtonTitle:@"OK"
			     otherButtonTitles: nil];
    [alert show];
}

- (void) helpUnloadPlayed: (id) junk withData: (id) junk2
{
    UIAlertView *alert = [[UIAlertView alloc]
			     initWithTitle:@"Unload podcasts when finished"
			     message:@"Unload downloaded podcast when finished playing."
			     delegate:nil 
			     cancelButtonTitle:@"OK"
			     otherButtonTitles: nil];
    [alert show];
}

- (void) helpHelp: (id) junk withData: (id) junk2
{
    UIAlertView *alert = [[UIAlertView alloc]
			     initWithTitle:@"Help with Application"
			     message:@"Get help with application"
			     delegate:nil 
			     cancelButtonTitle:@"OK"
			     otherButtonTitles: nil];
    [alert show];
}

- (NSString *) settingsName
{
    NSArray *paths;
    NSString *libdir;
    NSString *filePath;

    paths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES);
    libdir = [paths objectAtIndex:0];
    filePath = [NSString stringWithFormat: @"%@/settings", libdir];

    return filePath;
}

- (void) donePressed: (id) junk
{
    [_viewCon switchToAppByName: @"main"];
}

- (void) cancelPressed: (id) junk
{
    [_viewCon switchToAppByName: @"main"];
}

-(void) activateTop
{
    return;
}

-(void) deactivateTop
{
    return;
}

- (void) resyncLibrary: (id) value
{
    [MFANSetList forceResync];
}

- (void) unloadAllPodcasts: (id) value
{
    MFANTopLevel *topLevel;
    NSArray *playContexts;
    MFANPlayContext *pc;
    MFANSetList *setList;
    MFANWarn *warn;

    warn = [[MFANWarn alloc] initWithTitle: @"Unload all podcasts"
			     message: @"Deleting all loaded podcast media files"
			     secs: 2.0];

    [MFANDownload deleteDownloadedContent];
    topLevel = (MFANTopLevel *) [_viewCon getTopAppByName: @"main"];
    playContexts = [topLevel getPlayContexts];
    for(pc in playContexts) {
	setList = [pc setList];
	[[pc download] checkDownloadedArray: [setList itemArray]];
    }
}

- (void) unloadPlayed: (id) value
{
    _unloadPlayed = !_unloadPlayed;
    [_button12 setSelected: _unloadPlayed];

    [self saveSettings];
}

- (void) sendUsage: (id) value
{
    _sendUsage = !_sendUsage;
    [_button6 setSelected: _sendUsage];

    [self saveSettings];
}

- (void) help: (id) value
{
    [_viewCon switchToAppByName: @"help"];
}

- (void) neverUseCellData: (id) value
{
    _neverUseCellData = !_neverUseCellData;
    [_button1 setSelected: _neverUseCellData];

    [self saveSettings];
}

- (void) resetWarnings: (id) value
{
    MFANWarn *warn;

    warn = [[MFANWarn alloc] initWithTitle: @"Reset Warnings"
			     message: @"Resetting all warnings"
			     secs: 2.0];
    _warningFlags = 0;

    [self saveSettings];
}

- (void) showHistory: (id) value
{
    [_viewCon switchToAppByName: @"history"];
}

- (void) upnp: (id) value
{
    [_viewCon switchToAppByName: @"upnp"];
}

- (void) autoDim: (id) value
{
    _autoDim = !_autoDim;
    [_button14 setSelected: _autoDim];

    [self saveSettings];
}

- (void) useCloudDefault: (id) value
{
    _useCloudDefault = !_useCloudDefault;
    [_button4 setSelected: _useCloudDefault];

    [self saveSettings];
}

- (void) useCellForDownloads: (id) value
{
    _useCellForDownloads = !_useCellForDownloads;
    [_button7 setSelected: _useCellForDownloads];

    [self saveSettings];
}

- (void) autoDownload: (id) value
{
    _autoDownload = !_autoDownload;
    [_button8 setSelected: _autoDownload];

    [self saveSettings];
}

- (void) reloadSettings
{
    FILE *filep;
    Xgml *xgmlp;
    Xgml::Node *rootNodep;
    Xgml::Attr *attrp;
    char *tbufferp;
    char *origBufferp;
    int tc;
    int bytesRead;
    char *tp;
    int32_t code;
    int overflow;
    static const uint32_t maxFileSize = 10000;

    /* defaults */
    _useCloudDefault = 0;
    _sendUsage = 1;
    _autoDim = 0;
    _useCellForDownloads = 1;
    _autoDownload = 0;
    _neverUseCellData = 0;
    _warningFlags = 0;
    _firstUsed = 0;

    xgmlp = new Xgml();
    rootNodep = NULL;

    filep = fopen([_associatedFile cStringUsingEncoding: NSUTF8StringEncoding], "r");
    if (!filep) {
	delete xgmlp;
	return;
    }
    origBufferp = tbufferp = (char *) malloc(maxFileSize);
    bytesRead = 0;
    tp = tbufferp;
    overflow = 0;
    while (1) {
	if (bytesRead >= maxFileSize-1) {
	    overflow = 1;
	    break;
	}
	tc = fgetc(filep);
	if (tc < 0) {
	    break;
	}
	*tp++ = tc;
	bytesRead++;
    }
    if (overflow) {
	delete xgmlp;
	return;
    }
    *tp++ = 0;

    fclose(filep);
    filep = NULL;

    code = xgmlp->parse(&tbufferp, &rootNodep);
    if (code) {
	delete xgmlp;
	return;
    }

    /* pull out the query string */
    for(attrp = rootNodep->_attrs.head(); attrp; attrp = attrp->_dqNextp) {
	if (strcmp(attrp->_name.c_str(), "useCloudDefault") == 0) {
	    code = sscanf(attrp->_value.c_str(), "%d", &_useCloudDefault);
	}
	else if (strcmp(attrp->_name.c_str(), "sendUsage") == 0) {
	    code = sscanf(attrp->_value.c_str(), "%d", &_sendUsage);
	}
	else if (strcmp(attrp->_name.c_str(), "autoDim") == 0) {
	    code = sscanf(attrp->_value.c_str(), "%d", &_autoDim);
	}
	else if (strcmp(attrp->_name.c_str(), "warningFlags") == 0) {
	    code = sscanf(attrp->_value.c_str(), "%d", &_warningFlags);
	}
	else if (strcmp(attrp->_name.c_str(), "autoDownload") == 0) {
	    code = sscanf(attrp->_value.c_str(), "%d", &_autoDownload);
	}
	else if (strcmp(attrp->_name.c_str(), "firstUsed") == 0) {
	    code = sscanf(attrp->_value.c_str(), "%u", &_firstUsed);
	}
	else if (strcmp(attrp->_name.c_str(), "licensed") == 0) {
	    code = sscanf(attrp->_value.c_str(), "%u", &_licensed);
	}
    }

    delete rootNodep;
    delete xgmlp;
    free(origBufferp);
}

- (void) saveSettings
{
    char tbuffer[100];
    MFANFileWriter *writer;
    std::string cppString;
    long code;

    Xgml *xgmlp;
    Xgml::Node *rootNodep;
    Xgml::Attr *attrNodep;
    xgmlp = new Xgml;
    rootNodep = new Xgml::Node();
    rootNodep->init("settings", /* needsEnd */ 1, /* !isLeaf */ 0);

    sprintf(tbuffer, "%6lu", (long) _useCloudDefault);
    attrNodep = new Xgml::Attr();
    attrNodep->init("useCloudDefault", tbuffer);
    rootNodep->appendAttr(attrNodep);
    
    sprintf(tbuffer, "%6lu", (long) _sendUsage);
    attrNodep = new Xgml::Attr();
    attrNodep->init("sendUsage", tbuffer);
    rootNodep->appendAttr(attrNodep);
    
    sprintf(tbuffer, "%6lu", (long) _autoDim);
    attrNodep = new Xgml::Attr();
    attrNodep->init("autoDim", tbuffer);
    rootNodep->appendAttr(attrNodep);
    
    /* turn off unused flags, so we can reuse flags eventually */
    sprintf(tbuffer, "%6lu", (long) (_warningFlags & MFANTopSettings_warnedAll));
    attrNodep = new Xgml::Attr();
    attrNodep->init("warningFlags", tbuffer);
    rootNodep->appendAttr(attrNodep);
    
    sprintf(tbuffer, "%6lu", (long) _useCellForDownloads);
    attrNodep = new Xgml::Attr();
    attrNodep->init("useCellForDownloads", tbuffer);
    rootNodep->appendAttr(attrNodep);

    sprintf(tbuffer, "%6lu", (long) _autoDownload);
    attrNodep = new Xgml::Attr();
    attrNodep->init("autoDownload", tbuffer);
    rootNodep->appendAttr(attrNodep);

    sprintf(tbuffer, "%u", (unsigned int) _firstUsed);
    attrNodep = new Xgml::Attr();
    attrNodep->init("firstUsed", tbuffer);
    rootNodep->appendAttr(attrNodep);
    
    sprintf(tbuffer, "%u", (unsigned int) _licensed);
    attrNodep = new Xgml::Attr();
    attrNodep->init("licensed", tbuffer);
    rootNodep->appendAttr(attrNodep);
    
    NSLog(@"- Saving settings to file %@", _associatedFile);
    writer = [[MFANFileWriter alloc] initWithFile: _associatedFile];
    if ([writer failed]) {
	delete rootNodep;
	return;
    }

    cppString.clear();
    rootNodep->printToCPP(&cppString);
    code = fwrite(cppString.c_str(), (int) cppString.length(), 1, [writer fileOf]);
    if (code != 1) {
	NSLog(@"!WRITE FAILED");
	[writer cleanup];
    } else {
	code = [writer flush];
	if (code != 0) {
	    NSLog(@"!FCLOSE FAILED");
	}
    }
    delete rootNodep;
    NSLog(@"- Done with async part of save");
}

+ (int) randomizePodcasts
{
    if (!_globalSettings)
	return 0;

    return 0;
}

+ (int) unloadPlayed
{
    if (!_globalSettings)
	return 0;

    return _globalSettings->_unloadPlayed;
}

+ (int) randomizeMusic
{
    if (!_globalSettings)
	return 1;

    return 0;
}

+ (int) useCloudDefault
{
    if (!_globalSettings)
	return 1;

    return _globalSettings->_useCloudDefault;
}

+ (int) licensed
{
    if (!_globalSettings)
	return 1;

#ifndef MFAN_DEMO
    /* non-demo always has license */
    return 1;
#endif

    return _globalSettings->_licensed;
}

+ (int) sendUsage
{
    if (!_globalSettings)
	return 1;

    return _globalSettings->_sendUsage;
}

+ (int) autoDim
{
    if (!_globalSettings)
	return 0;

    return _globalSettings->_autoDim;
}

+ (int) neverUseCellData
{
    if (!_globalSettings)
	return 0;

    return _globalSettings->_neverUseCellData;
}

+ (int) autoDownload
{
    if (!_globalSettings)
	return 0;
    return _globalSettings->_autoDownload;
}

+ (unsigned int) firstUsed
{
    if (!_globalSettings)
	return 0;
    return _globalSettings->_firstUsed;
}

+ (int) useCellForDownloads
{
    if (!_globalSettings)
	return 1;

    return _globalSettings->_useCellForDownloads;
}

+ (MFANTopSettings *) globalSettings
{
    if (_globalSettings == nil)
	NSLog(@"!GlobalSettings called too early!!");

    return _globalSettings;
}

+ (UIColor *) baseColor
{
    return _globalSettings->_baseColor;
}

+ (UIColor *) lightBaseColor
{
    return _globalSettings->_lightBaseColor;
}

+ (UIColor *) clearBaseColor
{
    return _globalSettings->_clearBaseColor;
}

+ (UIColor *) textColor
{
    return _globalSettings->_textColor;
}

+ (UIColor *) selectedBackgroundColor
{
    return _globalSettings->_selectedBackgroundColor;
}

+ (void) setWarningFlag: (int) flag
{
    _globalSettings->_warningFlags |= flag;
    [_globalSettings saveSettings];
}

+ (int) warningFlags
{
    return _globalSettings->_warningFlags;
}

+ (UIFont *) basicFontWithSize: (CGFloat) size
{
    //return [UIFont fontWithName: @"Avenir-Black" size: size];
    //return [UIFont fontWithName: @"AvenirNextCondensed-Bold" size: size];
    return [UIFont fontWithName: @"Arial-BoldMT" size: size];
    // return [UIFont boldSystemFontOfSize: size];
}

// Only override drawRect: if you perform custom drawing.
// An empty implementation adversely affects performance during animation.
//- (void)drawRect:(CGRect)rect
//{
//    drawBackground(rect);
//}

@end
