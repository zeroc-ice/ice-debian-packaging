// **********************************************************************
//
// Copyright (c) 2003-2016 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package test.Ice.exceptions;

public class AMDServer extends test.Util.Application
{
    @Override
    public int run(String[] args)
    {
        Ice.Communicator communicator = communicator();
        Ice.ObjectAdapter adapter = communicator.createObjectAdapter("TestAdapter");
        Ice.ObjectAdapter adapter2 = communicator.createObjectAdapter("TestAdapter2");
        Ice.ObjectAdapter adapter3 = communicator.createObjectAdapter("TestAdapter3");
        Ice.Object object = new AMDThrowerI();
        adapter.add(object, communicator.stringToIdentity("thrower"));
        adapter2.add(object, communicator.stringToIdentity("thrower"));
        adapter3.add(object, communicator.stringToIdentity("thrower"));
        adapter.activate();
        adapter2.activate();
        adapter3.activate();
        return WAIT;
    }

    @Override
    protected Ice.InitializationData getInitData(Ice.StringSeqHolder argsH)
    {
        Ice.InitializationData initData = createInitializationData();
        //
        // For this test, we need a dummy logger, otherwise the
        // assertion test will print an error message.
        //
        initData.logger = new DummyLogger();

        initData.properties = Ice.Util.createProperties(argsH);
        initData.properties.setProperty("Ice.Warn.Dispatch", "0");
        initData.properties.setProperty("Ice.Warn.Connections", "0");
        initData.properties.setProperty("Ice.Package.Test", "test.Ice.exceptions.AMD");
        initData.properties.setProperty("TestAdapter.Endpoints", "default -p 12010:udp");
        initData.properties.setProperty("Ice.MessageSizeMax", "10"); // 10KB max
        initData.properties.setProperty("TestAdapter2.Endpoints", "default -p 12011");
        initData.properties.setProperty("TestAdapter2.MessageSizeMax", "0");
        initData.properties.setProperty("TestAdapter3.Endpoints", "default -p 12012");
        initData.properties.setProperty("TestAdapter3.MessageSizeMax", "1");

        return initData;
    }

    public static void main(String[] args)
    {
        AMDServer app = new AMDServer();
        int result = app.main("AMDServer", args);
        System.gc();
        System.exit(result);
    }
}
