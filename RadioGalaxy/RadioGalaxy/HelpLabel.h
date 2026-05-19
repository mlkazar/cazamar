#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#import "ViewController.h"

@interface HelpLabel : UIButton

- (HelpLabel *) initWithFrame: (CGRect) frame
		       target: (id) target
		     selector: (SEL) selector;


- (HelpLabel *) initWithTarget: (id) target selector:(SEL) selector;
@end
