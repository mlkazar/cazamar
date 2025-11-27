//
//  MFANAppDelegate.m
//  MusicFan
//
//  Created by Michael Kazar on 4/19/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANAppDelegate.h"
#import "MFANSceneDelegate.h"
#import "MFANTopLevel.h"
#import "MFANViewController.h"

@implementation MFANAppDelegate {
    BOOL _isActive;
}

- (BOOL)application:(UIApplication *)application
didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    _isActive = YES;

#if 0
    [[UIApplication sharedApplication]
	setMinimumBackgroundFetchInterval: 15.0];
#endif
    [application setMinimumBackgroundFetchInterval: 15.0];

    return YES;
}

- (void)applicationWillTerminate:(UIApplication *)application
{
    // Called when the application is about to terminate. Save data if
    // appropriate. See also applicationDidEnterBackground:.
}

- (UISceneConfiguration *)application:(UIApplication *)application
    configurationForConnectingSceneSession:(UISceneSession *) connectingSceneSession
			      options:(UISceneConnectionOptions *) options {
    // Called when a new scene session is being created.  Use this
    // method to select a configuration to create the new scene with.
    return [[UISceneConfiguration alloc] initWithName:@"Default Configuration"
					  sessionRole:connectingSceneSession.role];
}


- (void) application:(UIApplication *) application
didDiscardSceneSessions:(NSSet<UISceneSession *> *) sceneSessions {
    // Called when the user discards a scene session.  If any sessions
    // were discarded while the application was not running, this will
    // be called shortly after
    // application:didFinishLaunchingWithOptions.  Use this method to
    // release any resources that were specific to the discarded
    // scenes, as they will not return.
}

@end

/* This allows the use of self-signed SSL certificates */
@implementation NSURLRequest(DataController) 
+ (BOOL)allowsAnyHTTPSCertificateForHost:(NSString *)host 
{ 
    return YES; 
} 
@end 
