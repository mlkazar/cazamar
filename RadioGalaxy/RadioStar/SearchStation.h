#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import "SignStation.h"
#import "ViewController.h"

@interface SearchStation : UIView<UITextViewDelegate,
    UITableViewDelegate,
    UITableViewDataSource,
    UISearchBarDelegate,
    UIPickerViewDataSource,
    UIPickerViewDelegate,
    TopViewInt>

@property bool canceled;
@property NSMutableArray *signStations;

- (void) activateTopView;

- (void) deactivateTopView;

- (SearchStation *) initWithFrame:(CGRect) frame ViewCont: (ViewController *) vc;

- (void) setCallback: (id) object WithSel: (SEL) selector;

@end

@interface SearchStationResults : NSObject
- (SearchStationResults *) initWithSearchStation: (SearchStation *) search;

// returns nil when out of entries
- (SignStation *) getNext;
@end
