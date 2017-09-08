// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#import <objc/Ice.h>
#import <ServantLocatorTest.h>


@interface TestServantLocatorTestIntfI : TestServantLocatorTestIntf<TestServantLocatorTestIntf>
@end

@interface TestServantLocatorCookieI : TestServantLocatorCookie<TestServantLocatorCookie>
@end
