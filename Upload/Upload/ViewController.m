//
//  ViewController.m
//  Upload
//
//  Created by Michael Kazar on 5/27/18.
//  Copyright © 2018 Mike Kazar. All rights reserved.
//

#import "ViewController.h"

@implementation ViewController

WKWebView *_webViewp;
NSURL *_urlp;
NSURLRequest *_requestp;
WebView *_oldViewp;

- (void) loadView {
    [super loadView];

}

- (void)viewDidLoad {
    [super viewDidLoad];


    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: @"http://192.168.1.203:7701"]];

#if 0
#if 1
    // Do any additional setup after loading the view.
    NSWindow *windowp = [NSApplication sharedApplication].windows[0];
    WKWebViewConfiguration *theConfiguration = [[WKWebViewConfiguration alloc] init];
    WKWebView *_webViewp = [[WKWebView alloc] initWithFrame:self.view.frame
					      configuration:theConfiguration];
    _webViewp.navigationDelegate = self;

    _urlp=[NSURL URLWithString:@"http://apple.com"];
    _requestp=[NSURLRequest requestWithURL:_urlp];
    [_webViewp loadRequest:_requestp];
    // [self.view addSubview:_webViewp];
    self.view = _webViewp;
    NSLog(@"windowp=%@ wvp=%@", windowp, _webViewp);
    NSLog(@"vcp=%@ vcviewp=%@ contentviewp=%@", self, self.view, windowp.contentView);
#else
    // Do any additional setup after loading the view.
    WebView *_oldViewp = [[WebView alloc] initWithFrame:self.view.frame];
    NSWindow *windowp = [NSApplication sharedApplication].windows[0];

    _urlp=[NSURL URLWithString:@"https://google.com"];
    _requestp=[NSURLRequest requestWithURL:_urlp];
    [_oldViewp.mainFrame loadRequest:_requestp];
    self.view = _oldViewp;
    NSLog(@"vcp=%@ vcviewp=%@ webviewp=%@", self, self.view, _oldViewp);
#endif
#endif
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
