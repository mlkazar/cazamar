//
//  Settings.m
//  MusicFan
//
//  Created by Michael Kazar on 6/11/14.
//  Copyright (c) 2014-2026 Mike Kazar. All rights reserved.
//
#import "MFANCGUtil.h"
#import "MFANFileWriter.h"
#import "MFANIconButton.h"
#import "Settings.h"
#import "ViewController.h"

#include "xgml.h"
#include <string>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/stat.h>

/* SettingsLabels are actually UIButtons, so you can use them as such
 * for adjust appearances.
 */
@implementation SettingsLabel {
    id _target;
    SEL _sel;
}

- (SettingsLabel *) initWithFrame: (CGRect) frame target: (id) target selector:(SEL) selector
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
	[_target performSelectorOnMainThread: _sel
				  withObject: nil
			       waitUntilDone: true];
    }
}
@end

@implementation Settings {
    ViewController *_vc;
    CGRect _appFrame;
    CGFloat _appVMargin;
    CGFloat _appHMargin;
    CGFloat _appWidth;
    CGFloat _appHeight;
    CGFloat _buttonWidth;
    NSString *_associatedFile;

    SettingsLabel *_label1;
    UISwitch *_button1;
    SettingsLabel *_label2;
    UISwitch *_button2;
    SettingsLabel *_label3;
    UIStepper *_button3;

    CGRect _doneFrame;
    CGRect _cancelFrame;

    MFANIconButton *_doneButton;

    bool _keepStreamingAfterSwitch;	// except for carplay
    bool _keepStreamingAfterCarPlay;	// after carplay next/prev
    uint32_t _streamBufferMinutes;	// minutes of stream buffer to keep
}

Settings *_globalSettings;

