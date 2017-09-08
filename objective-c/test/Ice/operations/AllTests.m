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
#import <OperationsTest.h>

id<TestOperationsMyClassPrx>
operationsAllTests(id<ICECommunicator> communicator)
{
    NSString* ref = @"test:default -p 12010";
    id<ICEObjectPrx> base = [communicator stringToProxy:(ref)];
    id<TestOperationsMyClassPrx> cl = [TestOperationsMyClassPrx checkedCast:base];
    id<TestOperationsMyDerivedClassPrx> derived = [TestOperationsMyDerivedClassPrx checkedCast:cl];

    tprintf("testing twoway operations... ");
    void twoways(id<ICECommunicator>, id<TestOperationsMyClassPrx>);
    twoways(communicator, cl);
    twoways(communicator, derived);
    [derived opDerived];
    tprintf("ok\n");

    tprintf("testing oneway operations... ");
    void oneways(id<ICECommunicator>, id<TestOperationsMyClassPrx>);
    oneways(communicator, cl);
    tprintf("ok\n");

    tprintf("testing twoway operations with AMI... ");
    void twowaysNewAMI(id<ICECommunicator>, id<TestOperationsMyClassPrx>);
    twowaysNewAMI(communicator, cl);
    twowaysNewAMI(communicator, derived);
    tprintf("ok\n");

    tprintf("testing oneway operations with AMI... ");
    void onewaysNewAMI(id<ICECommunicator>, id<TestOperationsMyClassPrx>);
    onewaysNewAMI(communicator, cl);
    tprintf("ok\n");

    tprintf("testing batch oneway operations... ");
    void batchOneways(id<TestOperationsMyClassPrx>);
    batchOneways(cl);
    batchOneways(derived);
    tprintf("ok\n");

    tprintf("testing batch oneway operations with AMI... ");
    void batchOnewaysAMI(id<TestOperationsMyClassPrx>);
    batchOnewaysAMI(cl);
    batchOnewaysAMI(derived);
    tprintf("ok\n");

    return cl;
}
