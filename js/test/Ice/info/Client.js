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
    var IceSSL = require("ice").IceSSL;
    var Test = require("Test").Test;
    var Promise = Ice.Promise;

    function allTests(communicator, out)
    {
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

        var base, testIntf;

        var defaultHost = communicator.getProperties().getPropertyWithDefault("Ice.Default.Host");

        return Promise.try(
            function()
            {
                out.write("testing proxy endpoint information... ");
                var ref = "test -t:default -h tcphost -p 10000 -t 1200 -z --sourceAddress 10.10.10.10:" +
                      "opaque -e 1.8 -t 100 -v ABCD";
                var p1 = communicator.stringToProxy(ref);

                var endps = p1.ice_getEndpoints();

                var ipEndpoint = endps[0].getInfo();
                test(ipEndpoint.host == "tcphost");
                test(ipEndpoint.port == 10000);
                test(ipEndpoint.timeout == 1200);
                test(ipEndpoint.sourceAddress == "10.10.10.10");
                test(ipEndpoint.compress);
                test(!ipEndpoint.datagram());
                test(ipEndpoint.type() == Ice.TCPEndpointType && !ipEndpoint.secure() ||
                     ipEndpoint.type() == Ice.WSEndpointType && !ipEndpoint.secure() ||
                     ipEndpoint.type() == Ice.WSSEndpointType && ipEndpoint.secure());

                var opaqueEndpoint = endps[1].getInfo();
                test(opaqueEndpoint.rawEncoding.equals(new Ice.EncodingVersion(1, 8)));
            }
        ).then(
            function()
            {
                out.writeLine("ok");

                out.write("testing connection endpoint information... ");
                base = communicator.stringToProxy("test:default -p 12010");
                testIntf = Test.TestIntfPrx.uncheckedCast(base);

                var ipinfo;
                return base.ice_getConnection().then(
                    function(conn)
                    {
                        ipinfo = conn.getEndpoint().getInfo();
                        test(ipinfo.port == 12010);
                        test(!ipinfo.compress);
                        test(ipinfo.host == defaultHost);

                        return testIntf.getEndpointInfoAsContext();
                    }
                ).then(
                    function(ctx)
                    {
                        test(ctx.get("host") == ipinfo.host);
                        test(ctx.get("compress") == "false");
                        var port = parseInt(ctx.get("port"));
                        test(port > 0);
                    }
                );
            }
        ).then(
            function()
            {
                out.writeLine("ok");

                out.write("testing connection information... ");

                var info, ctx, connection;
                return base.ice_getConnection().then(
                    function(conn)
                    {
                        connection = conn;
                        connection.setBufferSize(1024, 2048);

                        info = connection.getInfo();
                        test(!info.incoming);
                        test(info.adapterName.length === 0);
                        if(connection.type() != "ws" && connection.type() != "wss")
                        {
                            test(info.localPort > 0);
                        }
                        test(info.remotePort == 12010);
                        if(defaultHost == "127.0.0.1")
                        {
                            test(info.remoteAddress == defaultHost);
                            if(connection.type() != "ws" && connection.type() != "wss")
                            {
                                test(info.localAddress == defaultHost);
                            }
                        }
                        //test(info.rcvSize >= 1024);
                        test(info.sndSize >= 2048);

                        return testIntf.getConnectionInfoAsContext();
                    }
                ).then(
                    function(c)
                    {
                        ctx = c;

                        test(ctx.get("incoming") == "true");
                        test(ctx.get("adapterName") == "TestAdapter");
                        if(connection.type() != "ws" && connection.type() != "wss")
                        {
                            test(ctx.get("remoteAddress") == info.localAddress);
                            test(ctx.get("localAddress") == info.remoteAddress);
                            test(parseInt(ctx.get("remotePort")) == info.localPort);
                            test(parseInt(ctx.get("localPort")) == info.remotePort);
                        }

                        if(connection.type() == "ws" || connection.type() == "wss")
                        {
                            //test(info.headers["Upgrade"] == "websocket");
                            //test(info.headers.get("Connection") == "Upgrade");
                            //test(info.headers.get("Sec-WebSocket-Protocol") == "ice.zeroc.com");
                            //test(info.headers.get("Sec-WebSocket-Accept") != null);

                            test(ctx.get("ws.Upgrade").toLowerCase() == "websocket");
                            test(ctx.get("ws.Connection").indexOf("Upgrade") >= 0);
                            test(ctx.get("ws.Sec-WebSocket-Protocol") == "ice.zeroc.com");
                            test(ctx.get("ws.Sec-WebSocket-Version") == "13");
                            test(ctx.get("ws.Sec-WebSocket-Key") !== null);
                        }
                    }
                );
            }
        ).then(
            function()
            {
                out.writeLine("ok");

                return testIntf.shutdown();
            }
        );
    }

    var run = function(out, id)
    {
        var communicator = Ice.initialize(id);
        return Promise.try(
            function()
            {
                return allTests(communicator, out);
            }
        ).finally(
            function()
            {
                communicator.destroy();
            }
        );
    };
    exports.__test__ = run;
    exports.__runServer__ = true;
}
(typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? module : undefined,
 typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? require : this.Ice.__require,
 typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? exports : this));
