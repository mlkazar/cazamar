//
//  MFANCGUtil.h
//  MusicFan
//
//  Created by Michael Kazar on 5/13/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>
#import <MediaPlayer/MediaPlayer.h>

#ifdef __cplusplus
extern "C" {
#endif

UIViewController *
currentViewController(void);

uint64_t
primeLE(uint64_t maxPrime);

uint32_t
parseRssDate(char *datep);

uint64_t
signature(NSString *str);

void
drawGradRectangle(CGContextRef cxp, CGRect rect, CGFloat *colors, int colorSets);

void
drawGlossy(CGContextRef cxp, CGRect rect, CGFloat *colors, int colorSets);

NSData *
silentData(int seconds);

UIImage *
mediaImageWithSize(MPMediaItem *item, CGSize newSize, UIImage *defaultImage);

UIImage *
resizeImage(UIImage* image, CGFloat newSide);

UIImage *
resizeImage2(UIImage* image, CGSize newSize);

UIImage *
traceImage(UIImage *patternImage, UIImage *shapeImage);

UIImage *
tintImage(UIImage *image, UIColor *color);

void
drawBackground(CGRect rect);

NSString *
fileNameForFile(NSString *fileName);

NSString *
fileNameForDoc(NSString *fileName);

#ifdef __cplusplus
}
#endif
