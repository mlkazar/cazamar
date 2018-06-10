/* Copyright (C) 2015 by Mike Kazar.  All Rights Reserved */

#import <UIKit/UIKit.h>
#import "MFANTopApp.h"
#import "MFANTopSettings.h"     /* for MFANLabel definition */

@class MFANTopRStar;

@interface MFANRStarLogin : UIView<UITextFieldDelegate>

- (MFANRStarLogin *) initWithFrame: (CGRect) frame
			     rstar: (MFANTopRStar *) rstar
		    viewController: (MFANViewController *) vc;

@end

@interface MFANRStarCreateAcct : UIView<UITextFieldDelegate>

- (MFANRStarCreateAcct *) initWithFrame: (CGRect) frame
				  rstar: (MFANTopRStar *) rstar
			 viewController: (MFANViewController *) vc;

@end

@interface MFANTopRStar : UIView<MFANTopApp, NSURLConnectionDelegate, NSURLConnectionDataDelegate>

- (NSString *) defaultHost;

- (MFANTopRStar *) initWithFrame: (CGRect) frame viewController: (MFANViewController *) vc;

/* called when we've received the response headers; may not happen on some errors */
- (void)connection:(NSURLConnection *)connection didReceiveResponse:(NSURLResponse *)response;

/* some data has arrived; must be copied or pointer reserved */
- (void)connection:(NSURLConnection *)connection didReceiveData:(NSData *)data;

/* called when the entire connection is done */
- (void)connectionDidFinishLoading:(NSURLConnection *)connection;

- (int32_t) createAcctForUser: (NSString *) user
			email: (NSString *) emailAddr
		     password: (NSString *) pass;
- (int32_t) loginEmail: (NSString *) email
	     password: (NSString *) pass
	    sessionId: (NSString *) session	/* OUT */
	   sessionKey: (NSString *) sessionKey;	/* OUT */
@end
