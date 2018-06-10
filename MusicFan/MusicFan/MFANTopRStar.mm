//
//  MFANTopRStar.m
//  MusicFan
//
//  Created by Michael Kazar on 11//27/15
//  Copyri	ght (c) 2015 Mike Kazar. All rights reserved.
//

#import "MFANMetalButton.h"
#import "MFANViewController.h"
#import "MFANCGUtil.h"
#import "MFANRButton.h"
#import "MFANDownload.h"
#import "MFANWarn.h"
#import "MFANPopHelp.h"
#import "MFANRButton.h"
#import "MFANTopRStar.h"

#include "json.h"
#include "oauth.h"
#include <string>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/stat.h>

@implementation MFANRStarCreateAcct {
    MFANRButton *_cancelButton;
    MFANRButton *_createButton;
    MFANViewController *_viewCon;
    MFANTopRStar *_rstar;
    CGRect _frame;

    UITextField *_userName;
    UITextField *_email;
    UITextField *_pass1;
    UITextField *_pass2;
    NSString *_userNameStr;
    NSString *_emailStr;
    NSString *_pass1Str;
    NSString *_pass2Str;
    UILabel *_userNameLabel;
    UILabel *_emailLabel;
    UILabel *_pass1Label;
    UILabel *_pass2Label;
}

- (void) cancelPressed: (id) junk withData: (id) junk2
{
    [self removeFromSuperview];
}

- (void) createPressed: (id) junk withData: (id) junk2
{
    int32_t code;

    _userNameStr = [_userName text];
    _emailStr = [_email text];
    _pass1Str = [_pass1 text];
    _pass2Str = [_pass2 text];

    if (![_pass1Str isEqualToString: _pass2Str]) {
	(void) [[MFANWarn alloc] initWithTitle:@"Password Mismatch"
				 message: @"The two supplied passwords do not match"
				 secs: 4];
	
    }
    else {
	code = [_rstar createAcctForUser: _userNameStr
		       email: _emailStr
		       password: _pass1Str];
	if (code == 0) {
	    [self removeFromSuperview];
	}
	else {
	    (void) [[MFANWarn alloc] initWithTitle: @"Account creation failed"
				     message: @"The account couldn't be created"
				     secs: 4];
	}
    }
}

