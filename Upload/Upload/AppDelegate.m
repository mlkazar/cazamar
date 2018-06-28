//
//  AppDelegate.m
//  Upload
//
//  Created by Michael Kazar on 6/28/18.
//  Copyright © 2018 Mike Kazar. All rights reserved.
//

#import "AppDelegate.h"
#import "ViewController.h"

@interface AppDelegate ()

@property (weak) IBOutlet NSWindow *window;
@end

ViewController *_vc;

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    // Insert code here to initialize your application
}


- (void)applicationWillTerminate:(NSNotification *)aNotification {
    // Insert code here to tear down your application
}

- (void) awakeFromNib {
    _vc = [[ViewController alloc] init];
}

@end
