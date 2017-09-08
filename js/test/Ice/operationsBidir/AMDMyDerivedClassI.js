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

    var AMDMyDerivedClassI = Class(TestAMD.MyDerivedClass, {
        //
        // Override the Object "pseudo" operations to verify the operation mode.
        //
        __init__: function()
        {
            this._opByteSOnewayCount = 0;
        },
        ice_isA: function(id, current)
        {
            test(current.mode === Ice.OperationMode.Nonmutating);
            return Ice.Object.prototype.ice_isA.call(this, id, current);
        },
        ice_ping: function(current)
        {
            test(current.mode === Ice.OperationMode.Nonmutating);
            Ice.Object.prototype.ice_ping.call(this, current);
        },
        ice_ids: function(current)
        {
            test(current.mode === Ice.OperationMode.Nonmutating);
            return Ice.Object.prototype.ice_ids.call(this, current);
        },
        ice_id: function(current)
        {
            test(current.mode === Ice.OperationMode.Nonmutating);
            return Ice.Object.prototype.ice_id.call(this, current);
        },
        shutdown_async: function(cb, current)
        {
            current.adapter.getCommunicator().shutdown();
            cb.ice_response();
        },
        delay_async: function(cb, ms, current)
        {
            Ice.Timer.setTimeout(
                function()
                {
                    cb.ice_response();
                }, ms);
        },
        opVoid_async: function(cb, current)
        {
            test(current.mode === Ice.OperationMode.Normal);
            cb.ice_response();
        },
        opBool_async: function(cb, p1, p2, current)
        {
            cb.ice_response(p2, p1);
        },
        opBoolS_async: function(cb, p1, p2, current)
        {
            var p3 = p1.concat(p2);
            cb.ice_response(p1.reverse(), p3);
        },
        opBoolSS_async: function(cb, p1, p2, current)
        {
            var p3 = p1.concat(p2);
            cb.ice_response(p1.reverse(), p3);
        },
        opByte_async: function(cb, p1, p2, current)
        {
            cb.ice_response(p1, (p1 ^ p2) & 0xff);
        },
        opByteBoolD_async: function(cb, p1, p2, current)
        {
            var r = p1.clone();
            r.merge(p2);
            cb.ice_response(r, p1);
        },
        opByteS_async: function(cb, p1, p2, current)
        {
            var i;
            var p3 = Ice.Buffer.createNative(p1.length);
            for(i = 0; i < p1.length; i++)
            {
                p3[i] = p1[p1.length - (i + 1)];
            }

            var r = Ice.Buffer.createNative(p1.length + p2.length);
            for(i = 0; i < p1.length; ++i)
            {
                r[i] = p1[i];
            }
            for(i = 0; i < p2.length; ++i)
            {
                r[i + p1.length] = p2[i];
            }
            cb.ice_response(r, p3);
        },
        opByteSS_async: function(cb, p1, p2, current)
        {
            var r = p1.concat(p2);
            cb.ice_response(r, p1.reverse());
        },
        opFloatDouble_async: function(cb, p1, p2, current)
        {
            cb.ice_response(p2, p1, p2);
        },
        opFloatDoubleS_async: function(cb, p1, p2, current)
        {
            var r = p2.concat(p1);
            var p4 = p2.reverse();
            cb.ice_response(r, p1, p4);
        },
        opFloatDoubleSS_async: function(cb, p1, p2, current)
        {
            var r = p2.concat(p2);
            var p4 = p2.reverse();
            cb.ice_response(r, p1, p4);
        },
        opLongFloatD_async: function(cb, p1, p2, current)
        {
            var r = p1.clone();
            r.merge(p2);
            cb.ice_response(r, p1);
        },
        opMyClass_async: function(cb, p1, current)
        {
            var p2 = p1;
            var p3 = TestAMD.MyClassPrx.uncheckedCast(
                current.adapter.createProxy(current.adapter.getCommunicator().stringToIdentity("noSuchIdentity")));
            var r = TestAMD.MyClassPrx.uncheckedCast(current.adapter.createProxy(current.id));
            cb.ice_response(r, p2, p3);
        },
        opMyEnum_async: function(cb, p1, current)
        {
            cb.ice_response(TestAMD.MyEnum.enum3, p1);
        },
        opShortIntD_async: function(cb, p1, p2, current)
        {
            var r = p1.clone();
            r.merge(p2);
            cb.ice_response(r, p1);
        },
        opShortIntLong_async: function(cb, p1, p2, p3, current)
        {
            cb.ice_response(p3, p1, p2, p3);
        },
        opShortIntLongS_async: function(cb, p1, p2, p3, current)
        {
            cb.ice_response(p3, p1, p2.reverse(), p3.concat(p3));
        },
        opShortIntLongSS_async: function(cb, p1, p2, p3, current)
        {
            cb.ice_response(p3, p1, p2.reverse(), p3.concat(p3));
        },
        opString_async: function(cb, p1, p2, current)
        {
            cb.ice_response(p1 + " " + p2, p2 + " " + p1);
        },
        opStringMyEnumD_async: function(cb, p1, p2, current)
        {
            var r = p1.clone();
            r.merge(p2);
            cb.ice_response(r, p1);
        },
        opMyEnumStringD_async: function(cb, p1, p2, current)
        {
            var r = p1.clone();
            r.merge(p2);
            cb.ice_response(r, p1);
        },
        opMyStructMyEnumD_async: function(cb, p1, p2, current)
        {
            var r = p1.clone();
            r.merge(p2);
            cb.ice_response(r, p1);
        },
        opByteBoolDS_async: function(cb, p1, p2, current)
        {
            var p3 = p2.concat(p1);
            var r = p1.reverse();
            cb.ice_response(r, p3);
        },
        opShortIntDS_async: function(cb, p1, p2, current)
        {
            var p3 = p2.concat(p1);
            var r = p1.reverse();
            cb.ice_response(r, p3);
        },

        opLongFloatDS_async: function(cb, p1, p2, current)
        {
            var p3 = p2.concat(p1);
            var r = p1.reverse();
            cb.ice_response(r, p3);
        },
        opStringStringDS_async: function(cb, p1, p2, current)
        {
            var p3 = p2.concat(p1);
            var r = p1.reverse();
            cb.ice_response(r, p3);
        },
        opStringMyEnumDS_async: function(cb, p1, p2, current)
        {
            var p3 = p2.concat(p1);
            var r = p1.reverse();
            cb.ice_response(r, p3);
        },
        opMyEnumStringDS_async: function(cb, p1, p2, current)
        {
            var p3 = p2.concat(p1);
            var r = p1.reverse();
            cb.ice_response(r, p3);
        },
        opMyStructMyEnumDS_async: function(cb, p1, p2, current)
        {
            var p3 = p2.concat(p1);
            var r = p1.reverse();
            cb.ice_response(r, p3);
        },
        opByteByteSD_async: function(cb, p1, p2, current)
        {
            var r = p1.clone();
            r.merge(p2);
            var p3 = p2.clone();
            cb.ice_response(r, p3);
        },
        opBoolBoolSD_async: function(cb, p1, p2, current)
        {
            var r = p1.clone();
            r.merge(p2);
            var p3 = p2.clone();
            cb.ice_response(r, p3);
        },
        opShortShortSD_async: function(cb, p1, p2, current)
        {
            var r = p1.clone();
            r.merge(p2);
            var p3 = p2.clone();
            cb.ice_response(r, p3);
        },
        opIntIntSD_async: function(cb, p1, p2, current)
        {
            var r = p1.clone();
            r.merge(p2);
            var p3 = p2.clone();
            cb.ice_response(r, p3);
        },
        opLongLongSD_async: function(cb, p1, p2, current)
        {
            var r = p1.clone();
            r.merge(p2);
            var p3 = p2.clone();
            cb.ice_response(r, p3);
        },
        opStringFloatSD_async: function(cb, p1, p2, current)
        {
            var r = p1.clone();
            r.merge(p2);
            var p3 = p2.clone();
            cb.ice_response(r, p3);
        },
        opStringDoubleSD_async: function(cb, p1, p2, current)
        {
            var r = p1.clone();
            r.merge(p2);
            var p3 = p2.clone();
            cb.ice_response(r, p3);
        },
        opStringStringSD_async: function(cb, p1, p2, current)
        {
            var r = p1.clone();
            r.merge(p2);
            var p3 = p2.clone();
            cb.ice_response(r, p3);
        },
        opMyEnumMyEnumSD_async: function(cb, p1, p2, current)
        {
            var r = p1.clone();
            r.merge(p2);
            var p3 = p2.clone();
            cb.ice_response(r, p3);
        },
        opIntS_async: function(cb, s, current)
        {
            cb.ice_response(s.map(function(v, i, arr) { return -v; }));
        },
        opByteSOneway_async: function(cb, s, current)
        {
            this._opByteSOnewayCount += 1;
            cb.ice_response();
        },
        opByteSOnewayCallCount_async: function(cb, current)
        {
            var count = this._opByteSOnewayCount;
            this._opByteSOnewayCount = 0;
            cb.ice_response(count);
        },
        opContext_async: function(cb, current)
        {
            cb.ice_response(current.ctx);
        },
        opDoubleMarshaling_async: function(cb, p1, p2, current)
        {
            var d = 1278312346.0 / 13.0;
            test(p1 === d);
            for(var i = 0; i < p2.length; ++i)
            {
                test(p2[i] === d);
            }
            cb.ice_response();
        },
        opStringS_async: function(cb, p1, p2, current)
        {
            var p3 = p1.concat(p2);
            var r = p1.reverse();
            cb.ice_response(r, p3);
        },
        opStringSS_async: function(cb, p1, p2, current)
        {
            var p3 = p1.concat(p2);
            var r = p2.reverse();
            cb.ice_response(r, p3);
        },
        opStringSSS_async: function(cb, p1, p2, current)
        {
            var p3 = p1.concat(p2);
            var r = p2.reverse();
            cb.ice_response(r, p3);
        },
        opStringStringD_async: function(cb, p1, p2, current)
        {
            var r = p1.clone();
            r.merge(p2);
            cb.ice_response(r, p1);
        },
        opStruct_async: function(cb, p1, p2, current)
        {
            p1.s.s = "a new string";
            cb.ice_response(p2, p1);
        },
        opIdempotent_async: function(cb, current)
        {
            test(current.mode === Ice.OperationMode.Idempotent);
            cb.ice_response();
        },
        opNonmutating_async: function(cb, current)
        {
            test(current.mode === Ice.OperationMode.Nonmutating);
            cb.ice_response();
        },
        opDerived_async: function(cb, current)
        {
            cb.ice_response();
        },
        opByte1_async: function(cb, value, current)
        {
            cb.ice_response(value);
        },
        opShort1_async: function(cb, value, current)
        {
            cb.ice_response(value);
        },
        opInt1_async: function(cb, value, current)
        {
            cb.ice_response(value);
        },
        opLong1_async: function(cb, value, current)
        {
            cb.ice_response(value);
        },
        opFloat1_async: function(cb, value, current)
        {
            cb.ice_response(value);
        },
        opDouble1_async: function(cb, value, current)
        {
            cb.ice_response(value);
        },
        opString1_async: function(cb, value, current)
        {
            cb.ice_response(value);
        },
        opStringS1_async: function(cb, value, current)
        {
            cb.ice_response(value);
        },
        opByteBoolD1_async: function(cb, value, current)
        {
            cb.ice_response(value);
        },
        opStringS2_async: function(cb, value, current)
        {
            cb.ice_response(value);
        },
        opByteBoolD2_async: function(cb, value, current)
        {
            cb.ice_response(value);
        },
        opMyClass1_async: function(cb, value, current)
        {
            return cb.ice_response(value);
        },
        opMyStruct1_async: function(cb, value, current)
        {
            return cb.ice_response(value);
        },
        opStringLiterals_async: function(cb, current)
        {
            return cb.ice_response([
                TestAMD.s0, TestAMD.s1, TestAMD.s2, TestAMD.s3, TestAMD.s4, TestAMD.s5, TestAMD.s6, TestAMD.s7, TestAMD.s8, TestAMD.s9, TestAMD.s10,
                TestAMD.sw0, TestAMD.sw1, TestAMD.sw2, TestAMD.sw3, TestAMD.sw4, TestAMD.sw5, TestAMD.sw6, TestAMD.sw7, TestAMD.sw8, TestAMD.sw9, TestAMD.sw10,
                TestAMD.ss0, TestAMD.ss1, TestAMD.ss2, TestAMD.ss3, TestAMD.ss4, TestAMD.ss5,
                TestAMD.su0, TestAMD.su1, TestAMD.su2]);
        }
    });

    exports.AMDMyDerivedClassI = AMDMyDerivedClassI;
}
(typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? module : undefined,
 typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? require : this.Ice.__require,
 typeof(global) !== "undefined" && typeof(global.process) !== "undefined" ? exports : this));
