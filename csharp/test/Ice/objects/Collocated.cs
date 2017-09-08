// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

using Test;
using System;
using System.Diagnostics;
using System.Reflection;

[assembly: CLSCompliant(true)]

[assembly: AssemblyTitle("IceTest")]
[assembly: AssemblyDescription("Ice test")]
[assembly: AssemblyCompany("ZeroC, Inc.")]

public class Client
{
    private static int run(string[] args, Ice.Communicator communicator)
    {
        communicator.getProperties().setProperty("TestAdapter.Endpoints", "default -p 12010");
        Ice.ObjectAdapter adapter = communicator.createObjectAdapter("TestAdapter");
        Initial initial = new InitialI(adapter);
        adapter.add(initial, communicator.stringToIdentity("initial"));
        UnexpectedObjectExceptionTestI uet = new UnexpectedObjectExceptionTestI();
        adapter.add(uet, communicator.stringToIdentity("uoet"));
        AllTests.allTests(communicator);
        // We must call shutdown even in the collocated case for cyclic dependency cleanup
        initial.shutdown();
        return 0;
    }

    public static int Main(string[] args)
    {
        int status = 0;
        Ice.Communicator communicator = null;

        try
        {
            Ice.InitializationData data = new Ice.InitializationData();
            data.properties = Ice.Util.createProperties();
#if COMPACT
            //
            // When using Ice for .NET Compact Framework, we need to specify
            // the assembly so that Ice can locate classes and exceptions.
            //
            data.properties.setProperty("Ice.FactoryAssemblies", "collocated");
#endif
            data.properties.setProperty("Ice.Warn.Dispatch", "0");
            data.properties.setProperty("Ice.ClassGraphDepthMax", "100");
            communicator = Ice.Util.initialize(ref args, data);
            status = run(args, communicator);
        }
        catch(System.Exception ex)
        {
            System.Console.WriteLine(ex);
            status = 1;
        }

        if(communicator != null)
        {
            try
            {
                communicator.destroy();
            }
            catch(Ice.LocalException ex)
            {
                System.Console.WriteLine(ex);
                status = 1;
            }
        }

        return status;
    }
}
