#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import "ViewController.h"

@interface ExportPlayer : UIView<TopViewInt, UITableViewDelegate, UITableViewDataSource>

- (ExportPlayer *) initWithViewCont: (ViewController *) vc;

- (void) activateTopView;

- (void) deactivateTopView;

@end