- (MFANRStarCreateAcct *) initWithFrame: (CGRect) frame
				  rstar: (MFANTopRStar *) rstar
			 viewController: (MFANViewController *) vc
{
    float buttonWidth = frame.size.width / 3;
    float buttonHeight = frame.size.height * 0.1;
    float buttonY;
    CGRect cancelButtonFrame;
    CGRect createButtonFrame;
    CGFloat fontSize = 12.0;
    CGRect textFrame;
    CGRect labelFrame;
    CGFloat interline;

    self = [super initWithFrame: frame];
    if (self) {
	_viewCon = vc;
	_frame = frame;
	_rstar = rstar;

	buttonY = frame.size.height - 1.1 * buttonHeight;
	textFrame.origin.x = frame.size.width * 0.05;
	textFrame.origin.y = frame.size.height / 8;
	textFrame.size.width = frame.size.width * 0.9;
	textFrame.size.height = frame.size.height/12;
	labelFrame = textFrame;
	labelFrame.origin.y += textFrame.size.height * 1.1;
	labelFrame.size.height = textFrame.size.height * 0.4;
	interline = 1.8 * textFrame.size.height;

	_userName = [[UITextField alloc] initWithFrame: textFrame];
	_userNameLabel = [[UILabel alloc] initWithFrame: labelFrame];
	[self addSubview: _userName];
	[self addSubview: _userNameLabel];
	_userNameLabel.text = @"User name";
	_userName.placeholder = @"User name";
	_userName.backgroundColor = [UIColor whiteColor];
	_userName.delegate = self;
	_userName.autocapitalizationType = UITextAutocapitalizationTypeNone;
	_userName.autocorrectionType = UITextAutocorrectionTypeNo;
	_userName.returnKeyType = UIReturnKeyNext;

	textFrame.origin.y += interline;
	labelFrame.origin.y += interline;
	_email = [[UITextField alloc] initWithFrame: textFrame];
	_emailLabel = [[UILabel alloc] initWithFrame: labelFrame];
	[self addSubview: _email];
	[self addSubview: _emailLabel];
	_emailLabel.text = @"Email address";
	_email.placeholder = @"user@host.com";
	_email.backgroundColor = [UIColor whiteColor];
	_email.delegate = self;
	_email.keyboardType = UIKeyboardTypeEmailAddress;
	_email.autocapitalizationType = UITextAutocapitalizationTypeNone;
	_email.autocorrectionType = UITextAutocorrectionTypeNo;
	_email.returnKeyType = UIReturnKeyNext;

	textFrame.origin.y += interline;
	labelFrame.origin.y += interline;
	_pass1 = [[UITextField alloc] initWithFrame: textFrame];
	_pass1Label = [[UILabel alloc] initWithFrame: labelFrame];
	[self addSubview: _pass1];
	[self addSubview: _pass1Label];
	_pass1Label.text = @"Password";
	_pass1.placeholder = @"Your password";
	_pass1.backgroundColor = [UIColor whiteColor];
	_pass1.delegate = self;
	_pass1.autocapitalizationType = UITextAutocapitalizationTypeNone;
	_pass1.autocorrectionType = UITextAutocorrectionTypeNo;
	_pass1.secureTextEntry = YES;
	_pass1.returnKeyType = UIReturnKeyNext;

	textFrame.origin.y += interline;
	labelFrame.origin.y += interline;
	_pass2 = [[UITextField alloc] initWithFrame: textFrame];
	_pass2Label = [[UILabel alloc] initWithFrame: labelFrame];
	[self addSubview: _pass2];
	[self addSubview: _pass2Label];
	_pass2Label.text = @"Confirm Password";
	_pass2.placeholder = @"Your password";
	_pass2.backgroundColor = [UIColor whiteColor];
	_pass2.delegate = self;
	_pass2.autocapitalizationType = UITextAutocapitalizationTypeNone;
	_pass2.autocorrectionType = UITextAutocorrectionTypeNo;
	_pass2.secureTextEntry = YES;
	_pass2.returnKeyType = UIReturnKeyDone;

	textFrame.origin.y += interline;
	labelFrame.origin.y += interline;

	/* define create button */
	createButtonFrame.origin.x = frame.size.width * 1.0 / 4.0 - buttonWidth/2;
	createButtonFrame.origin.y = buttonY;
	createButtonFrame.size.width = buttonWidth;
	createButtonFrame.size.height = buttonHeight;

	_createButton = [[MFANRButton alloc] initWithFrame: createButtonFrame
					     title: @"Create Acct"
					     color: [UIColor colorWithRed: 0.5
							     green: 0.5
							     blue: 1.0
							     alpha: 1.0]
					     fontSize: fontSize];
	[_createButton addCallback: self
		       withAction: @selector(createPressed:withData:)
		       value: nil];
	[self addSubview: _createButton];

	/* define cancel button */
	cancelButtonFrame.origin.x = frame.size.width * 3.0 / 4.0 - buttonWidth/2;
	cancelButtonFrame.origin.y = buttonY;
	cancelButtonFrame.size.width = buttonWidth;
	cancelButtonFrame.size.height = buttonHeight;

	_cancelButton = [[MFANRButton alloc] initWithFrame: cancelButtonFrame
					     title: @"Cancel"
					     color: [UIColor redColor]
					     fontSize: fontSize];
	[_cancelButton addCallback: self
		       withAction: @selector(cancelPressed:withData:)
		       value: nil];
	[self addSubview: _cancelButton];

	[self setBackgroundColor: [UIColor colorWithHue:0.0
					   saturation: 0.0
					   brightness: 0.6
					   alpha: 1.0]];

	/* do we have to do next after subview add? */
	self.layer.zPosition = 2;
    }

    return self;
}

