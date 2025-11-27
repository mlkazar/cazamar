//
//  MFANAppDelegate.m
//  MusicFan
//
//  Created by Michael Kazar on 4/19/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANSceneDelegate.h"
#import "MFANTopLevel.h"
#import "MFANViewController.h"

@implementation MFANSceneDelegate {
    MFANViewController *_viewCon;
}

- (void) scene:(UIScene *) scene
willConnectToSession:(UISceneSession *) session
       options:(UISceneConnectionOptions *) connectionOptions {
    // Use this method to optionally configure and attach the UIWindow
    // `window` to the provided UIWindowScene `scene`.  If using a
    // storyboard, the `window` property will automatically be
    // initialized and attached to the scene.  This delegate does not
    // imply the connecting scene or session are new (see
    // `application:configurationForConnectingSceneSession` instead).
    _viewCon = [[MFANViewController alloc] init];
    self.window = [[UIWindow alloc] initWithWindowScene: (UIWindowScene *) scene];
    [self.window setRootViewController: _viewCon];
    [self.window makeKeyAndVisible];
}


- (void) sceneDidDisconnect:(UIScene *) scene {
    // Called as the scene is being released by the system.  This
    // occurs shortly after the scene enters the background, or when
    // its session is discarded.  Release any resources associated
    // with this scene that can be re-created the next time the scene
    // connects.  The scene may re-connect later, as its session was
    // not necessarily discarded (see
    // `application:didDiscardSceneSessions` instead).
}



- (void)sceneWillResignActive:(UIScene *)application
{
    // Sent when the application is about to move from active to
    // inactive state. This can occur for certain types of temporary
    // interruptions (such as an incoming phone call or SMS message)
    // or when the user quits the application and it begins the
    // transition to the background state.  Use this method to pause
    // ongoing tasks, disable timers, and throttle down OpenGL ES
    // frame rates. Games should use this method to pause the game.
    NSLog(@"- Scene becoming background");
    [_viewCon enterBackground];
}

- (void)sceneDidEnterBackground:(UIScene *)scene
{
    // Use this method to release shared resources, save user data,
    // invalidate timers, and store enough application state
    // information to restore your application to its current state in
    // case it is terminated later.  If your application supports
    // background execution, this method is called instead of
    // applicationWillTerminate: when the user quits.
}

- (void)sceneWillEnterForeground:(UIScene *)scene
{
    // Called as part of the transition from the background to the
    // inactive state; here you can undo many of the changes made on
    // entering the background.
}

- (void)sceneDidBecomeActive:(UIScene *)scene
{
    // Restart any tasks that were paused (or not yet started) while
    // the application was inactive. If the application was previously
    // in the background, optionally refresh the user interface.
    NSLog(@"- Scene leaving background");
    [_viewCon leaveBackground];
}

@end

/* This allows the use of self-signed SSL certificates */
@implementation NSURLRequest(DataController) 
+ (BOOL)allowsAnyHTTPSCertificateForHost:(NSString *)host 
{ 
    return YES; 
} 
@end 
