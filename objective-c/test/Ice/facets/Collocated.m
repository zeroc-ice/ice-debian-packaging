// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#import <objc/Ice.h>
#import <facets/TestI.h>
#import <TestCommon.h>

static int
run(id<ICECommunicator> communicator)
{
    [[communicator getProperties] setProperty:@"TestAdapter.Endpoints" value:@"default -p 12010"];
    id<ICEObjectAdapter> adapter = [communicator createObjectAdapter:@"TestAdapter"];
    ICEObject* d = [TestFacetsDI d];
    ICEObject* f = [TestFacetsFI f];
    ICEObject* h = [TestFacetsHI h];
    [adapter add:d identity:[communicator stringToIdentity:@"d"]];
    [adapter addFacet:d identity:[communicator stringToIdentity:@"d"] facet:@"facetABCD"];
    [adapter addFacet:f identity:[communicator stringToIdentity:@"d"] facet:@"facetEF"];
    [adapter addFacet:h identity:[communicator stringToIdentity:@"d"] facet:@"facetGH"];

    id<TestFacetsGPrx> facetsAllTests(id<ICECommunicator>);
    facetsAllTests(communicator);

    return EXIT_SUCCESS;
}

#if TARGET_OS_IPHONE
#  define main facetsServer
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
#if TARGET_OS_IPHONE
            initData.prefixTable__ = [NSDictionary dictionaryWithObjectsAndKeys:
                                      @"TestFacets", @"::Test",
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