- (BOOL) textFieldShouldReturn: (UITextField *) textField
{
    [textField resignFirstResponder];
    if (textField == _userName)
	[_email becomeFirstResponder];
    else if (textField == _email)
	[_pass1 becomeFirstResponder];
    else if (textField == _pass1)
	[_pass2 becomeFirstResponder];
    return YES;
}

@end

@implementation MFANRStarLogin {
    MFANRButton *_cancelButton;
    MFANRButton *_loginButton;
    MFANViewController *_viewCon;
    MFANTopRStar *_rstar;
    CGRect _frame;

    UITextField *_email;
    UITextField *_pass;
    NSString *_emailStr;
    NSString *_passStr;
    UILabel *_emailLabel;
    UILabel *_passLabel;

    NSString *_sessionIdStr;
    NSString *_sessionKeyStr;
}

- (void) cancelPressed: (id) junk withData: (id) junk2
{
    [self removeFromSuperview];
}

- (void) loginPressed: (id) junk withData: (id) junk2
{
    int32_t code;

    _emailStr = [_email text];
    _passStr = [_pass text];

    code = [_rstar loginEmail: _emailStr
		   password: _passStr
		   sessionId: _sessionIdStr
		   sessionKey: _sessionKeyStr];
    if (code == 0) {
	[self removeFromSuperview];
    }
    else {
	(void) [[MFANWarn alloc] initWithTitle: @"Login Failed"
				 message: @"Email or password is wrong"
				 secs: 4];
    }

}

- (MFANRStarLogin *) initWithFrame: (CGRect) frame
			     rstar: (MFANTopRStar *) rstar
		    viewController: (MFANViewController *) vc
{
    float buttonWidth = frame.size.width / 3;
    float buttonHeight = frame.size.height * 0.1;
    float buttonY = frame.size.height * 2 / 3;
    CGRect cancelButtonFrame;
    CGRect loginButtonFrame;
    CGFloat fontSize = 10.0;
    CGRect labelFrame;
    CGRect textFrame;
    CGFloat interline;

    self = [super initWithFrame: frame];
    if (self) {
	_viewCon = vc;
	_frame = frame;
	_rstar = rstar;

	_sessionIdStr = [[NSString alloc] init];
	_sessionKeyStr = [[NSString alloc] init];

	buttonY = frame.size.height - 1.1 * buttonHeight;
	textFrame.origin.x = frame.size.width * 0.05;
	textFrame.origin.y = frame.size.height / 8;
	textFrame.size.width = frame.size.width * 0.9;
	textFrame.size.height = frame.size.height/12;
	labelFrame = textFrame;
	labelFrame.origin.y += textFrame.size.height * 1.1;
	labelFrame.size.height = textFrame.size.height * 0.4;
	interline = 1.8 * textFrame.size.height;

	_email = [[UITextField alloc] initWithFrame: textFrame];
	_emailLabel = [[UILabel alloc] initWithFrame: labelFrame];
	[self addSubview: _email];
	[self addSubview: _emailLabel];
	_emailLabel.text = @"Email";
	_email.placeholder = @"Email";
	_email.backgroundColor = [UIColor whiteColor];
	_email.delegate = self;
	_email.autocapitalizationType = UITextAutocapitalizationTypeNone;
	_email.autocorrectionType = UITextAutocorrectionTypeNo;
	_email.returnKeyType = UIReturnKeyNext;

	textFrame.origin.y += interline;
	labelFrame.origin.y += interline;
	_pass = [[UITextField alloc] initWithFrame: textFrame];
	_passLabel = [[UILabel alloc] initWithFrame: labelFrame];
	[self addSubview: _pass];
	[self addSubview: _passLabel];
	_passLabel.text = @"Password";
	_pass.placeholder = @"Your password";
	_pass.backgroundColor = [UIColor whiteColor];
	_pass.delegate = self;
	_pass.autocapitalizationType = UITextAutocapitalizationTypeNone;
	_pass.autocorrectionType = UITextAutocorrectionTypeNo;
	_pass.secureTextEntry = YES;
	_pass.returnKeyType = UIReturnKeyDone;

	/* define login button */
	loginButtonFrame.origin.x = frame.size.width * 1.0 / 4.0 - buttonWidth/2;
	loginButtonFrame.origin.y = buttonY;
	loginButtonFrame.size.width = buttonWidth;
	loginButtonFrame.size.height = buttonHeight;

	_loginButton = [[MFANRButton alloc] initWithFrame: loginButtonFrame
					    title: @"Login"
					    color: [UIColor greenColor]
					    fontSize: fontSize];
	[_loginButton addCallback: self
		      withAction: @selector(loginPressed:withData:)
		      value: nil];
	[self addSubview: _loginButton];

	/* define cancel button */
	cancelButtonFrame.origin.x = frame.size.width * 3.0 / 4.0 - buttonWidth/2;
	cancelButtonFrame.origin.y = buttonY;
	cancelButtonFrame.size.width = buttonWidth;
	cancelButtonFrame.size.height = buttonHeight;

	_cancelButton = [[MFANRButton alloc] initWithFrame: cancelButtonFrame
					     title: @"Cancel"
					     color: [UIColor redColor]
					     fontSize: fontSize];
	[_cancelButton addCallback: self
		       withAction: @selector(cancelPressed:withData:)
		       value: nil];
	[self addSubview: _cancelButton];

	[self setBackgroundColor: [UIColor colorWithHue:0.0
					   saturation: 0.0
					   brightness: 0.6
					   alpha: 1.0]];

	/* do we have to do next after subview add? */
	self.layer.zPosition = 2;
    }

    return self;
}

