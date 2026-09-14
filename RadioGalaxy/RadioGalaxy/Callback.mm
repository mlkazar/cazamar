#import "Callback.h"

#include <pthread.h>

@implementation CallbackEntry {
    NSObject *_callbackObj;
    SEL _callbackSel;
}

- (CallbackEntry *) initWithObj: (NSObject *) obj
			    sel: (SEL) sel {
    self = [super init];
    if (self != nil) {
	_callbackObj = obj;
	_callbackSel = sel;
    }
    return self;
}
@end

@implementation CallbackSet {
    NSMutableArray *_callbackEntries;
    pthread_mutex_t _mutex;
}

- (CallbackSet *) init {
    self = [super init];
    if (self != nil) {
	_callbackEntries = [[NSMutableArray alloc] init];
	pthread_mutex_init(&_mutex, nullptr);
    }
    return self;
}

- (void) addCallbackWithObj: (NSObject *) obj
			sel: (SEL) sel {
    CallbackEntry *tce;
    bool patched = false;
    pthread_mutex_lock(&_mutex);
    for(tce in _callbackEntries) {
	if (tce.callbackObj == obj && tce.callbackSel == sel) {
	    patched = true;
	    break;
	}
    }

    if (!patched) {
	CallbackEntry *ce = [[CallbackEntry alloc] initWithObj: obj
							   sel: sel];
	[_callbackEntries addObject: ce];
    }

    pthread_mutex_unlock(&_mutex);
}

// if sel is nullptr, match all instances with the right obj value
- (int32_t) removeCallbackWithObj: (NSObject *) obj
			      sel: (SEL) sel {
    uint32_t i;
    int32_t code = -1;
    uint32_t count = (uint32_t) [_callbackEntries count];
    CallbackEntry *ce;

    pthread_mutex_lock(&_mutex);
    for(i=0;i<count;i++) {
	ce = _callbackEntries[i];
	if (ce.callbackObj == obj && ce.callbackSel == sel) {
	    [_callbackEntries removeObjectAtIndex: i];
	    code = 0;
	    break;
	}
    }
    pthread_mutex_unlock(&_mutex);

    return code;
}

// This will get rid of our saved references, but if a callback is
// actually active now, the local callback array will keep references
// on everything as well.
- (void) shutdown {
    pthread_mutex_lock(&_mutex);
    _callbackEntries = nil;
    pthread_mutex_unlock(&_mutex);
}

- (bool) haveCallbacks {
    return ([_callbackEntries count] > 0);
}

- (void) applyWithParm: (NSObject *) parm {
    NSArray *callbacks;
    CallbackEntry *tce;

    pthread_mutex_lock(&_mutex);
    callbacks = [_callbackEntries copy];
    pthread_mutex_unlock(&_mutex);

    for(tce in callbacks) {
	dispatch_async(dispatch_get_main_queue(), ^{
		[tce.callbackObj performSelectorOnMainThread: tce.callbackSel
						  withObject: parm
					       waitUntilDone: true];
	    });
    } // for
}
@end