- (Settings *) initWithViewController: (ViewController *) vc;
{
    CGRect labelFrame;
    CGRect buttonFrame;
    UILabel *tlabel;
    int buttonHeight;
    float buttonPct = 0.10;
    CGRect frame;

    self.frame = vc.activeFrame;
    frame = vc.activeFrame;

    self = [super initWithFrame:frame];
    if (self) {
        // Initialization code
	_globalSettings = self;
	_associatedFile = fileNameForFile(@"settings.xgml");

	[self reloadSettings];

	_vc = vc;

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

	float fontSizeScale = 0.6;
	float labelPct = 0.667;

	labelFrame = _appFrame;
	labelFrame.size.height = buttonHeight;
	labelFrame.size.width = _appWidth * labelPct;

	/* ================================================================ */

	/* setup next settings button */
	buttonFrame = labelFrame;
	buttonFrame.origin.x += _appWidth * labelPct;
	buttonFrame.size.width = _appWidth * (1 - labelPct);
	_label1 = [[SettingsLabel alloc] initWithFrame: labelFrame
						target: self
					      selector: @selector(helpStreamAfterSwitch:)];
	tlabel = [_label1 titleLabel];
	_label1.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
	[_label1 setTitle: @"Stream even after switch" forState: UIControlStateNormal];
	[_label1 setTitleColor: [UIColor blackColor] forState: UIControlStateNormal];
	[_label1 setSelected: YES];
	[tlabel setFont: [UIFont fontWithName: @"Arial-BoldMT"
					 size: labelFrame.size.height * fontSizeScale]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];
	[self addSubview: _label1];
	_button1 = [[UISwitch alloc] initWithFrame: buttonFrame];

	[_button1 addTarget: self
		     action:@selector(streamAfterSwitch:)
	   forControlEvents:UIControlEventAllEvents];
	[self addSubview: _button1];
	[_button1 setOn: _keepStreamingAfterSwitch animated: false];

	labelFrame.origin.y += labelFrame.size.height*1.3;

	buttonFrame = labelFrame;
	buttonFrame.origin.x += _appWidth * labelPct;
	buttonFrame.size.width = _appWidth * (1 - labelPct);
	_label2 = [[SettingsLabel alloc] initWithFrame: labelFrame
						target: self
					      selector: @selector(helpStreamAfterCarPlay:)];
	tlabel = [_label2 titleLabel];
	_label2.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
	[_label2 setTitle: @"Stream even after car play" forState: UIControlStateNormal];
	[_label2 setTitleColor: [UIColor blackColor] forState: UIControlStateNormal];
	[_label2 setSelected: YES];
	[tlabel setFont: [UIFont fontWithName: @"Arial-BoldMT"
					 size: labelFrame.size.height * fontSizeScale]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];
	[self addSubview: _label2];
	_button2 = [[UISwitch alloc] initWithFrame: buttonFrame];

	[_button2 addTarget: self
		     action:@selector(streamAfterCarPlay:)
	   forControlEvents:UIControlEventAllEvents];
	[self addSubview: _button2];
	[_button2 setOn: _keepStreamingAfterCarPlay animated: false];

	labelFrame.origin.y += labelFrame.size.height*1.3;

	buttonFrame = labelFrame;
	buttonFrame.origin.x += _appWidth * labelPct;
	buttonFrame.size.width = _appWidth * (1-labelPct);
	_label3 = [[SettingsLabel alloc] initWithFrame: labelFrame
						target: self
					      selector: @selector(helpStreamBuffer:)];
	tlabel = [_label3 titleLabel];
	_label3.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
	[_label3 setTitle: @"Hours of stream to keep" forState: UIControlStateNormal];
	[_label3 setTitleColor: [UIColor blackColor] forState: UIControlStateNormal];
	[_label3 setSelected: YES];
	[tlabel setFont: [UIFont fontWithName: @"Arial-BoldMT"
					 size: labelFrame.size.height * fontSizeScale]];
	[tlabel setAdjustsFontSizeToFitWidth: YES];
	[self addSubview: _label3];
	_button3 = [[UIStepper alloc] initWithFrame: buttonFrame];
	_button3.minimumValue = 0.5;
	_button3.maximumValue = 48;
	_button3.stepValue = 0.5;

	[_button3 addTarget: self
		     action:@selector(streamBuffer:)
	   forControlEvents:UIControlEventAllEvents];
	[self addSubview: _button3];
	_button3.value = _streamBufferMinutes / 60;

	/* ================================================================ */

	_buttonWidth = _appWidth/5;
	buttonHeight = _buttonWidth;

	_doneFrame.origin.x = 0.5 * frame.size.width - _buttonWidth/2;
	_doneFrame.size.width = _buttonWidth;
	_doneFrame.origin.y = frame.size.height - buttonHeight;
	_doneFrame.size.height = buttonHeight;
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

- (void) donePressed: (id) junk {
    [self saveSettings];
    [_vc popTopView];
}

- (void) cancelPressed: (id) junk {
    [_vc popTopView];
}

- (void) streamAfterSwitch: (UISwitch *) sender {
    _keepStreamingAfterSwitch = sender.on;
}

- (void) helpStreamAfterSwitch: (id) junk {
    UIAlertController *alert = [UIAlertController
				   alertControllerWithTitle: @"Keep Streaming After Switch"
						    message:@"Keep streaming from a station even"
				   @" after switching away from it using app."
					     preferredStyle: UIAlertControllerStyleAlert];
    UIAlertAction *action = [UIAlertAction actionWithTitle: @"Done"
						     style:UIAlertActionStyleDefault
						   handler: ^(UIAlertAction *act) {
	    return;
	}];
    [alert addAction: action];
    [_vc presentViewController: alert animated:true completion: nil];
}

- (void) streamAfterCarPlay: (UISwitch *) sender {
    _keepStreamingAfterCarPlay = sender.on;
}

- (void) helpStreamAfterCarPlay: (id) junk {
    UIAlertController *alert = [UIAlertController
				   alertControllerWithTitle: @"Keep Streaming After Car Play"
						    message:@"Keep streaming from a station even"
				   @" after switching away from it using Car Play or Lock Screen."
					     preferredStyle: UIAlertControllerStyleAlert];
    UIAlertAction *action = [UIAlertAction actionWithTitle: @"Done"
						     style:UIAlertActionStyleDefault
						   handler: ^(UIAlertAction *act) {
	    return;
	}];
    [alert addAction: action];
    [_vc presentViewController: alert animated:true completion: nil];
}

- (void) streamBuffer: (UIStepper *) sender {
    _streamBufferMinutes = (uint32_t) (sender.value * 60.0);
    NSLog(@"set stream buffer minutes to %d", _streamBufferMinutes);
}

- (void) helpStreamBuffer: (id) junk {
    UIAlertController *alert = [UIAlertController
				   alertControllerWithTitle: @"Stream Buffer Size"
						    message:@"Hours of stream to buffer"
					     preferredStyle: UIAlertControllerStyleAlert];
    UIAlertAction *action = [UIAlertAction actionWithTitle: @"Done"
						     style:UIAlertActionStyleDefault
						   handler: ^(UIAlertAction *act) {
	    return;
	}];
    [alert addAction: action];
    [_vc presentViewController: alert animated:true completion: nil];
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
    _keepStreamingAfterSwitch = true;

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
    int temp;
    // defaults
    _keepStreamingAfterSwitch = false;
    _keepStreamingAfterCarPlay = false;
    _streamBufferMinutes = 150;	// 2.5 hours in minutes
    for(attrp = rootNodep->_attrs.head(); attrp; attrp = attrp->_dqNextp) {
	if (strcmp(attrp->_name.c_str(), "keepStreamingAfterSwitch") == 0) {
	    code = sscanf(attrp->_value.c_str(), "%d", &temp);
	    _keepStreamingAfterSwitch = temp;
	} else if (strcmp(attrp->_name.c_str(), "keepStreamingAfterCarPlay") == 0) {
	    code = sscanf(attrp->_value.c_str(), "%d", &temp);
	    _keepStreamingAfterCarPlay = temp;
	} else if (strcmp(attrp->_name.c_str(), "streamBufferMinutes") == 0) {
	    code = sscanf(attrp->_value.c_str(), "%d", &temp);
	    _streamBufferMinutes = temp;
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

    snprintf(tbuffer, sizeof(tbuffer), "%6lu", (long) _keepStreamingAfterSwitch);
    attrNodep = new Xgml::Attr();
    attrNodep->init("keepStreamingAfterSwitch", tbuffer);
    rootNodep->appendAttr(attrNodep);
    
    snprintf(tbuffer, sizeof(tbuffer), "%6lu", (long) _keepStreamingAfterCarPlay);
    attrNodep = new Xgml::Attr();
    attrNodep->init("keepStreamingAfterPlay", tbuffer);
    rootNodep->appendAttr(attrNodep);
    
    snprintf(tbuffer, sizeof(tbuffer), "%6lu", (long) _streamBufferMinutes);
    attrNodep = new Xgml::Attr();
    attrNodep->init("streamBufferMinutes", tbuffer);
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

- (void) activateTopView {
    return;
}

- (void) deactivateTopView {
    return;
}

@end
