// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#import <objc/Ice.h>
#import <TestCommon.h>
#import <AdapterDeactivationTest.h>

id<TestAdapterDeactivationTestIntfPrx>
adapterDeactivationAllTests(id<ICECommunicator> communicator)
{
    tprintf("testing stringToProxy... ");
    id<ICEObjectPrx> base = [communicator stringToProxy:@"test:default -p 12010"];
    test(base);
    tprintf("ok\n");

    tprintf("testing checked cast... ");
    id<TestAdapterDeactivationTestIntfPrx> obj = [TestAdapterDeactivationTestIntfPrx checkedCast:base];
    test(obj);
    test([obj isEqual:base]);
    tprintf("ok\n");

    {
        tprintf("creating/destroying/recreating object adapter... ");
        id<ICEObjectAdapter> adapter = 
            [communicator createObjectAdapterWithEndpoints:@"TransientTestAdapter" endpoints:@"default -p 9999"];
        @try
        {
            [communicator createObjectAdapterWithEndpoints:@"TransientTestAdapter" endpoints:@"default -p 9998"];
            test(NO);
        }
        @catch(ICEAlreadyRegisteredException*)
        {
        }
        [adapter destroy];

        //
        // Use a different port than the first adapter to avoid an @"address already in use" error.
        //
        adapter = [communicator createObjectAdapterWithEndpoints:@"TransientTestAdapter" endpoints:@"default -p 9998"];
        [adapter destroy];
        tprintf("ok\n");
    }

    tprintf("creating/activating/deactivating object adapter in one operation... ");
    [obj transient];
    tprintf("ok\n");

    tprintf("deactivating object adapter in the server... ");
    [obj deactivate];
    tprintf("ok\n");

    tprintf("testing whether server is gone... ");
    @try
    {
        [obj ice_ping];
        test(NO);
    }
    @catch(ICELocalException*)
    {
        tprintf("ok\n");
    }

    return obj;
}
