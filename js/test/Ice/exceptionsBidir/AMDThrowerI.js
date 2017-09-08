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
    var TestAMD = require("TestAMD").TestAMD;
    var Class = Ice.Class;

    var test = function(b)
    {
        if(!b)
        {
            throw new Error("test failed");
        }
    };

    var AMDThrowerI = Class(TestAMD.Thrower, {
        shutdown_async: function(cb, current)
        {
            current.adapter.getCommunicator().shutdown();
            cb.ice_response();
        },

        supportsUndeclaredExceptions_async: function(cb, current)
        {
            cb.ice_response(true);
        },

        supportsAssertException_async: function(cb, current)
        {
            cb.ice_response(false);
        },

        throwAasA_async: function(cb, a, current)
        {
            var ex = new TestAMD.A();
            ex.aMem = a;
            cb.ice_exception(ex);
        },

        throwAorDasAorD_async: function(cb, a, current)
        {
            var ex;
            if(a > 0)
            {
                ex = new TestAMD.A();
                ex.aMem = a;
                cb.ice_exception(ex);
            }
            else
            {
                ex = new TestAMD.D();
                ex.dMem = a;
                cb.ice_exception(ex);
            }
        },

        throwBasA_async: function(cb, a, b, current)
        {
            this.throwBasB_async(cb, a, b, current);
        },

        throwBasB_async: function(cb, a, b, current)
        {
            var ex = new TestAMD.B();
            ex.aMem = a;
            ex.bMem = b;
            cb.ice_exception(ex);
        },

        throwCasA_async: function(cb, a, b, c, current)
        {
            this.throwCasC_async(cb, a, b, c, current);
        },

        throwCasB_async: function(cb, a, b, c, current)
        {
            this.throwCasC_async(cb, a, b, c, current);
        },

        throwCasC_async: function(cb, a, b, c, current)
        {
            var ex = new TestAMD.C();
            ex.aMem = a;
            ex.bMem = b;
            ex.cMem = c;
            cb.ice_exception(ex);
        },

        throwUndeclaredA_async: function(cb, a, current)
        {
            var ex = new TestAMD.A();
            ex.aMem = a;
            cb.ice_exception(ex);
        },

        throwUndeclaredB_async: function(cb, a, b, current)
        {
            var ex = new TestAMD.B();
            ex.aMem = a;
            ex.bMem = b;
            cb.ice_exception(ex);
        },

        throwUndeclaredC_async: function(cb, a, b, c, current)
        {
            var ex = new TestAMD.C();
            ex.aMem = a;
            ex.bMem = b;
            ex.cMem = c;
            cb.ice_exception(ex);
        },

        throwLocalException_async: function(cb, current)
        {
            cb.ice_exception(new Ice.TimeoutException());
        },

        throwLocalExceptionIdempotent_async: function(cb, current)
        {
            cb.ice_exception(new Ice.TimeoutException());
        },

        throwNonIceException_async: function(cb, current)
        {
            cb.ice_exception(new Error());
        },

        throwAssertException_async: function(cb, current)
        {
            test(false);
        },

        throwMemoryLimitException_async: function(cb, seq, current)
        {
            cb.ice_response(Ice.Buffer.createNative(1024 * 20)); // 20KB is over the configured 10KB message size max.
        },

        throwAfterResponse_async: function(cb, current)
        {
            cb.ice_response();

            throw new Error();
        },

        throwAfterException_async: function(cb, current)
        {
            cb.ice_exception(new TestAMD.A());

            throw new Error();
        }
    });

    exports.AMDThrowerI = AMDThrowerI;
}
(typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? module : undefined,
 typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? require : this.Ice.__require,
 typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? exports : this));