- (BOOL) textFieldShouldReturn: (UITextField *) textField
{
    [textField resignFirstResponder];
    if (textField == _email)
	[_pass becomeFirstResponder];
    return YES;
}

@end

@implementation MFANTopRStar {
    MFANViewController *_viewCon;
    CGRect _frame;
    UIAlertView *_hostAlertView;
    MFANRButton *_loginButton;
    MFANRButton *_createAcctButton;
    MFANRButton *_cancelButton;
    NSString *_defaultHost;
    char _message[1024];
}

- (int32_t) loginEmail: (NSString *) email
	     password: (NSString *) pass
	    sessionId: (NSString *) session	/* OUT */
	   sessionKey: (NSString *) sessionKey	/* OUT */
{
    NSMutableURLRequest *urlReq;
    NSURLResponse *urlResp;
    NSError *urlError;
    NSURL *url;
    NSData *data;
    NSString *urlString;
    NSData *bodyData;
    char tbuffer[100];
    char *encodedPassp;
    Json::Node *rootNodep;
    Json::Node *tempNodep;
    Json::Node *nameNodep;
    Json::Node *parmNodep;
    std::string unparsedDataStr;
    NSString *unparsedData;

    urlString = [NSString stringWithFormat: @"http://%@:8234/api",
			  [self defaultHost]];
    NSLog(@"url = %@", urlString);
    url = [NSURL URLWithString: urlString];
    urlReq = [NSMutableURLRequest requestWithURL: url];
    urlReq.HTTPMethod = @"POST";

    /* next, generate the call: a struct with a named "op" field, a "ts" field
     * with an unquoted decimal second since 1970 (Unix GMT time), and a "parm" field
     * giving a json struct with a set of named parameters.
     *
     * For login, we have a string email address ("email") and a base64
     * hmac-sha1 of a string constructed from the password
     * ("password").
     *
     * Normal RPCs also include a base64 encoded signature over the
     * body of the call, but login doesn't have a shared secret yet
     * (that's the returned session key), so it uses https to provide
     * security instead.
     */
    rootNodep = new Json::Node();
    rootNodep->initStruct();

    /* marshal opcode */
    tempNodep = new Json::Node();
    tempNodep->initString("loginUser", 1);
    nameNodep = new Json::Node();
    nameNodep->initNamed("op", tempNodep);
    rootNodep->appendChild(nameNodep);

    /* marshal call timestamp as part of RPC basic header */
    sprintf(tbuffer, "%ld", time(0));
    tempNodep = new Json::Node();
    tempNodep->initString(tbuffer, 0);
    nameNodep = new Json::Node();
    nameNodep->initNamed("ts", tempNodep);
    rootNodep->appendChild(nameNodep);

    /* create parameters node */
    parmNodep = new Json::Node();
    parmNodep->initStruct();
    nameNodep = new Json::Node();
    nameNodep->initNamed("parm", parmNodep);
    rootNodep->appendChild(nameNodep);

    tempNodep = new Json::Node();
    tempNodep->initString([email cStringUsingEncoding: NSUTF8StringEncoding], 1);
    nameNodep = new Json::Node();
    nameNodep->initNamed("email", tempNodep);
    parmNodep->appendChild(nameNodep);

    tempNodep = new Json::Node();
    encodedPassp = oauth_encode_pass([pass cStringUsingEncoding: NSUTF8StringEncoding],
				     [email cStringUsingEncoding: NSUTF8StringEncoding]);
    tempNodep->initString(encodedPassp, 1);
    nameNodep = new Json::Node();
    nameNodep->initNamed("password", tempNodep);
    parmNodep->appendChild(nameNodep);
    free(encodedPassp);	/* string was allocated by oauth_encode_pass */

    /* next, unmarshal the tree into a string, and turn it into a data object */
    rootNodep->unparse(&unparsedDataStr); /* builds an std::string */
    unparsedData = [NSString stringWithCString: unparsedDataStr.c_str()
			     encoding: NSUTF8StringEncoding]; /* now it's an NSString */
    bodyData = [unparsedData dataUsingEncoding: NSUTF8StringEncoding]; /* now its data */

    [urlReq setValue: @"x-cazamar-rpc-signature" forHTTPHeaderField: @"0"];
    urlReq.HTTPBody = bodyData;
    urlReq.timeoutInterval = 20.0;

    NSLog(@"- about to call");
    data = [NSURLConnection sendSynchronousRequest: urlReq
			    returningResponse: &urlResp
			    error: &urlError];
    NSLog(@"- done error=%p", urlError);

    if (urlError == nil) {
	strncpy(_message, (char *)[data bytes], sizeof(_message));
	_message[[data length]] = 0;
	UIAlertView *talert = [[UIAlertView alloc]
				  initWithTitle:@"Received data"
				  message: [NSString stringWithUTF8String: _message]
				  delegate:nil 
				  cancelButtonTitle:@"OK"
				  otherButtonTitles: nil];
	[talert show];
    }

    return 0;
}

