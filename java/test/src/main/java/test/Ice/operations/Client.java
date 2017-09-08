// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package test.Ice.operations;

import test.Ice.operations.Test.MyClassPrx;

public class Client extends test.Util.Application
{
    @Override
    public int run(String[] args)
    {
        java.io.PrintWriter out = getWriter();
        MyClassPrx myClass = AllTests.allTests(this, out);

        out.print("testing server shutdown... ");
        out.flush();
        myClass.shutdown();
        try
        {
            myClass.opVoid();
            throw new RuntimeException();
        }
        catch(Ice.LocalException ex)
        {
            out.println("ok");
        }

        return 0;
    }

    @Override
    protected Ice.InitializationData getInitData(Ice.StringSeqHolder argsH)
    {
        Ice.InitializationData initData = createInitializationData() ;
        initData.properties = Ice.Util.createProperties(argsH);
        initData.properties.setProperty("Ice.ThreadPool.Client.Size", "2");
        initData.properties.setProperty("Ice.ThreadPool.Client.SizeWarn", "0");
        initData.properties.setProperty("Ice.Package.Test", "test.Ice.operations");
        initData.properties.setProperty("Ice.BatchAutoFlushSize", "100");
        return initData;
    }

    public static void main(String[] args)
    {
        Client c = new Client();
        int status = c.main("Client", args);

        System.gc();
        System.exit(status);
    }
}
