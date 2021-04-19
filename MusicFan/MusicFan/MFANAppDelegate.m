//
//  MFANAppDelegate.m
//  MusicFan
//
//  Created by Michael Kazar on 4/19/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANAppDelegate.h"
#import "MFANTopLevel.h"
#import "MFANViewController.h"

@implementation MFANAppDelegate {
    BOOL _isActive;
    MFANViewController *_viewCon;

    UIBackgroundTaskIdentifier _bkgId;
    NSTimer *_bkgTimer;
}

- (BOOL)application:(UIApplication *)application
didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    // Override point for customization after application launch.
    self.window.backgroundColor = [UIColor colorWithRed: 0.5 green: 0.5 blue: .49 alpha: 1.0];

    // create a view here
    // CGRect frame = [[UIScreen mainScreen] bounds];
    // self._topLevel = [[MFANTopLevel alloc] initWithFrame: frame];
    // [self.window addSubview: self._topLevel];

    _viewCon = [[MFANViewController alloc] init];
    self._controller = _viewCon;
    self.window.rootViewController = self._controller;

    [self.window makeKeyAndVisible];

    [[UIApplication sharedApplication]
	setMinimumBackgroundFetchInterval: 15.0];

    _isActive = YES;

    [application setMinimumBackgroundFetchInterval: 15.0];

    return YES;
}

- (void)applicationWillResignActive:(UIApplication *)application
{
    // Sent when the application is about to move from active to
    // inactive state. This can occur for certain types of temporary
    // interruptions (such as an incoming phone call or SMS message)
    // or when the user quits the application and it begins the
    // transition to the background state.  Use this method to pause
    // ongoing tasks, disable timers, and throttle down OpenGL ES
    // frame rates. Games should use this method to pause the game.
    NSLog(@"- App becoming background");
    [_viewCon enterBackground];
}

- (void)applicationDidEnterBackground:(UIApplication *)application
{
    // Use this method to release shared resources, save user data,
    // invalidate timers, and store enough application state
    // information to restore your application to its current state in
    // case it is terminated later.  If your application supports
    // background execution, this method is called instead of
    // applicationWillTerminate: when the user quits.
    /* we sleep for 5 seconds here to give any in progress status file
     * saves time to complete before our process stops.
     */
    _bkgId = [[UIApplication sharedApplication] beginBackgroundTaskWithExpirationHandler: ^ {
	    NSLog(@"beginBack magic block");
	}];
    _bkgTimer = [NSTimer scheduledTimerWithTimeInterval: 10.0
						 target: self
					       selector: @selector(bkgDone:)
					       userInfo: nil
						repeats: NO];
    NSLog(@"beginBackgroundTaskWithExpirationHander started timer for id=%d", _bkgId);
}

- (void) bkgDone: (id) context
{
    [[UIApplication sharedApplication] endBackgroundTask: _bkgId];
    NSLog(@"Background Timer: back from endBackgroundTask for id=%d", _bkgId);
}

- (void)applicationWillEnterForeground:(UIApplication *)application
{
    // Called as part of the transition from the background to the
    // inactive state; here you can undo many of the changes made on
    // entering the background.
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
    // Restart any tasks that were paused (or not yet started) while
    // the application was inactive. If the application was previously
    // in the background, optionally refresh the user interface.
    NSLog(@"- App leaving background");
    [_viewCon leaveBackground];
}

- (void)applicationWillTerminate:(UIApplication *)application
{
    // Called when the application is about to terminate. Save data if
    // appropriate. See also applicationDidEnterBackground:.
}

@end

/* This allows the use of self-signed SSL certificates */
@implementation NSURLRequest(DataController) 
+ (BOOL)allowsAnyHTTPSCertificateForHost:(NSString *)host 
{ 
    return YES; 
} 
@end 
