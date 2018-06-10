//
//  MFANQueryResults.h
//  MusicFan
//
//  Created by Michael Kazar on 5/4/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <Foundation/Foundation.h>

@interface MFANQueryResults : NSObject

/* test to see if a number is present */
- (BOOL) isPresent: (NSNumber *) keyp;

/* load up a collection of MPMediaItems into the dictionary for quick membership
 * testing.
 */

- (MFANQueryResults *) initWithResults: (NSArray *) queryResults
			     singleton: (MPMediaItem *) item
				  name: (NSString *)name
			   registerIds: (BOOL) doRegister;

- (NSArray *) queryResults;

- (void) addObject: (MPMediaItem *) itemp;

@property (readonly) NSString *name;

@end