/* TODO: make this a real login call */
- (int32_t) createAcctForUser: (NSString *) user
			email: (NSString *) emailAddr
		     password: (NSString *) pass
{
    NSMutableURLRequest *urlReq;
    NSURLResponse *urlResp;
    NSError *urlError;
    NSURL *url;
    NSData *data;
    NSString *urlString;
    NSData *bodyData;
    char tbuffer[100];
    char *encodedPassp;
    Json::Node *rootNodep;
    Json::Node *tempNodep;
    Json::Node *nameNodep;
    Json::Node *parmNodep;
    std::string unparsedDataStr;
    NSString *unparsedData;

    urlString = [NSString stringWithFormat: @"http://%@:8234/api",
			  [self defaultHost]];
    NSLog(@"url = %@", urlString);
    url = [NSURL URLWithString: urlString];
    urlReq = [NSMutableURLRequest requestWithURL: url];
    urlReq.HTTPMethod = @"POST";

    /* next, generate the call: a struct with a named "op" field, a "ts" field
     * with an unquoted decimal second since 1970 (Unix GMT time), and a "parm" field
     * giving a json struct with a set of named parameters.
     *
     * For account creation, we have a string user name ("user") and
     * email address ("email"), and a base64 hmac-sha1 of a string
     * constructed from the password ("password").
     *
     * Normal RPCs also include a base64 encoded signature over the body of the
     * call, but account creation doesn't have a shared secret yet, so uses
     * https to provide security instead.
     */
    rootNodep = new Json::Node();
    rootNodep->initStruct();

    /* marshal opcode */
    tempNodep = new Json::Node();
    tempNodep->initString("createAcct", 1);
    nameNodep = new Json::Node();
    nameNodep->initNamed("op", tempNodep);
    rootNodep->appendChild(nameNodep);

    /* marshal call timestamp as part of RPC basic header */
    sprintf(tbuffer, "%ld", time(0));
    tempNodep = new Json::Node();
    tempNodep->initString(tbuffer, 0);
    nameNodep = new Json::Node();
    nameNodep->initNamed("ts", tempNodep);
    rootNodep->appendChild(nameNodep);

    /* create parameters node */
    parmNodep = new Json::Node();
    parmNodep->initStruct();
    nameNodep = new Json::Node();
    nameNodep->initNamed("parm", parmNodep);
    rootNodep->appendChild(nameNodep);

    tempNodep = new Json::Node();
    tempNodep->initString([user cStringUsingEncoding: NSUTF8StringEncoding], 1);
    nameNodep = new Json::Node();
    nameNodep->initNamed("user", tempNodep);
    parmNodep->appendChild(nameNodep);

    tempNodep = new Json::Node();
    tempNodep->initString([emailAddr cStringUsingEncoding: NSUTF8StringEncoding], 1);
    nameNodep = new Json::Node();
    nameNodep->initNamed("email", tempNodep);
    parmNodep->appendChild(nameNodep);

    tempNodep = new Json::Node();
    encodedPassp = oauth_encode_pass([pass cStringUsingEncoding: NSUTF8StringEncoding],
				     [emailAddr cStringUsingEncoding: NSUTF8StringEncoding]);
    tempNodep->initString(encodedPassp, 1);
    nameNodep = new Json::Node();
    nameNodep->initNamed("password", tempNodep);
    parmNodep->appendChild(nameNodep);
    free(encodedPassp);	/* string was allocated by oauth_encode_pass */

    /* next, unmarshal the tree into a string, and turn it into a data object */
    rootNodep->unparse(&unparsedDataStr); /* builds an std::string */
    unparsedData = [NSString stringWithCString: unparsedDataStr.c_str()
			     encoding: NSUTF8StringEncoding]; /* now it's an NSString */
    bodyData = [unparsedData dataUsingEncoding: NSUTF8StringEncoding]; /* now its data */

    [urlReq setValue: @"x-cazamar-rpc-signature" forHTTPHeaderField: @"0"];
    urlReq.HTTPBody = bodyData;
    urlReq.timeoutInterval = 20.0;

    NSLog(@"- about to call");
    data = [NSURLConnection sendSynchronousRequest: urlReq
			    returningResponse: &urlResp
			    error: &urlError];
    NSLog(@"- done error=%p", urlError);

    if (urlError == nil) {
	strncpy(_message, (char *)[data bytes], sizeof(_message));
	_message[[data length]] = 0;
	UIAlertView *talert = [[UIAlertView alloc]
				  initWithTitle:@"Received data"
				  message: [NSString stringWithUTF8String: _message]
				  delegate:nil 
				  cancelButtonTitle:@"OK"
				  otherButtonTitles: nil];
	[talert show];
    }

    return 0;
}

