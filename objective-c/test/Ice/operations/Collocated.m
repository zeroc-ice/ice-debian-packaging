// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#import <objc/Ice.h>
#import <operations/TestI.h>
#import <TestCommon.h>

static int
run(id<ICECommunicator> communicator)
{
    [[communicator getProperties] setProperty:@"TestOperationsAdapter.Endpoints" value:@"default -p 12010"];
    id<ICEObjectAdapter> adapter = [communicator createObjectAdapter:@"TestOperationsAdapter"];
    id<ICEObjectPrx> prx = [adapter add:[TestOperationsMyDerivedClassI myDerivedClass]
             		       identity:[communicator stringToIdentity:@"test"]];
    //[adapter activate]; // Don't activate OA to ensure collocation is used.

    test(![prx ice_getConnection]);

    id<TestOperationsMyClassPrx> operationsAllTests(id<ICECommunicator>);
    operationsAllTests(communicator);

    return EXIT_SUCCESS;
}

#if TARGET_OS_IPHONE
#  define main operationsCollocated
#endif

int
main(int argc, char* argv[])
{
    int status;
    @autoreleasepool
    {
        id<ICECommunicator> communicator = nil;

        @try
        {
            ICEInitializationData* initData = [ICEInitializationData initializationData];
            initData.properties = defaultServerProperties(&argc, argv);

            [initData.properties setProperty:@"Ice.BatchAutoFlushSize" value:@"100"];
#if TARGET_OS_IPHONE
            initData.prefixTable__ = [NSDictionary dictionaryWithObjectsAndKeys:
                                      @"TestOperations", @"::Test",
                                      nil];
#endif
            communicator = [ICEUtil createCommunicator:&argc argv:argv initData:initData];
            status = run(communicator);
        }
        @catch(ICEException* ex)
        {
            tprintf("%@\n", ex);
            status = EXIT_FAILURE;
        }

        if(communicator)
        {
            @try
            {
                [communicator destroy];
            }
            @catch(ICEException* ex)
            {
                tprintf("%@\n", ex);
                status = EXIT_FAILURE;
            }
        }
    }

    return status;
}
