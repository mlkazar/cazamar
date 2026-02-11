#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import "SignStation.h"

@class ViewController;

@interface SearchStation : UIView<UITextViewDelegate,
    UITableViewDelegate,
    UITableViewDataSource,
    UISearchBarDelegate> 

@property bool canceled;
@property NSMutableArray *signStations;

- (SearchStation *) initWithFrame:(CGRect) frame ViewCont: (ViewController *) vc;

- (void) setCallback: (id) object WithSel: (SEL) selector;

@end

@interface SearchStationResults : NSObject
- (void) initWithSearchStation: (SearchStation *) search;

// returns nil when out of entries
- (SignStation *) getNext;
@end