- (NSString *) defaultHost
{
    return _defaultHost;
}

- (void) checkHost
{
    UITextField *alertTextField;
    NSString *hostString;

    alertTextField = [_hostAlertView textFieldAtIndex: 0];
    hostString = [alertTextField text];
    _defaultHost = hostString;
}

- (void) alertView:(UIAlertView *) alert clickedButtonAtIndex:(NSInteger) buttonIndex
{
    if (buttonIndex == 1) {
	/* pressed OK */
	if (alert == _hostAlertView) {
	    [self checkHost];
	    _hostAlertView = nil;
	}
    }
}

- (MFANTopRStar *) initWithFrame: (CGRect) frame viewController: (MFANViewController *) vc
{
    float buttonWidth = frame.size.width / 3;
    float buttonHeight = frame.size.height * 0.1;
    CGRect createAcctFrame;
    CGRect loginFrame;
    CGRect cancelFrame;

    self = [super initWithFrame: frame];
    if (self) {
	_viewCon = vc;
	_frame = frame;

	/* set some defaults */
	_defaultHost = @"192.168.1.12";

	/* define schedule button */
	loginFrame.origin.x = frame.size.width * 1.0 / 4.0 - buttonWidth/2;
	loginFrame.origin.y = frame.size.height - 1.5 * buttonHeight;
	loginFrame.size.width = buttonWidth;
	loginFrame.size.height = buttonHeight;

	_loginButton = [[MFANRButton alloc] initWithFrame: loginFrame
					    title: @"Login"
					    color: [UIColor colorWithRed: 0.5
							    green: 0.5
							    blue: 1.0
							    alpha: 1.0]
					    fontSize: 14];
	[_loginButton addCallback: self
		      withAction: @selector(loginPressed:withData:)
		      value: nil];
	[self addSubview: _loginButton];
	
	/* define createAcct button */
	createAcctFrame.origin.x = frame.size.width * 2.0 / 4.0 - buttonWidth/2;
	createAcctFrame.origin.y = frame.size.height - 1.5 * buttonHeight;
	createAcctFrame.size.width = buttonWidth;
	createAcctFrame.size.height = buttonHeight;

	_createAcctButton = [[MFANRButton alloc] initWithFrame: createAcctFrame
						 title: @"Create Acct"
						 color: [UIColor colorWithRed: 0.5
								 green: 0.5
								 blue: 1.0
								 alpha: 1.0]
						 fontSize: 14];
	[_createAcctButton addCallback: self
			   withAction: @selector(createAcctPressed:withData:)
			   value: nil];
	[self addSubview: _createAcctButton];
	
	/* define show button */
	cancelFrame.origin.x = frame.size.width * 3.0 / 4.0 - buttonWidth/2;
	cancelFrame.origin.y = frame.size.height - 1.5 * buttonHeight;
	cancelFrame.size.width = buttonWidth;
	cancelFrame.size.height = buttonHeight;

	_cancelButton = [[MFANRButton alloc] initWithFrame: cancelFrame
					     title: @"Cancel"
					     color: [UIColor redColor]
					     fontSize: 14];
	[_cancelButton addCallback: self
		       withAction: @selector(cancelPressed:withData:)
		       value: nil];
	[self addSubview: _cancelButton];
    }

    return self;
}

