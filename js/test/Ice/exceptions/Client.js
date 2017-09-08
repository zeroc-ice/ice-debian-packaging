// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

(function(module, require, exports)
{
    var Ice = require("ice").Ice;
    var Test = require("Test").Test;

    var Promise = Ice.Promise;

    var allTests = function(out, communicator, Test, bidir)
    {
        var EmptyI = function()
        {
        };

        EmptyI.prototype = new Test.Empty();
        EmptyI.prototype.constructor = EmptyI;

        var ServantLocatorI = function()
        {
        };

        ServantLocatorI.prototype.locate = function(curr, cookie)
        {
            return null;
        };

        ServantLocatorI.prototype.finished = function(curr, servant, cookie)
        {
        };

        ServantLocatorI.prototype.deactivate = function(category)
        {
        };

        var ObjectFactoryI = function()
        {
        };

        ObjectFactoryI.prototype.create = function(type)
        {
            return null;
        };

        ObjectFactoryI.prototype.destroy = function()
        {
        };

        var p = new Ice.Promise();
        var test = function(b)
        {
            if(!b)
            {
                try
                {
                    throw new Error("test failed");
                }
                catch(err)
                {
                    p.fail(err);
                    throw err;
                }
            }
        };

        var failCB = function(){ test(false); };

        var supportsUndeclaredExceptions = function(thrower)
        {
            return Promise.try(
                function()
                {
                    return thrower.supportsUndeclaredExceptions().then(
                        function(v)
                        {
                            if(v)
                            {
                                out.write("catching unknown user exception... ");
                                return thrower.throwUndeclaredA(1).then(
                                    failCB,
                                    function(ex)
                                    {
                                        test(ex instanceof Ice.UnknownUserException);
                                        return thrower.throwUndeclaredB(1, 2);
                                    }
                                ).then(
                                    failCB,
                                    function(ex)
                                    {
                                        test(ex instanceof Ice.UnknownUserException);
                                        return thrower.throwUndeclaredC(1, 2, 3);
                                    }
                                ).then(
                                    failCB,
                                    function(ex)
                                    {
                                        test(ex instanceof Ice.UnknownUserException);
                                        out.writeLine("ok");
                                    }
                                );
                            }
                        });
                });
        };

        var supportsAssertException = function(thrower)
        {
            return Promise.try(
                function()
                {
                    return thrower.supportsAssertException().then(
                        function(v)
                        {
                            if(v)
                            {
                                out.write("testing assert in the server... ");
                                return thrower.throwAssertException().then(
                                    failCB,
                                    function(ex)
                                    {
                                        test(ex instanceof Ice.ConnectionLostException ||
                                             ex instanceof Ice.UnknownException);
                                        out.writeLine("ok");
                                    }
                                );
                            }
                        });
                });
        };

        var base, ref, thrower;
        Promise.try(
            function()
            {
                out.write("testing object adapter registration exceptions... ");
                return communicator.createObjectAdapter("TestAdapter0").then(
                    failCB,
                    function(ex)
                    {
                        test(ex instanceof Ice.InitializationException); // Expected
                    });
            }
        ).then(
            function()
            {
                return communicator.createObjectAdapterWithEndpoints("TestAdapter0", "default").then(
                    failCB,
                    function(ex)
                    {
                        test(ex instanceof Ice.FeatureNotSupportedException); // Expected
                        out.writeLine("ok");
                    });
            }
        ).then(
            function()
            {
                out.write("testing servant registration exceptions... ");
                return communicator.createObjectAdapter("").then(
                    function(adapter)
                    {
                        var obj = new EmptyI();
                        adapter.add(obj, communicator.stringToIdentity("x"));
                        try
                        {
                            adapter.add(obj, communicator.stringToIdentity("x"));
                            test(false);
                        }
                        catch(ex)
                        {
                            test(ex instanceof Ice.AlreadyRegisteredException);
                        }
                        try
                        {
                            adapter.add(obj, communicator.stringToIdentity(""));
                            test(false);
                        }
                        catch(ex)
                        {
                            test(ex instanceof Ice.IllegalIdentityException);
                            test(ex.id.name === "");
                        }
                        try
                        {
                            adapter.add(null, communicator.stringToIdentity("x"));
                            test(false);
                        }
                        catch(ex)
                        {
                            test(ex instanceof Ice.IllegalServantException);
                        }

                        adapter.remove(communicator.stringToIdentity("x"));
                        try
                        {
                            adapter.remove(communicator.stringToIdentity("x"));
                            test(false);
                        }
                        catch(ex)
                        {
                            test(ex instanceof Ice.NotRegisteredException);
                        }
                        adapter.deactivate();
                        out.writeLine("ok");
                    });
            }
        ).then(
            function()
            {
                out.write("testing servant locator registration exceptions... ");
                return communicator.createObjectAdapter("").then(
                    function(adapter)
                    {
                        var loc = new ServantLocatorI();
                        adapter.addServantLocator(loc, "x");
                        try
                        {
                            adapter.addServantLocator(loc, "x");
                            test(false);
                        }
                        catch(ex)
                        {
                            test(ex instanceof Ice.AlreadyRegisteredException);
                        }
                        adapter.deactivate();
                        out.writeLine("ok");
                    });
            }
        ).then(
            function()
            {
                out.write("testing object factory registration exception... ");
                var of = new ObjectFactoryI();
                communicator.addObjectFactory(of, "::x");
                try
                {
                    communicator.addObjectFactory(of, "::x");
                    test(false);
                }
                catch(ex)
                {
                    test(ex instanceof Ice.AlreadyRegisteredException);
                }
                out.writeLine("ok");

                out.write("testing stringToProxy... ");
                ref = "thrower:default -p 12010";
                base = communicator.stringToProxy(ref);
                test(base !== null);
                out.writeLine("ok");
                out.write("testing checked cast... ");
                return Test.ThrowerPrx.checkedCast(base);
            }
            ).then(
            function(obj)
            {
                thrower = obj;
                test(thrower !== null);
                test(thrower.equals(base));
                out.writeLine("ok");
                out.write("catching exact types... ");
                return thrower.throwAasA(1);
            }
        ).then(
            failCB,
            function(ex)
            {
                test(ex instanceof Test.A);
                test(ex.aMem === 1);
                return thrower.throwAorDasAorD(1);
            }
        ).then(
            failCB,
            function(ex)
            {
                test(ex instanceof Test.A);
                test(ex.aMem === 1);
                return thrower.throwAorDasAorD(-1);
            }
        ).then(
            failCB,
            function(ex)
            {
                test(ex instanceof Test.D);
                test(ex.dMem === -1);
                return thrower.throwBasB(1, 2);
            }
        ).then(
            failCB,
            function(ex)
            {
                test(ex instanceof Test.B);
                test(ex.aMem == 1);
                test(ex.bMem == 2);
                return thrower.throwCasC(1, 2, 3);
            }
        ).then(
            failCB,
            function(ex)
            {
                test(ex instanceof Test.C);
                test(ex.aMem == 1);
                test(ex.bMem == 2);
                test(ex.cMem == 3);
                out.writeLine("ok");
                out.write("catching base types... ");
                return thrower.throwBasB(1, 2);
            }
        ).then(
            failCB,
            function(ex)
            {
                test(ex instanceof Test.A);
                test(ex.aMem == 1);
                return thrower.throwCasC(1, 2, 3);
            }
        ).then(
            failCB,
            function(ex)
            {
                test(ex instanceof Test.B);
                test(ex.aMem == 1);
                test(ex.bMem == 2);
                out.writeLine("ok");
                out.write("catching derived types... ");
                return thrower.throwBasA(1, 2);
            }
        ).then(
            failCB,
            function(ex)
            {
                test(ex instanceof Test.B);
                test(ex.aMem == 1);
                test(ex.bMem == 2);
                return thrower.throwCasA(1, 2, 3);
            }
        ).then(
            failCB,
            function(ex)
            {
                test(ex instanceof Test.C);
                test(ex.aMem == 1);
                test(ex.bMem == 2);
                test(ex.cMem == 3);
                return thrower.throwCasB(1, 2, 3);
            }
        ).then(
            failCB,
            function(ex)
            {
                test(ex instanceof Test.C);
                test(ex.aMem == 1);
                test(ex.bMem == 2);
                test(ex.cMem == 3);
                out.writeLine("ok");
                return supportsUndeclaredExceptions(thrower);
            }
        ).then(
            function()
            {
                return supportsAssertException(thrower);
            }
        ).then(
            function()
            {
                if(!bidir)
                {
                    out.write("testing memory limit marshal exception...");
                    return thrower.throwMemoryLimitException(null).then(
                        failCB,
                        function(ex)
                        {
                            test(ex instanceof Ice.MemoryLimitException);
                            return thrower.throwMemoryLimitException(Ice.Buffer.createNative(20 * 1024));
                        }
                    ).then(
                        failCB,
                        function(ex)
                        {
                            test(ex instanceof Ice.ConnectionLostException);
                            out.writeLine("ok");
                        }
                    );
                }
            }
        ).then(
            function()
            {
                out.write("catching object not exist exception... ");
                var id = communicator.stringToIdentity("does not exist");
                var thrower2 = Test.ThrowerPrx.uncheckedCast(thrower.ice_identity(id));
                return thrower2.ice_ping();
            }
        ).then(
            failCB,
            function(ex)
            {
                test(ex instanceof Ice.ObjectNotExistException);
                test(ex.id.equals(communicator.stringToIdentity("does not exist")));
                out.writeLine("ok");
                out.write("catching facet not exist exception... ");
                var thrower2 = Test.ThrowerPrx.uncheckedCast(thrower, "no such facet");
                return thrower2.ice_ping();
            }
        ).then(
            failCB,
            function(ex)
            {
                test(ex instanceof Ice.FacetNotExistException);
                test(ex.facet == "no such facet");
                out.writeLine("ok");
                out.write("catching operation not exist exception... ");
                var thrower2 = Test.WrongOperationPrx.uncheckedCast(thrower);
                return thrower2.noSuchOperation();
            }
        ).then(
            failCB,
            function(ex)
            {
                test(ex instanceof Ice.OperationNotExistException);
                test(ex.operation == "noSuchOperation");
                out.writeLine("ok");
                out.write("catching unknown local exception... ");
                return thrower.throwLocalException();
            }
        ).then(
            failCB,
            function(ex)
            {
                test(ex instanceof Ice.UnknownLocalException);
                return thrower.throwLocalExceptionIdempotent();
            }
        ).then(
            failCB,
            function(ex)
            {
                test(ex instanceof Ice.UnknownLocalException ||
                     ex instanceof Ice.OperationNotExistException);
                out.writeLine("ok");
                out.write("catching unknown non-Ice exception... ");
                return thrower.throwNonIceException();
            }
        ).then(
            failCB,
            function(ex)
            {
                test(ex instanceof Ice.UnknownException);
                out.writeLine("ok");
                out.write("testing asynchronous exceptions... ");
                return thrower.throwAfterResponse();
            }
        ).then(
            function()
            {
                return thrower.throwAfterException();
            }
        ).then(
            failCB,
            function(ex)
            {
                test(ex instanceof Test.A);
                out.writeLine("ok");
                return thrower.shutdown();
            }
        ).then(
            function()
            {
                p.succeed();
            },
            function(ex)
            {
                p.fail(ex);
            }
        );
        return p;
    };

    var run = function(out, id)
    {
        id.properties.setProperty("Ice.MessageSizeMax", "10");
        id.properties.setProperty("Ice.Warn.Connections", "0");
        var c = Ice.initialize(id);
        return Promise.try(
            function()
            {
                return allTests(out, c, Test);
            }
        ).finally(
            function()
            {
                return c.destroy();
            }
        );
    };
    exports.__test__ = run;
    exports.__clientAllTests__ = allTests;
    exports.__runServer__ = true;
}
(typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? module : undefined,
 typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? require : this.Ice.__require,
 typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? exports : this));
