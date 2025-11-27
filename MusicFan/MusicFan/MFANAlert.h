//
//  MFANAlert.h
//  DJ To Go
//
//  Created by Michael Kazar on 11/27/25.
//  Copyright © 2025 Mike Kazar. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@class UIAlertAction;

@interface MFANAlert : UIView

- (instancetype)initWithTitle:(NSString *)title
                      message:(NSString *)message
                  buttonTitle:(NSString *)buttonTitle;

- (instancetype)initWithTitleEx:(NSString *)title
		      messageEx:(NSString *)message
		  buttonTitleEx:(NSString *)buttonTitle
		      handlerEx:(void (^)(UIAlertAction *action))handler;

- (void)show;

- (void)dismiss;

@end

NS_ASSUME_NONNULL_END
