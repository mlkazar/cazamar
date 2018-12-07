//
//  ViewController.m
//  Upload
//
//  Created by Michael Kazar on 5/27/18.
//  Copyright © 2018 Mike Kazar. All rights reserved.
//

#import "ViewController.h"
#include "upobj.h"

NSImage *
resizeImage2(NSImage* image, NSSize newSize) 
{
    NSImage *newImage;
    NSSize oldSize;

    newImage = [[NSImage alloc] initWithSize: newSize];
    oldSize = [image size];

    [newImage lockFocus];
    [image setSize: newSize];
    [image drawInRect: NSMakeRect(0, 0, newSize.width, newSize.height)
	   fromRect: NSMakeRect(0, 0, oldSize.width, oldSize.height)
	   operation: NSCompositingOperationSourceOver
	   fraction: 1.0];
    [newImage unlockFocus];

    return newImage;
}

@implementation ViewController

WKWebView *_webViewp;
NSURL *_urlp;
NSURLRequest *_requestp;
WebView *_oldViewp;
NSStatusItem *_statusItem;
NSMenu *_menu;
Upload *_uploadp;
NSAlert *_alert;
NSTimer *_timer;

- (ViewController *) init {
    NSImage *image;
    NSImage *scaledImage;

    self = [super init];
    if (self) {
	_statusItem = [[NSStatusBar systemStatusBar]
			  statusItemWithLength: NSSquareStatusItemLength];
	image = [NSImage imageNamed: @"status.png"];
	scaledImage = resizeImage2(image, NSMakeSize(18.0, 18.0));
	_statusItem.button.image = image;
	_statusItem.button.alternateImage = image;
	_statusItem.highlightMode = YES;
	//	_statusItem.visible = YES;
    
	[self updateMenu];
    }

    return self;
}


- (void) updateMenu
{
    NSMenuItem *item;

    _menu = [[NSMenu alloc] init];
    _statusItem.menu = _menu;
    
    item = [[NSMenuItem alloc]
	       initWithTitle: @"Setup/Run"
	       action: @selector(detailsPressed)
	       keyEquivalent: (NSString *) @"s"];
    item.target = self;
    [_menu addItem: item];
    
    item = [[NSMenuItem alloc]
	       initWithTitle: @"Backup now"
	       action: @selector(backupPressed)
	       keyEquivalent: @"b"];
    item.target = self;
    [_menu addItem: item];

    item = [NSMenuItem separatorItem];
    [_menu addItem: item];
    
    item = [[NSMenuItem alloc]
	       initWithTitle: @"Version 1.5 (12/07/2018)"
	       action: @selector(versionPressed)
	       keyEquivalent: @""];
    item.target = self;
    [_menu addItem: item];
    
    item = [NSMenuItem separatorItem];
    [_menu addItem: item];
    
    item = [[NSMenuItem alloc]
	       initWithTitle: @"Quit"
	       action: @selector(quitPressed)
	       keyEquivalent: (NSString *) @"q"];
    item.target = self;
    [_menu addItem: item];
}

- (void) ensureSetup
{
    if (!_uploadp) {
	_uploadp = new Upload();
	_uploadp->init(NULL, NULL);
    }
}

- (void) backupPressed
{
    NSLog(@"backup pressed");
    [self ensureSetup];
    _uploadp->backup();
}

- (void) versionPressed {
    return;
}

- (void) detailsPressed {
    NSLog(@"details pressed!!");
    [self ensureSetup];

    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: @"http://localhost:7701"]];
}

- (void) quitPressed {
    NSLog(@"quit pressed");
    [NSApp terminate: self];
}

@end
