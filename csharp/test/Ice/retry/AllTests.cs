// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

using System;
using System.Diagnostics;
using System.Threading;

#if SILVERLIGHT
using System.Windows.Controls;
#endif

public class AllTests : TestCommon.TestApp
{
    private class Callback
    {
        internal Callback()
        {
            _called = false;
        }

        public void check()
        {
            lock(this)
            {
                while(!_called)
                {
                    System.Threading.Monitor.Wait(this);
                }

                _called = false;
            }
        }

        public void called()
        {
            lock(this)
            {
                Debug.Assert(!_called);
                _called = true;
                System.Threading.Monitor.Pulse(this);
            }
        }

        private bool _called;
    }

#if SILVERLIGHT
    public override Ice.InitializationData initData()
    {
        Ice.InitializationData initData = new Ice.InitializationData();
        initData.properties = Ice.Util.createProperties();
        initData.observer = Instrumentation.getObserver();

        initData.properties.setProperty("Ice.RetryIntervals", "0 1 10 1");

        //
        // This test kills connections, so we don't want warnings.
        //
        initData.properties.setProperty("Ice.Warn.Connections", "0");
        return initData;
    }

    public override void run(Ice.Communicator communicator)
    {
        //
        // Configure a second communicator for the invocation timeout
        // + retry test, we need to configure a large retry interval
        // to avoid time-sensitive failures.
        //
        Ice.InitializationData initData2 = new Ice.InitializationData();
        initData2.properties = communicator.getProperties().ice_clone_();
        initData2.properties.setProperty("Ice.RetryIntervals", "0 1 10000");
        initData2.observer = Instrumentation.getObserver();
        Ice.Communicator communicator2 = Ice.Util.initialize(initData2);
        try
        {
            allTests(communicator, communicator2, "retry:default -p 12010");
        }
        finally
        {
            communicator2.destroy();
        }
    }
    public void
#else
    static public Test.RetryPrx
#endif
    allTests(Ice.Communicator communicator, Ice.Communicator communicator2, string rf)
    {
        Write("testing stringToProxy... ");
        Flush();
        Ice.ObjectPrx base1 = communicator.stringToProxy(rf);
        test(base1 != null);
        Ice.ObjectPrx base2 = communicator.stringToProxy(rf);
        test(base2 != null);
        WriteLine("ok");

        Write("testing checked cast... ");
        Flush();
        Test.RetryPrx retry1 = Test.RetryPrxHelper.checkedCast(base1);
        test(retry1 != null);
        test(retry1.Equals(base1));
        Test.RetryPrx retry2 = Test.RetryPrxHelper.checkedCast(base2);
        test(retry2 != null);
        test(retry2.Equals(base2));
        WriteLine("ok");

        Write("calling regular operation with first proxy... ");
        Flush();
        retry1.op(false);
        WriteLine("ok");

        Instrumentation.testInvocationCount(3);

        Write("calling operation to kill connection with second proxy... ");
        Flush();
        try
        {
            retry2.op(true);
            test(false);
        }
        catch(Ice.UnknownLocalException)
        {
            // Expected with collocation
        }
        catch(Ice.ConnectionLostException)
        {
        }
        Instrumentation.testInvocationCount(1);
        Instrumentation.testFailureCount(1);
        Instrumentation.testRetryCount(0);
        WriteLine("ok");

        Write("calling regular operation with first proxy again... ");
        Flush();
        retry1.op(false);
        Instrumentation.testInvocationCount(1);
        Instrumentation.testFailureCount(0);
        Instrumentation.testRetryCount(0);
        WriteLine("ok");

        Callback cb = new Callback();

        Write("calling regular AMI operation with first proxy... ");
        retry1.begin_op(false).whenCompleted(
            () =>
            {
                cb.called();
            },
            (Ice.Exception ex) =>
            {
                test(false);
            });
        cb.check();
        Instrumentation.testInvocationCount(1);
        Instrumentation.testFailureCount(0);
        Instrumentation.testRetryCount(0);
        WriteLine("ok");

        Write("calling AMI operation to kill connection with second proxy... ");
        retry2.begin_op(true).whenCompleted(
            () =>
            {
                test(false);
            },
            (Ice.Exception ex) =>
            {
                test(ex is Ice.ConnectionLostException || ex is Ice.UnknownLocalException);
                cb.called();
            });
        cb.check();
        Instrumentation.testInvocationCount(1);
        Instrumentation.testFailureCount(1);
        Instrumentation.testRetryCount(0);
        WriteLine("ok");

        Write("calling regular AMI operation with first proxy again... ");
        retry1.begin_op(false).whenCompleted(
            () =>
            {
                cb.called();
            },
            (Ice.Exception ex) =>
            {
                test(false);
            });
        cb.check();
        Instrumentation.testInvocationCount(1);
        Instrumentation.testFailureCount(0);
        Instrumentation.testRetryCount(0);
        WriteLine("ok");

        Write("testing idempotent operation... ");
        test(retry1.opIdempotent(4) == 4);
        Instrumentation.testInvocationCount(1);
        Instrumentation.testFailureCount(0);
        Instrumentation.testRetryCount(4);
        test(retry1.end_opIdempotent(retry1.begin_opIdempotent(4)) == 4);
        Instrumentation.testInvocationCount(1);
        Instrumentation.testFailureCount(0);
        Instrumentation.testRetryCount(4);
        WriteLine("ok");

        Write("testing non-idempotent operation... ");
        try
        {
            retry1.opNotIdempotent();
            test(false);
        }
        catch(Ice.LocalException)
        {
        }
        Instrumentation.testInvocationCount(1);
        Instrumentation.testFailureCount(1);
        Instrumentation.testRetryCount(0);
        try
        {
            retry1.end_opNotIdempotent(retry1.begin_opNotIdempotent());
            test(false);
        }
        catch(Ice.LocalException)
        {
        }
        Instrumentation.testInvocationCount(1);
        Instrumentation.testFailureCount(1);
        Instrumentation.testRetryCount(0);
        WriteLine("ok");

        if(retry1.ice_getConnection() == null)
        {
            Instrumentation.testInvocationCount(1);

            Write("testing system exception... ");
            try
            {
                retry1.opSystemException();
                test(false);
            }
            catch(SystemFailure)
            {
            }
            Instrumentation.testInvocationCount(1);
            Instrumentation.testFailureCount(1);
            Instrumentation.testRetryCount(0);
            try
            {
                retry1.end_opSystemException(retry1.begin_opSystemException());
                test(false);
            }
            catch(SystemFailure)
            {
            }
            Instrumentation.testInvocationCount(1);
            Instrumentation.testFailureCount(1);
            Instrumentation.testRetryCount(0);
            WriteLine("ok");
        }

        Write("testing invocation timeout and retries... ");
        Flush();

        retry2 = Test.RetryPrxHelper.checkedCast(communicator2.stringToProxy(retry1.ToString()));
        try
        {
            // No more than 2 retries before timeout kicks-in
            ((Test.RetryPrx)retry2.ice_invocationTimeout(500)).opIdempotent(4);
            test(false);
        }
        catch(Ice.InvocationTimeoutException)
        {
            Instrumentation.testRetryCount(2);
            retry2.opIdempotent(-1); // Reset the counter
            Instrumentation.testRetryCount(-1);
        }
        try
        {
            // No more than 2 retries before timeout kicks-in
            Test.RetryPrx prx = (Test.RetryPrx)retry2.ice_invocationTimeout(500);
            prx.end_opIdempotent(prx.begin_opIdempotent(4));
            test(false);
        }
        catch(Ice.InvocationTimeoutException)
        {
            Instrumentation.testRetryCount(2);
            retry2.opIdempotent(-1); // Reset the counter
            Instrumentation.testRetryCount(-1);
        }
        WriteLine("ok");

#if SILVERLIGHT
        retry1.shutdown();
#else
        return retry1;
#endif
    }
}
