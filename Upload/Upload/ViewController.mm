//
//  ViewController.m
//  Upload
//
//  Created by Michael Kazar on 5/27/18.
//  Copyright © 2018 Mike Kazar. All rights reserved.
//

#import "ViewController.h"
#include "upload.h"

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

- (void) loadView {
    [super loadView];

}

- (void)viewDidLoad {
    NSImage *image;
    NSImage *scaledImage;

    [super viewDidLoad];

    _statusItem = [[NSStatusBar systemStatusBar]
		      statusItemWithLength: NSSquareStatusItemLength];
    image = [NSImage imageNamed: @"status.png"];
    scaledImage = resizeImage2(image, NSMakeSize(18.0, 18.0));
    _statusItem.button.image = image;
    _statusItem.button.alternateImage = image;
    _statusItem.visible = YES;
    
    [self updateMenu];
}


- (void) updateMenu
{
    NSMenuItem *item;

    _menu = [[NSMenu alloc] init];
    _statusItem.menu = _menu;
    
    item = [[NSMenuItem alloc]
	       initWithTitle: @"Login"
	       action: @selector(loginPressed)
	       keyEquivalent: (NSString *) @"s"];
    item.target = self;
    [_menu addItem: item];
    
    item = [[NSMenuItem alloc]
	       initWithTitle: @"Backup"
	       action: @selector(backupPressed)
	       keyEquivalent: @"b"];
    item.target = self;
    [_menu addItem: item];

    item = [[NSMenuItem alloc]
	       initWithTitle: @"Run tests"
	       action: @selector(testPressed)
	       keyEquivalent: @"t"];
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

- (void) backupPressed
{
    NSLog(@"backup pressed");
}

- (void) testPressed
{
    if (!_uploadp) {
	_alert = [[NSAlert alloc] init];
	_alert.messageText = @"Not logged in";
	_alert.informativeText = @"You must login before you can run the CFS tests";
	[_alert runModal];
	NSLog(@"alert done");
    }
    else {
	_uploadp->runTests();
    }
}

- (void) loginPressed {
    NSLog(@"start pressed!!");
    _uploadp = new Upload();
    _uploadp->init(NULL, NULL);

    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: @"http://localhost:7701"]];
}

- (void) quitPressed {
    NSLog(@"quit pressed");
    [NSApp terminate: self];
}

- (void)setRepresentedObject:(id)representedObject {
    [super setRepresentedObject:representedObject];

    // Update the view, if already loaded.
}

- (void) webView: (WKWebView *) webViewp
didFailNavigation: (WKNavigation *)navp
       withError: (NSError *)errorp
{
    NSLog(@"IN FAIL");
}

- (void) webView: (WKWebView *) webViewp
didFailProvisionalNavigation: (WKNavigation *)navp
       withError: (NSError *)errorp
{
    NSLog(@"IN FAIL PROV");
}

@end
