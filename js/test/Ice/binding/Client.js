// **********************************************************************
//
// Copyright (c) 2003-2016 ZeroC, Inc. All rights reserved.
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
    var ArrayUtil = Ice.ArrayUtil;

    var forEach = function(array, fn)
    {
        if(array.length === 0)
        {
            return new Promise.succeed();
        }
        var p = null;
        array.forEach(function(e)
                      {
                          p = p ? p.then(fn(e)) : fn(e);
                      });
        return p;
    };

    var isBrowser = (typeof window !== 'undefined' || typeof WorkerGlobalScope !== 'undefined');

    var communicator;
    var com;
    var allTests = function(out, initData)
    {
        var initialize = function()
        {
            return Promise.try(
                function()
                {
                    if(communicator)
                    {
                        return communicator.destroy().then(
                            function()
                            {
                                communicator = Ice.initialize(initData);
                                com = Test.RemoteCommunicatorPrx.uncheckedCast(
                                    communicator.stringToProxy("communicator:default -p 12010"));
                            });
                    }
                    else
                    {
                        communicator = Ice.initialize(initData);
                    }
                });
        };

        var createTestIntfPrx = function(adapters)
        {
            var endpoints = [];
            var closePromises = [];
            var p = null;

            return Promise.all(adapters.map(function(adapter){ return adapter.getTestIntf(); })).then(
                function()
                {
                    var results = Array.prototype.slice.call(arguments);
                    results.forEach(
                        function(r)
                        {
                            p = r[0];
                            test(p);
                            endpoints = endpoints.concat(p.ice_getEndpoints());
                        }
                    );
                    return Test.TestIntfPrx.uncheckedCast(p.ice_endpoints(endpoints));
                });
        };

        var deactivate = function(communicator, adapters)
        {
            var f1 = function(adapters)
            {
                var adapter = adapters.shift();
                communicator.deactivateObjectAdapter(adapter).then(
                    function()
                    {
                        if(adapters.length > 0)
                        {
                            f1(adapters);
                        }
                    }
                );
            };
            return f1(ArrayUtil.clone(adapters));
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

        var ref, adapter, test1, test2, test3, conn1, conn2, conn3, adapters, names, prx;

        Promise.try(
            function()
            {
                return initialize();
            }
        ).then(
            function()
            {
                out.write("testing stringToProxy... ");
                ref = "communicator:default -p 12010";
                com = Test.RemoteCommunicatorPrx.uncheckedCast(communicator.stringToProxy(ref));
                test(com !== null);
                out.writeLine("ok");

                out.write("testing binding with single endpoint... ");
                var adapter, test1, test2, test3, conn1, conn2, conn3;
                adapters = [];
                names = ["Adapter11", "Adapter12", "Adapter13"];

                return com.createObjectAdapter("Adapter", "default");
            }
        ).then(
            function(obj)
            {
                adapter = obj;
                return Promise.all(adapter.getTestIntf(), adapter.getTestIntf());
            }
        ).then(
            function(r1, r2)
            {
                test1 = r1[0];
                test2 = r2[0];

                return Promise.all(test1.ice_getConnection(), test2.ice_getConnection());
            }
        ).then(
            function(r1, r2)
            {
                conn1 = r1[0];
                conn2 = r2[0];
                test(conn1 === conn2);
                return Promise.all(test1.ice_ping(), test2.ice_ping());
            }
        ).then(
            function(r1, r2)
            {
                return com.deactivateObjectAdapter(adapter);
            }
        ).then(
            function()
            {
                test3 = Test.TestIntfPrx.uncheckedCast(test1);
                return Promise.all(test3.ice_getConnection(), test1.ice_getConnection());
            }
        ).then(
            function(r1, r2)
            {
                conn3 = r1[0];
                conn1 = r2[0];
                test(conn3 === conn1);
                return Promise.all(test3.ice_getConnection(), test2.ice_getConnection());
            }
        ).then(
            function(r1, r2)
            {
                conn3 = r1[0];
                conn2 = r2[0];
                test(conn3 === conn2);
                return test3.ice_ping();
            }
        ).then(
            function()
            {
                test(false);
            },
            function(ex)
            {
                if(!(!isBrowser && ex instanceof Ice.ConnectionRefusedException) &&
                   !(isBrowser && ex instanceof Ice.ConnectFailedException))
                {
                    throw ex;
                }
                out.writeLine("ok");
                return initialize();
            }
        ).then(
            function()
            {
                out.write("testing binding with multiple endpoints... ");

                return Promise.all(
                    com.createObjectAdapter("Adapter11", "default"),
                    com.createObjectAdapter("Adapter12", "default"),
                    com.createObjectAdapter("Adapter13", "default"));
            }
        ).then(
            //
            // Ensure that when a connection is opened it's reused for new
            // proxies and that all endpoints are eventually tried.
            //
            function(r1, r2, r3)
            {
                adapters.push(r1[0]);
                adapters.push(r2[0]);
                adapters.push(r3[0]);

                var f1 = function(names)
                {
                    var adpts = ArrayUtil.clone(adapters);
                    return createTestIntfPrx(adpts).then(
                        function(obj)
                        {
                            test1 = obj;
                            ArrayUtil.shuffle(adpts);
                            return createTestIntfPrx(adpts);
                        }
                    ).then(
                        function(obj)
                        {
                            test2 = obj;
                            ArrayUtil.shuffle(adpts);
                            return createTestIntfPrx(adpts);
                        }
                    ).then(
                        function(obj)
                        {
                            test3 = obj;
                            return Promise.all(test1.ice_getConnection(), test2.ice_getConnection());
                        }
                    ).then(
                        function(r1, r2)
                        {
                            test(r1[0] === r2[0]);
                            return Promise.all(test2.ice_getConnection(), test3.ice_getConnection());
                        }
                    ).then(
                        function(r1, r2)
                        {
                            test(r1[0] === r2[0]);
                            return test1.getAdapterName();
                        }
                    ).then(
                        function(name)
                        {
                            if(names.indexOf(name) !== -1)
                            {
                                names.splice(names.indexOf(name), 1);
                            }
                            return test1.ice_getConnection();
                        }
                    ).then(
                        function(conn)
                        {
                            return conn.close(false);
                        }
                    ).then(
                        function()
                        {
                            if(names.length > 0)
                            {
                                return f1(names);
                            }
                        }
                    );
                };
                return f1(ArrayUtil.clone(names));
            }
        ).then(
            function()
            {
                //
                // Ensure that the proxy correctly caches the connection (we
                // always send the request over the same connection.)
                //
                return Promise.all(adapters.map(function(adapter)
                                                {
                                                    return adapter.getTestIntf().then(
                                                        function(p)
                                                        {
                                                            return p.ice_ping();
                                                        });
                                                }));
            }
        ).then(
            function()
            {
                return createTestIntfPrx(adapters).then(
                    function(test1)
                    {
                        var i = 0;
                        var nRetry = 10;
                        var adapterName;

                        var f1 = function()
                        {
                            return test1.getAdapterName().then(
                                function(name)
                                {
                                    test(adapterName === name);
                                    if(++i < nRetry)
                                    {
                                        return f1();
                                    }
                                    else
                                    {
                                        test(i == nRetry);
                                    }
                                }
                            );
                        };

                        return test1.getAdapterName().then(
                            function(name)
                            {
                                adapterName = name;
                                return f1();
                            }
                        );
                    });
            }
        ).then(
            function()
            {
                return Promise.all(adapters.map(function(adapter)
                                                {
                                                    return adapter.getTestIntf().then(
                                                        function(p)
                                                        {
                                                            return p.ice_getConnection();
                                                        }).then(
                                                            function(c)
                                                            {
                                                                return c.close(false);
                                                            }
                                                        );
                                                }));
            }
        ).then(
            function()
            {
                names = ["Adapter12", "Adapter13"];
                return com.deactivateObjectAdapter(adapters[0]);
            }
        ).then(
            function()
            {
                var f1 = function(names)
                {
                    var adpts = ArrayUtil.clone(adapters);
                    return createTestIntfPrx(adpts).then(
                        function(obj)
                        {
                            test1 = obj;
                            ArrayUtil.shuffle(adpts);
                            return createTestIntfPrx(adpts);
                        }
                    ).then(
                        function(obj)
                        {
                            test2 = obj;
                            ArrayUtil.shuffle(adpts);
                            return createTestIntfPrx(adpts);
                        }
                    ).then(
                        function(obj)
                        {
                            test3 = obj;
                            var conn1, conn2;
                            return test1.ice_getConnection().then(
                                function(conn)
                                {
                                    conn1 = conn;
                                    return test2.ice_getConnection();
                                }
                            ).then(
                                function(conn)
                                {
                                    conn2 = conn;
                                    test(conn1 === conn2);
                                });
                        }
                    ).then(
                        function()
                        {
                            var conn1, conn2;
                            return test2.ice_getConnection().then(
                                function(conn)
                                {
                                    conn1 = conn;
                                    return test3.ice_getConnection();
                                }
                            ).then(
                                function(conn)
                                {
                                    conn2 = conn;
                                    test(conn1 === conn2);
                                });
                        }
                    ).then(
                        function()
                        {
                            return test1.getAdapterName();
                        }
                    ).then(
                        function(name)
                        {
                            if(names.indexOf(name) !== -1)
                            {
                                names.splice(names.indexOf(name), 1);
                            }
                            return test1.ice_getConnection();
                        }
                    ).then(
                        function(conn)
                        {
                            return conn.close(false);
                        }
                    ).then(
                        function()
                        {
                            if(names.length > 0)
                            {
                                return f1(names);
                            }
                        }
                    );
                };
                return f1(ArrayUtil.clone(names));
            }
        ).then(
            function()
            {
                return com.deactivateObjectAdapter(adapters[2]);
            }
        ).then(
            function()
            {
                return createTestIntfPrx(adapters);
            }
        ).then(
            function(prx)
            {
                return prx.getAdapterName();
            }
        ).then(
            function(name)
            {
                test(name == "Adapter12");
                return deactivate(com, adapters);
            }
        ).then(
            function()
            {
                out.writeLine("ok");
                return initialize();
            }
        ).then(
            function()
            {
                //
                // Skip this test with IE it open too many connections IE doesn't allow more
                // than 6 connections. Firefox has a configuration that causes failed connections
                // to be delayed on retry (network.websocket.delay-failed-reconnects=true by
                // default), so we prefer to also disable this test with Firefox.
                //
                if(typeof(navigator) !== "undefined" &&
                   (navigator.userAgent.indexOf("MSIE") !== -1 ||
                    navigator.userAgent.indexOf("Trident/7.0") ||
                    navigator.userAgent.indexOf("Firefox") !== -1))
                {
                    return;
                }

                out.write("testing binding with multiple random endpoints... ");
                names = ["AdapterRandom11", "AdapterRandom12", "AdapterRandom13", "AdapterRandom14", "AdapterRandom15"];

                return Promise.all(
                    names.map(function(name)
                              {
                                  return com.createObjectAdapter(name, "default");
                              })
                ).then(
                    function()
                    {
                        adapters = Array.prototype.slice.call(arguments).map(function(r) { return r[0]; });
                        var count = 20;
                        var adapterCount = adapters.length;
                        var proxies = new Array(10);
                        var nextInt = function(n) { return Math.floor((Math.random() * n)); };

                        var f1 = function(count, adapterCount, proxies)
                        {
                            var p1 = count === 10 ? com.deactivateObjectAdapter(adapters[2]) : new Promise().succeed();
                            return p1.then(
                                function()
                                {
                                    if(count === 10)
                                    {
                                        adapterCount--;
                                    }

                                    var f2 = function(i)
                                    {
                                        var adpts = new Array(nextInt(adapters.length) + 1);
                                        for(var j = 0; j < adpts.length; ++j)
                                        {
                                            adpts[j] = adapters[nextInt(adapters.length)];
                                        }

                                        return createTestIntfPrx(adpts).then(
                                            function(prx)
                                            {
                                                proxies[i] = prx;
                                                if(i < 10)
                                                {
                                                    return f2(++i);
                                                }
                                                else
                                                {
                                                    return proxies;
                                                }
                                            });
                                    };

                                    return f2(0);
                                }
                            ).then(
                                function(proxies)
                                {
                                    return Ice.Promise.try(
                                        function()
                                        {
                                            return forEach(proxies,
                                                           function(p)
                                                           {
                                                               p.getAdapterName();
                                                           });
                                        }
                                    ).then(
                                        function()
                                        {
                                            return forEach(proxies,
                                                           function(proxy)
                                                           {
                                                               return proxy.ice_ping().exception(
                                                                   function(ex)
                                                                   {
                                                                       test(ex instanceof Ice.LocalException);
                                                                   });
                                                           });
                                        }
                                    ).then(
                                        function()
                                        {
                                            var connections = [];
                                            proxies.forEach(
                                                function(p)
                                                {
                                                    var conn = p.ice_getCachedConnection();
                                                    if(conn !== null)
                                                    {
                                                        if(connections.indexOf(conn) !== -1)
                                                        {
                                                            connections.push(conn);
                                                        }
                                                    }
                                                });
                                            test(connections.length <= adapters.length);

                                            return Promise.all(adapters.map(
                                                function(adapter)
                                                {
                                                    return adapter.getTestIntf().then(
                                                        function(p)
                                                        {
                                                            return p.ice_getConnection();
                                                        }).then(
                                                            function(c)
                                                            {
                                                                return c.close(false);
                                                            },
                                                            function(ex)
                                                            {
                                                                // Expected if adapter is down.
                                                                test(ex instanceof Ice.LocalException);
                                                            }
                                                        );
                                                }));
                                        }
                                    );
                                }
                            ).then(
                                function()
                                {
                                    if(count > 0)
                                    {
                                        return f1(--count, adapterCount, proxies);
                                    }
                                });
                        };
                        return f1(count, adapterCount, proxies);
                    }).then(
                        function()
                        {
                            out.writeLine("ok");
                        },
                        function(ex)
                        {
                            out.writeLine("failed! " + ex.stack);
                        }
                    );
            }
        ).then(
            function()
            {
                return initialize();
            }
        ).then(
            function()
            {
                out.write("testing random endpoint selection... ");
                names = ["Adapter21", "Adapter22", "Adapter23"];
                return Promise.all(names.map(function(name) { return com.createObjectAdapter(name, "default"); }));
            }
        ).then(
            function()
            {
                adapters = Array.prototype.slice.call(arguments).map(function(r) { return r[0]; });
                return createTestIntfPrx(adapters);
            },
            function(ex)
            {
                console.log(ex.toString());
                test(false);
            }
        ).then(
            function(prx)
            {
                test(prx.ice_getEndpointSelection() == Ice.EndpointSelectionType.Random);

                var f1 = function()
                {
                    return prx.getAdapterName().then(
                        function(name)
                        {
                            if(names.indexOf(name) !== -1)
                            {
                                names.splice(names.indexOf(name), 1);
                            }
                            return prx.ice_getConnection();
                        }
                    ).then(
                        function(conn)
                        {
                            return conn.close(false);
                        }
                    ).then(
                        function()
                        {
                            if(names.length > 0)
                            {
                                return f1();
                            }
                            else
                            {
                                return prx;
                            }
                        }
                    );
                };

                return f1();
            }
        ).then(
            function(obj)
            {
                prx = obj;
                prx = Test.TestIntfPrx.uncheckedCast(prx.ice_endpointSelection(Ice.EndpointSelectionType.Random));
                test(prx.ice_getEndpointSelection() === Ice.EndpointSelectionType.Random);
                names = ["Adapter21", "Adapter22", "Adapter23"];
                var f1 = function()
                {
                    return prx.getAdapterName().then(
                        function(name)
                        {
                            if(names.indexOf(name) !== -1)
                            {
                                names.splice(names.indexOf(name), 1);
                            }
                            return prx.ice_getConnection();
                        }
                    ).then(
                        function(conn)
                        {
                            return conn.close(false);
                        }
                    ).then(
                        function()
                        {
                            if(names.length > 0)
                            {
                                return f1();
                            }
                            else
                            {
                                return prx;
                            }
                        }
                    );
                };
                return f1();
            },
            function(ex)
            {
                test(false);
            }
        ).then(
            function()
            {
                return  deactivate(com, adapters);
            }
        ).then(
            function()
            {
                out.writeLine("ok");
                return initialize();
            }
        ).then(
            function()
            {
                out.write("testing ordered endpoint selection... ");
                names = ["Adapter31", "Adapter32", "Adapter33"];
                return Promise.all(names.map(function(name) { return com.createObjectAdapter(name, "default"); }));
            }
        ).then(
            function()
            {
                adapters = Array.prototype.slice.call(arguments).map(function(r) { return r[0]; });
                return createTestIntfPrx(adapters);
            }
        ).then(
            function(obj)
            {
                prx = obj;
                prx = Test.TestIntfPrx.uncheckedCast(prx.ice_endpointSelection(Ice.EndpointSelectionType.Ordered));
                test(prx.ice_getEndpointSelection() === Ice.EndpointSelectionType.Ordered);
                var i, nRetry = 5;
                var f1 = function(i, idx, names)
                {
                    return prx.getAdapterName().then(
                        function(name)
                        {
                            test(name === names[0]);
                        }
                    ).then(
                        function()
                        {
                            if(i < nRetry)
                            {
                                return f1(++i, idx, names);
                            }
                            else
                            {
                                return com.deactivateObjectAdapter(adapters[idx]).then(
                                    function()
                                    {
                                        if(names.length > 1)
                                        {
                                            names.shift();
                                            return f1(0, ++idx, names);
                                        }
                                    }
                                );
                            }
                        }
                    );
                };

                return f1(0, 0, ArrayUtil.clone(names));
            }
        ).then(
            function()
            {
                return prx.getAdapterName();
            }
        ).then(
            function()
            {
                test(false);
            },
            function(ex)
            {
                test((!isBrowser && ex instanceof Ice.ConnectionRefusedException) ||
                     (isBrowser && ex instanceof Ice.ConnectFailedException));
                return prx.ice_getEndpoints();
            }
        ).then(
            function(endpoints)
            {
                var nRetry = 5;
                var j = 2;
                var f1 = function(i, names)
                {
                    return com.createObjectAdapter(names[0], endpoints[j--].toString()).then(
                        function(obj)
                        {
                            return obj.getTestIntf();
                        }
                    ).then(
                        function(prx)
                        {
                            var f2 = function(i, names)
                            {
                                return prx.getAdapterName().then(
                                    function(name)
                                    {
                                        test(name === names[0]);
                                        if(i < nRetry)
                                        {
                                            return f2(++i, names);
                                        }
                                        else if(names.length > 1)
                                        {
                                            names.shift();
                                            return f1(0, names);
                                        }
                                    });
                            };
                            return f2(0, names);
                        });
                };

                return f1(0, ["Adapter36", "Adapter35", "Adapter34"]);
            }
        ).then(
            function()
            {
                return deactivate(com, adapters);
            }
        ).then(
            function(){
                out.writeLine("ok");
                return initialize;
            }
        ).then(
            function()
            {
                out.write("testing per request binding with single endpoint... ");
                return com.createObjectAdapter("Adapter41", "default");
            }
        ).then(
            function(adapter)
            {
                var f1 = function()
                {
                    return adapter.getTestIntf().then(
                        function(obj)
                        {
                            test1 = Test.TestIntfPrx.uncheckedCast(obj.ice_connectionCached(false));
                            return adapter.getTestIntf();
                        }
                    ).then(
                        function(obj)
                        {
                            test2 = Test.TestIntfPrx.uncheckedCast(obj.ice_connectionCached(false));
                            test(!test1.ice_isConnectionCached());
                            test(!test2.ice_isConnectionCached());
                            return Promise.all(test1.ice_getConnection(),
                                               test2.ice_getConnection());
                        }
                    ).then(
                        function(r1, r2)
                        {
                            test(r1[0] == r2[0]);
                            return test1.ice_ping();
                        }
                    ).then(
                        function()
                        {
                            return com.deactivateObjectAdapter(adapter);
                        }
                    ).then(
                        function()
                        {
                            var test3 = Test.TestIntfPrx.uncheckedCast(test1);
                            return Promise.all(test3.ice_getConnection(),
                                               test1.ice_getConnection());
                        }
                    ).then(
                        function()
                        {
                            test(false);
                        },
                        function(ex)
                        {
                            test((!isBrowser && ex instanceof Ice.ConnectionRefusedException) ||
                                 (isBrowser && ex instanceof Ice.ConnectFailedException));
                        });
                };
                return f1();
            }
        ).then(
            function()
            {
                out.writeLine("ok");
                return initialize();
            }
        ).then(
            function()
            {
                out.write("testing per request binding with multiple endpoints... ");
                names = ["Adapter51", "Adapter52", "Adapter53"];
                return Promise.all(names.map(function(name) { return com.createObjectAdapter(name, "default"); }));
            }
        ).then(
            function()
            {
                adapters = Array.prototype.slice.call(arguments).map(function(r) { return r[0]; });

                var f2 = function(prx)
                {
                    return prx.getAdapterName().then(
                        function(name)
                        {
                            if(names.indexOf(name) !== -1)
                            {
                                names.splice(names.indexOf(name), 1);
                            }
                            if(names.length > 0)
                            {
                                return f2(prx);
                            }
                            else
                            {
                                return prx;
                            }
                        });
                };

                var f1 = function()
                {
                    return createTestIntfPrx(adapters).then(
                        function(prx)
                        {
                            prx = Test.TestIntfPrx.uncheckedCast(prx.ice_connectionCached(false));
                            test(!prx.ice_isConnectionCached());
                            return f2(prx);
                        });

                };

                return f1().then(
                    function(prx)
                    {
                        com.deactivateObjectAdapter(adapters[0]).then(
                            function()
                            {
                                names = ["Adapter52", "Adapter53"];
                                return f2(prx);
                            }
                        ).then(
                            function()
                            {
                                return com.deactivateObjectAdapter(adapters[0]);
                            }
                        ).then(
                            function()
                            {
                                names = ["Adapter52"];
                                return f2(prx);
                            }
                        );
                    });
            }
        ).then(
            function()
            {
                return deactivate(com, adapters);
            }
        ).then(
            function()
            {
                out.writeLine("ok");
                return initialize();
            }
        ).then(
            function()
            {
                if(typeof(navigator) !== "undefined" &&
                   (navigator.userAgent.indexOf("Firefox") !== -1 ||
                    navigator.userAgent.indexOf("MSIE") !== -1 ||
                    navigator.userAgent.indexOf("Trident/7.0") !== -1))
                {
                    //
                    // Firefox adds a delay on websocket failed reconnects that causes this test to take too
                    // much time to complete.
                    //
                    // You can set network.websocket.delay-failed-reconnects to false in Firefox about:config
                    // to disable this behaviour
                    //
                    return;
                }
                out.write("testing per request binding and ordered endpoint selection... ");
                names = ["Adapter61", "Adapter62", "Adapter63"];
                return Promise.all(names.map(function(name) { return com.createObjectAdapter(name, "default"); })).then(
                    function(a)
                    {
                        adapters = Array.prototype.slice.call(arguments).map(function(r) { return r[0]; });
                        return createTestIntfPrx(adapters);
                    }
                ).then(
                    function(obj)
                    {
                        prx = obj;
                        prx = Test.TestIntfPrx.uncheckedCast(prx.ice_endpointSelection(Ice.EndpointSelectionType.Ordered));
                        test(prx.ice_getEndpointSelection() == Ice.EndpointSelectionType.Ordered);
                        prx = Test.TestIntfPrx.uncheckedCast(prx.ice_connectionCached(false));
                        test(!prx.ice_isConnectionCached());
                        var nRetry = 5;
                        var f1 = function(i, idx, names)
                        {
                            return prx.getAdapterName().then(
                                function(name)
                                {
                                    test(name === names[0]);
                                }
                            ).then(
                                function()
                                {
                                    if(i < nRetry)
                                    {
                                        return f1(++i, idx, names);
                                    }
                                    else
                                    {
                                        return com.deactivateObjectAdapter(adapters[idx]).then(
                                            function()
                                            {
                                                if(names.length > 1)
                                                {
                                                    names.shift();
                                                    return f1(0, ++idx, names);
                                                }
                                            }
                                        );
                                    }
                                });
                        };

                        return f1(0, 0, ArrayUtil.clone(names));
                    }
                ).then(
                    function()
                    {
                        return prx.getAdapterName();
                    }
                ).then(
                    function()
                    {
                        test(false);
                    },
                    function(ex)
                    {
                        test((!isBrowser && ex instanceof Ice.ConnectionRefusedException) ||
                             (isBrowser && ex instanceof Ice.ConnectFailedException));
                        return prx.ice_getEndpoints();
                    }
                ).then(
                    function(endpoints)
                    {
                        var nRetry = 5;
                        var j = 2;
                        var f1 = function(i, names)
                        {
                            return com.createObjectAdapter(names[0], endpoints[j--].toString()).then(
                                function()
                                {
                                    var f2 = function(i, names)
                                    {
                                        return prx.getAdapterName().then(
                                            function(name)
                                            {
                                                test(name === names[0]);
                                                if(i < nRetry)
                                                {
                                                    return f2(++i, names);
                                                }
                                                else if(names.length > 1)
                                                {
                                                    names.shift();
                                                    return f1(0, names);
                                                }
                                            }
                                        );
                                    };
                                    return f2(0, names);
                                }
                            );
                        };
                        return f1(0, ["Adapter66", "Adapter65", "Adapter64"]);
                    }
                ).then(
                    function()
                    {
                        out.writeLine("ok");
                    });
            }
        ).then(
            function()
            {
                return com.shutdown();
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
        var p = new Ice.Promise();
        setTimeout(
            function()
            {
                try
                {
                    allTests(out, id).then(function(){
                            return communicator.destroy();
                        }).then(function(){
                            p.succeed();
                        }).exception(function(ex){
                            p.fail(ex);
                        });
                }
                catch(ex)
                {
                    p.fail(ex);
                }
            });
        return p;
    };

    if(typeof(navigator) !== 'undefined' && isWorker() && (isSafari() || (isWindows() && isChrome())))
    {
        //
        // BUGFIX:
        //
        // With Safari 9.1 and WebWorkers, this test hangs in communicator destruction. The
        // web socket send() method never returns for the sending of close connection message.
        //
        // With Chrome on Windows the Webworker is unexpectelly terminated.
        //
        exports.__test__ = function(out, id)
        {
            if(isSafari())
            {
                out.writeLine("Test not supported with Safari web workers.");
            }
            else if(isWindows() && isChrome())
            {
                out.writeLine("Test not supported with Chrome web workers.");
            }
        };
    }
    else
    {
        exports.__test__ = run;
        exports.__runServer__ = true;
    }
}
(typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? module : undefined,
 typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? require : this.Ice.__require,
 typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? exports : this));
