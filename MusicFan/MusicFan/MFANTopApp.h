//
//  MFANTopApp.h
//  MusicFan
//
//  Created by Michael Kazar on 4/25/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

typedef enum {
    MFANChannelMusic = 0,
    MFANChannelPodcast,
    MFANChannelRadio,
    MFANChannelTypeCount        /* must be last */
} MFANChannelType;

@protocol MFANTopApp
-(void) activateTop;

-(void) deactivateTop;
@end

