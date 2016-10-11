# **********************************************************************
#
# Copyright (c) 2003-2016 ZeroC, Inc. All rights reserved.
#
# This copy of Ice is licensed to you under the terms described in the
# ICE_LICENSE file included in this distribution.
#
# **********************************************************************

import Ice, Test, sys, threading, time

def test(b):
    if not b:
        raise RuntimeError('test assertion failed')

class CallbackBase:
    def __init__(self):
        self._called = False
        self._cond = threading.Condition()

    def called(self):
        self._cond.acquire()
        self._called = True
        self._cond.notify()
        self._cond.release()

    def check(self):
        self._cond.acquire()
        while not self._called:
            self._cond.wait()
        self._called = False
        return True

class Callback(CallbackBase):
    def response(self):
        self.called()

    def exception(self, ex):
        test(False)

    def responseEx(self):
        test(False)

    def exceptionEx(self, ex):
        test(isinstance(ex, Ice.TimeoutException))
        self.called()

def allTests(communicator):
    sref = "timeout:default -p 12010"
    obj = communicator.stringToProxy(sref)
    test(obj != None)

    timeout = Test.TimeoutPrx.checkedCast(obj)
    test(timeout != None)

    sys.stdout.write("testing connect timeout... ")
    sys.stdout.flush()
    #
    # Expect ConnectTimeoutException.
    #
    to = Test.TimeoutPrx.uncheckedCast(obj.ice_timeout(100))
    timeout.holdAdapter(500)
    try:
        to.op()
        test(False)
    except Ice.ConnectTimeoutException:
       pass # Expected.
    #
    # Expect success.
    #
    timeout.op() # Ensure adapter is active.
    to = Test.TimeoutPrx.uncheckedCast(obj.ice_timeout(1000))
    timeout.holdAdapter(500)
    try:
        to.op()
    except Ice.ConnectTimeoutException:
        test(False)
    print("ok")

    sys.stdout.write("testing connection timeout... ")
    sys.stdout.flush()
    #
    # Expect TimeoutException.
    #
    if sys.version_info[0] == 2:
        seq = []
        seq[0:10000000] = range(0, 10000000) # add 10,000,000 entries.
        seq = ['\x00' for x in seq] # set them all to \x00
        seq = ''.join(seq) # make into a byte array
    else:
        seq = bytes([0 for x in range(0, 10000000)])
    to = Test.TimeoutPrx.uncheckedCast(obj.ice_timeout(100))
    timeout.holdAdapter(500)
    try:
        to.sendData(seq)
        test(False)
    except Ice.TimeoutException:
       pass # Expected.
    #
    # Expect success.
    #
    timeout.op() # Ensure adapter is active.
    to = Test.TimeoutPrx.uncheckedCast(obj.ice_timeout(1000))
    timeout.holdAdapter(500)
    try:
        if sys.version_info[0] == 2:
            seq2 = []
            seq2[0:1000000] = range(0, 1000000) # add 1,000,000 entries.
            seq2 = ['\x00' for x in seq2] # set them all to \x00
            seq2 = ''.join(seq2) # make into a byte array
        else:
            seq2 = bytes([0 for x in range(0, 1000000)])
        to.sendData(seq2)
    except Ice.TimeoutException:
        test(False)
    print("ok")

    sys.stdout.write("testing invocation timeout... ")
    sys.stdout.flush()
    connection = obj.ice_getConnection();
    to = Test.TimeoutPrx.uncheckedCast(obj.ice_invocationTimeout(100));
    test(connection == to.ice_getConnection());
    try:
        to.sleep(750);
        test(False);
    except Ice.InvocationTimeoutException:
        pass
    obj.ice_ping();
    to = Test.TimeoutPrx.uncheckedCast(obj.ice_invocationTimeout(500));
    test(connection == to.ice_getConnection());
    try:
        to.sleep(250);
    except Ice.InvocationTimeoutException:
        test(False);
    test(connection == to.ice_getConnection());

    # #
    # # Expect InvocationTimeoutException.
    # #
    # to = Test.TimeoutPrx.uncheckedCast(obj.ice_invocationTimeout(250));
    # cb = new Callback();
    # to.begin_sleep(750, newCallback_Timeout_sleep(cb, &Callback.responseEx, &Callback.exceptionEx));
    # cb.check();

    # #
    # # Expect success.
    # #
    # to = Test.TimeoutPrx.uncheckedCast(obj.ice_invocationTimeout(500));
    # cb = new Callback();
    # to.begin_sleep(250, newCallback_Timeout_sleep(cb, &Callback.response, &Callback.exception));
    # cb.check();
    print("ok")

    sys.stdout.write("testing close timeout... ")
    sys.stdout.flush()
    to = Test.TimeoutPrx.checkedCast(obj.ice_timeout(100));
    connection = to.ice_getConnection();
    timeout.holdAdapter(500);
    connection.close(False);
    try:
        connection.getInfo(); # getInfo() doesn't throw in the closing state.
    except Ice.LocalException:
        test(False);
    time.sleep(0.5);
    try:
        connection.getInfo();
        test(False);
    except Ice.CloseConnectionException:
        # Expected.
        pass
    timeout.op(); # Ensure adapter is active.
    print("ok")

    sys.stdout.write("testing timeout overrides... ")
    sys.stdout.flush()

    #
    # Test Ice.Override.Timeout. This property overrides all
    # endpoint timeouts.
    #
    initData = Ice.InitializationData()
    initData.properties = communicator.getProperties().clone()
    initData.properties.setProperty("Ice.Override.Timeout", "100")
    comm = Ice.initialize(initData)
    to = Test.TimeoutPrx.checkedCast(comm.stringToProxy(sref))
    timeout.holdAdapter(500)
    try:
        to.sendData(seq)
        test(False)
    except Ice.TimeoutException:
       pass # Expected.
    #
    # Calling ice_timeout() should have no effect.
    #
    timeout.op() # Ensure adapter is active.
    to = Test.TimeoutPrx.checkedCast(to.ice_timeout(1000))
    timeout.holdAdapter(500);
    try:
        to.sendData(seq)
        test(False)
    except Ice.TimeoutException:
       pass # Expected.
    comm.destroy()
    #
    # Test Ice.Override.ConnectTimeout.
    #
    initData = Ice.InitializationData()
    initData.properties = communicator.getProperties().clone()
    initData.properties.setProperty("Ice.Override.ConnectTimeout", "250")
    comm = Ice.initialize(initData)
    timeout.holdAdapter(750)
    to = Test.TimeoutPrx.uncheckedCast(comm.stringToProxy(sref))
    try:
        to.op()
        test(False)
    except Ice.ConnectTimeoutException:
       pass # Expected.
    #
    # Calling ice_timeout() should have no effect on the connect timeout.
    #
    timeout.op() # Ensure adapter is active.
    timeout.holdAdapter(750)
    to = Test.TimeoutPrx.uncheckedCast(to.ice_timeout(1000))
    try:
        to.op()
        test(False)
    except Ice.ConnectTimeoutException:
       pass # Expected.
    #
    # Verify that timeout set via ice_timeout() is still used for requests.
    #
    timeout.op() # Ensure adapter is active.
    to = Test.TimeoutPrx.uncheckedCast(to.ice_timeout(250));
    to.ice_getConnection(); # Establish connection
    timeout.holdAdapter(750);
    try:
        to.sendData(seq)
        test(False)
    except Ice.TimeoutException:
       pass # Expected.
    comm.destroy()

    #
    # Test Ice.Override.CloseTimeout.
    #
    initData = Ice.InitializationData()
    initData.properties = communicator.getProperties().clone()
    initData.properties.setProperty("Ice.Override.CloseTimeout", "100")
    comm = Ice.initialize(initData)
    connection = comm.stringToProxy(sref).ice_getConnection();
    timeout.holdAdapter(800);
    now = time.clock();
    comm.destroy();
    test((time.clock() - now) < 0.7);

    print("ok")


    return timeout
