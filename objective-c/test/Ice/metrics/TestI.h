// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#import <MetricsTest.h>

@interface MetricsI : TestMetricsMetrics<TestMetricsMetrics>
-(id) init;
-(void) op:(ICECurrent*)current;
-(void) fail:(ICECurrent*)current;
-(void) opWithUserException:(ICECurrent*)current;
-(void) opWithRequestFailedException:(ICECurrent*)current;
-(void) opWithLocalException:(ICECurrent*)current;
-(void) opWithUnknownException:(ICECurrent*)current;
-(void) opByteS:(ICEByteSeq*)bs current:(ICECurrent*)current;
-(ICEObjectPrx*) getAdmin:(ICECurrent*)current;
-(void) shutdown:(ICECurrent*)current;
@end

@interface ControllerI : TestMetricsController<TestMetricsController>
{
@private
	id<ICEObjectAdapter> adapter;
}
-(id) init:(id<ICEObjectAdapter>)adapter_p;
-(void) hold:(ICECurrent*)current;
-(void) resume:(ICECurrent*)current;
@end