-(void) activateTop
{ 
    UITextField *alertTextField;

    _hostAlertView = [[UIAlertView alloc]
			 initWithTitle:@"Destination"
			 message:@"Target host addr or name"
			 delegate:self
			 cancelButtonTitle:@"Cancel"
			 otherButtonTitles:@"OK", nil];
    [_hostAlertView setAlertViewStyle: UIAlertViewStylePlainTextInput];
    alertTextField = [_hostAlertView textFieldAtIndex: 0];
    alertTextField.text = _defaultHost;
    [_hostAlertView show];    
}

-(void) deactivateTop
{
    return;
}

- (void) loginPressed: (id) junk withData: (id) junk2
{
    MFANRStarLogin *login;

    login = [[MFANRStarLogin alloc] initWithFrame: _frame
				    rstar: self
				    viewController: _viewCon];
    [self addSubview: login];
}

- (void) createAcctPressed: (id) junk withData: (id) junk2
{
    MFANRStarCreateAcct *create;

    create = [[MFANRStarCreateAcct alloc] initWithFrame: _frame
					  rstar: self
					  viewController: _viewCon];
    [self addSubview: create];
}

- (void) cancelPressed: (id) junk withData: (id) junk2
{
    [_viewCon switchToAppByName: @"main"];
}

- (void) connection: (NSURLConnection *)connection
 didReceiveResponse: (NSURLResponse *)response
{
    NSLog(@"in didreceiveresponse");
}

- (void) connectionDidFinishLoading: (NSURLConnection *)connection 
{
    NSLog(@"in didfinishloading");
}

- (void) connection: (NSURLConnection *)connection
     didReceiveData: (NSData *)data
{
    NSLog(@"in didreceivedata");
}
@end
