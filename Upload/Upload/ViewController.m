//
//  ViewController.m
//  Upload
//
//  Created by Michael Kazar on 5/27/18.
//  Copyright © 2018 Mike Kazar. All rights reserved.
//

#import "ViewController.h"

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

- (void) loadView {
    [super loadView];

}

- (void)viewDidLoad {
    [super viewDidLoad];


    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: @"http://192.168.1.203:7701"]];

    {
	NSImage *image;
	NSImage *scaledImage;
	NSMenuItem *item;

	_statusItem = [[NSStatusBar systemStatusBar]
			  statusItemWithLength: NSSquareStatusItemLength];
	image = [NSImage imageNamed: @"status.png"];
	scaledImage = resizeImage2(image, NSMakeSize(18.0, 18.0));
	_statusItem.button.image = image;
	_statusItem.button.alternateImage = image;
	_statusItem.visible = YES;

	_menu = [[NSMenu alloc] init];
	_statusItem.menu = _menu;

	item = [[NSMenuItem alloc]
		   initWithTitle: @"Start"
		   action: @selector(startPressed)
		   keyEquivalent: (NSString *) @"s"];
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
}


- (void) startPressed {
    NSLog(@"start pressed!!");
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
