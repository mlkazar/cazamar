#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import "SignStation.h"

@class ViewController;

@interface SearchStation : UIView<UITextViewDelegate,
    UITableViewDelegate,
    UITableViewDataSource,
    UISearchBarDelegate> 

@property bool canceled;

- (SearchStation *) initWithFrame:(CGRect) frame ViewCont: (ViewController *) vc;

- (void) setCallback: (id) object WithSel: (SEL) selector;

@end
