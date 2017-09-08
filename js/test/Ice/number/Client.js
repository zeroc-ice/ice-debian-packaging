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
    var Promise = Ice.Promise;

    var test = function(b)
    {
        if(!b)
        {
            throw new Error("test failed");
        }
    };

    var run = function(out)
    {
        return Promise.try(
            function()
            {
                out.write("Testing Ice.Long... ");
                //
                // Test positive numbers
                //
                test(new Ice.Long(0x00000000, 0x00000000).toNumber() === 0);                        // 0
                test(new Ice.Long(0x00000000, 0x00000001).toNumber() === 1);                        // 1
                test(new Ice.Long(0x00000000, 0x00000400).toNumber() === 1024);                     // 1024
                test(new Ice.Long(0x00000000, 0xFFFFFFFF).toNumber() === Math.pow(2, 32) - 1);      // 2^32 - 1
                test(new Ice.Long(0x00000001, 0x00000000).toNumber() === Math.pow(2, 32));          // 2^33
                test(new Ice.Long(0x00000001, 0xFFFFFFFF).toNumber() === Math.pow(2, 33) - 1);      // 2^33 - 1
                test(new Ice.Long(0x001FFFFF, 0xFFFFFFFF).toNumber() === Math.pow(2, 53) - 1);      // 2^53 - 1
                test(new Ice.Long(0x00200000, 0x00000000).toNumber() === Number.POSITIVE_INFINITY); // 2^53

                //
                // Test negative numbers
                //
                test(new Ice.Long(0xFFFFFFFF, 0xFFFFFFFF).toNumber() === -1);
                test(new Ice.Long(0xFFFFFFFF, 0xFFFFFFFE).toNumber() === -2);
                test(new Ice.Long(0xFFFFFFFF, 0xFFFFFF9C).toNumber() === -100);

                test(new Ice.Long(0xFFFFFFFF, 0x00000000).toNumber() === -Math.pow(2, 32));         // -(2^32)
                test(new Ice.Long(0xFFFFFFFE, 0x00000000).toNumber() === -Math.pow(2, 33));         // -(2^33)
                test(new Ice.Long(0xFFFFFFFE, 0x00000001).toNumber() === -(Math.pow(2, 33) - 1));   // -(2^33 - 1)
                test(new Ice.Long(0xFFF00000, 0x00000000).toNumber() === -Math.pow(2, 52));         // -(2^52)
                test(new Ice.Long(0xFFF00000, 0x00000001).toNumber() === -(Math.pow(2, 52) - 1));   // -(2^52 - 1)
                test(new Ice.Long(0xFFE00000, 0x00000001).toNumber() === -(Math.pow(2, 53) - 1));   // -(2^53 - 1)
                test(new Ice.Long(0xFFE00000, 0x00000000).toNumber() === Number.NEGATIVE_INFINITY); // -(2^53)
                out.writeLine("ok");
            });
    };
    exports.__test__ = run;
}
(typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? module : undefined,
 typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? require : this.Ice.__require,
 typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? exports : this));
