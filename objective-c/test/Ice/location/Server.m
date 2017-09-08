// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#import <objc/Ice.h>
#import <location/ServerLocator.h>
#import <location/TestI.h>
#import <TestCommon.h>

static int
run(id<ICECommunicator> communicator, ICEInitializationData* initData)
{
    //
    // Register the server manager. The server manager creates a new
    // 'server' (a server isn't a different process, it's just a new
    // communicator and object adapter).
    //
    [[communicator getProperties] setProperty:@"Ice.ThreadPool.Server.Size" value:@"2"];
    [[communicator getProperties] setProperty:@"ServerManager.Endpoints" value:@"default -p 12010:udp"];

    id<ICEObjectAdapter> adapter = [communicator createObjectAdapter:@"ServerManager"];

    //
    // We also register a sample server locator which implements the
    // locator interface, this locator is used by the clients and the
    // 'servers' created with the server manager interface.
    //
    ServerLocatorRegistry* registry = ICE_AUTORELEASE([[ServerLocatorRegistry alloc] init]);
    ServerManagerI* serverManager = ICE_AUTORELEASE([[ServerManagerI alloc] init:registry initData:initData]);

    [registry addObject:[adapter createProxy:[communicator stringToIdentity:@"ServerManager"]]];
    [adapter add:serverManager identity:[communicator stringToIdentity:@"ServerManager"]];

    id<ICELocatorRegistryPrx> registryPrx =
        [ICELocatorRegistryPrx uncheckedCast:[adapter add:registry
                                                      identity:[communicator stringToIdentity:@"registry"]]];

    ServerLocator* locator = ICE_AUTORELEASE([[ServerLocator alloc] init:registry proxy:registryPrx]);
    [adapter add:locator identity:[communicator stringToIdentity:@"locator"]];

    [adapter activate];

    serverReady(communicator);

    [communicator waitForShutdown];
    [serverManager terminate];

    return EXIT_SUCCESS;
}

#if TARGET_OS_IPHONE
#  define main locationServer
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
                                      @"TestLocation", @"::Test",
                                      nil];
#endif
            communicator = [ICEUtil createCommunicator:&argc argv:argv initData:initData];
            status = run(communicator, initData);
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
