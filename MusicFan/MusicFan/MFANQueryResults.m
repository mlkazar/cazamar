//
//  MFANQueryResults.m
//  MusicFan
//
//  Created by Michael Kazar on 5/4/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <MediaPlayer/MediaPlayer.h>

#import "MFANQueryResults.h"

@implementation MFANQueryResults {
    NSMutableArray *_queryResults;	/* array of MPMediaItems */
    NSMutableDictionary *_idDictionary;/* key=persistent ID, value=MPMediaItem */
    /* name is a property */
}

- (NSArray *) queryResults
{
    return _queryResults;
}

/* test to see if a number is present */
- (BOOL) isPresent: (NSNumber *) key
{
    MPMediaItem *item;

    item = [_idDictionary objectForKey: key];
    return (item != nil);
}

/* load up a collection of MPMediaItems into the dictionary for quick membership
 * testing.
 */
- (MFANQueryResults *) initWithResults: (NSMutableArray *) queryResults
			     singleton: (MPMediaItem *) aitem
				  name: (NSString *)name
			   registerIds: (BOOL) doRegister
{
    MPMediaItem *item;
    NSNumber *persistentId;

    self = [super init];
    if (!self)
	return self;

    if (queryResults == nil) {
	_queryResults = [NSMutableArray arrayWithObject: aitem];
    }
    else {
	_queryResults = queryResults;
    }

    _name = name;

    if (doRegister) {
	for(item in _queryResults) {
	    persistentId = [item valueForProperty: MPMediaItemPropertyPersistentID];
	    if (persistentId != nil)
		[_idDictionary setObject: item forKey: persistentId];
	}
    }
    else {
	_idDictionary = nil;
    }

    return self;
}

- (void) addObject: (MPMediaItem *) item
{
    [_queryResults addObject: item];
}

@end
