// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package Ice;

import java.util.concurrent.Callable;

public final class ConnectionI extends IceInternal.EventHandler
    implements Connection, IceInternal.ResponseHandler, IceInternal.CancellationHandler
{
    public interface StartCallback
    {
        void connectionStartCompleted(ConnectionI connection);

        void connectionStartFailed(ConnectionI connection, Ice.LocalException ex);
    }

    private class TimeoutCallback implements Runnable
    {
        @Override
        public void run()
        {
            timedOut();
        }
    }

    public void start(StartCallback callback)
    {
        try
        {
            synchronized(this)
            {
                // The connection might already be closed if the communicator
                // was destroyed.
                if(_state >= StateClosed)
                {
                    assert (_exception != null);
                    throw (Ice.LocalException) _exception.fillInStackTrace();
                }

                if(!initialize(IceInternal.SocketOperation.None) || !validate(IceInternal.SocketOperation.None))
                {
                    _startCallback = callback;
                    return;
                }

                //
                // We start out in holding state.
                //
                setState(StateHolding);
            }
        }
        catch(Ice.LocalException ex)
        {
            exception(ex);
            callback.connectionStartFailed(this, _exception);
            return;
        }

        callback.connectionStartCompleted(this);
    }

    public void startAndWait() throws InterruptedException
    {
        try
        {
            synchronized(this)
            {
                // The connection might already be closed if the communicator
                // was destroyed.
                if(_state >= StateClosed)
                {
                    assert (_exception != null);
                    throw (Ice.LocalException) _exception.fillInStackTrace();
                }

                if(!initialize(IceInternal.SocketOperation.None) || !validate(IceInternal.SocketOperation.None))
                {
                    while(_state <= StateNotValidated)
                    {
                        wait();
                    }

                    if(_state >= StateClosing)
                    {
                        assert (_exception != null);
                        throw (Ice.LocalException) _exception.fillInStackTrace();
                    }
                }

                //
                // We start out in holding state.
                //
                setState(StateHolding);
            }
        }
        catch(Ice.LocalException ex)
        {
            exception(ex);
            waitUntilFinished();
        }
    }

    public synchronized void activate()
    {
        if(_state <= StateNotValidated)
        {
            return;
        }

        if(_acmLastActivity > 0)
        {
            _acmLastActivity = IceInternal.Time.currentMonotonicTimeMillis();
        }

        setState(StateActive);
    }

    public synchronized void hold()
    {
        if(_state <= StateNotValidated)
        {
            return;
        }

        setState(StateHolding);
    }

    // DestructionReason.
    public final static int ObjectAdapterDeactivated = 0;
    public final static int CommunicatorDestroyed = 1;

    synchronized public void destroy(int reason)
    {
        switch(reason)
        {
            case ObjectAdapterDeactivated:
            {
                setState(StateClosing, new ObjectAdapterDeactivatedException());
                break;
            }

            case CommunicatorDestroyed:
            {
                setState(StateClosing, new CommunicatorDestroyedException());
                break;
            }
        }
    }

    @Override
    synchronized public void close(boolean force)
    {
        if(Thread.interrupted())
        {
            throw new Ice.OperationInterruptedException();
        }

        if(force)
        {
            setState(StateClosed, new ForcedCloseConnectionException());
        }
        else
        {
            //
            // If we do a graceful shutdown, then we wait until all
            // outstanding requests have been completed. Otherwise,
            // the CloseConnectionException will cause all outstanding
            // requests to be retried, regardless of whether the
            // server has processed them or not.
            //
            while(!_asyncRequests.isEmpty())
            {
                try
                {
                    wait();
                }
                catch(InterruptedException ex)
                {
                    throw new Ice.OperationInterruptedException();
                }
            }

            setState(StateClosing, new CloseConnectionException());
        }
    }

    public synchronized boolean isActiveOrHolding()
    {
        return _state > StateNotValidated && _state < StateClosing;
    }

    public synchronized boolean isFinished()
    {
        if(_state != StateFinished || _dispatchCount != 0)
        {
            return false;
        }

        assert (_state == StateFinished);
        return true;
    }

    public synchronized void throwException()
    {
        if(_exception != null)
        {
            assert (_state >= StateClosing);
            throw (Ice.LocalException) _exception.fillInStackTrace();
        }
    }

    public synchronized void waitUntilHolding() throws InterruptedException
    {
        while(_state < StateHolding || _dispatchCount > 0)
        {
            wait();
        }
    }

    public synchronized void waitUntilFinished() throws InterruptedException
    {
        //
        // We wait indefinitely until the connection is finished and all
        // outstanding requests are completed. Otherwise we couldn't
        // guarantee that there are no outstanding calls when deactivate()
        // is called on the servant locators.
        //
        while(_state < StateFinished || _dispatchCount > 0)
        {
            wait();
        }

        assert (_state == StateFinished);

        //
        // Clear the OA. See bug 1673 for the details of why this is necessary.
        //
        _adapter = null;
    }

    synchronized public void updateObserver()
    {
        if(_state < StateNotValidated || _state > StateClosed)
        {
            return;
        }

        assert (_instance.initializationData().observer != null);
        _observer = _instance.initializationData().observer.getConnectionObserver(initConnectionInfo(),
                                                                                  _endpoint,
                                                                                  toConnectionState(_state),
                                                                                  _observer);
        if(_observer != null)
        {
            _observer.attach();
        }
        else
        {
            _writeStreamPos = -1;
            _readStreamPos = -1;
        }
    }

    synchronized public void monitor(long now, IceInternal.ACMConfig acm)
    {
        if(_state != StateActive)
        {
            return;
        }

        //
        // We send a heartbeat if there was no activity in the last
        // (timeout / 4) period. Sending a heartbeat sooner than
        // really needed is safer to ensure that the receiver will
        // receive in time the heartbeat. Sending the heartbeat if
        // there was no activity in the last (timeout / 2) period
        // isn't enough since monitor() is called only every (timeout
        // / 2) period.
        //
        // Note that this doesn't imply that we are sending 4
        // heartbeats per timeout period because the monitor() method
        // is sill only called every (timeout / 2) period.
        //
        if(acm.heartbeat == ACMHeartbeat.HeartbeatAlways ||
           (acm.heartbeat != ACMHeartbeat.HeartbeatOff && _writeStream.isEmpty() &&
            now >= (_acmLastActivity + acm.timeout / 4)))
        {
            if(acm.heartbeat != ACMHeartbeat.HeartbeatOnInvocation || _dispatchCount > 0)
            {
                heartbeat();
            }
        }

        if(_readStream.size() > IceInternal.Protocol.headerSize || !_writeStream.isEmpty())
        {
            //
            // If writing or reading, nothing to do, the connection
            // timeout will kick-in if writes or reads don't progress.
            // This check is necessary because the activity timer is
            // only set when a message is fully read/written.
            //
            return;
        }

        if(acm.close != ACMClose.CloseOff && now >= (_acmLastActivity + acm.timeout))
        {
            if(acm.close == ACMClose.CloseOnIdleForceful ||
               (acm.close != ACMClose.CloseOnIdle && (!_asyncRequests.isEmpty())))
            {
                //
                // Close the connection if we didn't receive a heartbeat in
                // the last period.
                //
                setState(StateClosed, new ConnectionTimeoutException());
            }
            else if(acm.close != ACMClose.CloseOnInvocation && _dispatchCount == 0 && _batchRequestQueue.isEmpty() &&
                    _asyncRequests.isEmpty())
            {
                //
                // The connection is idle, close it.
                //
                setState(StateClosing, new ConnectionTimeoutException());
            }
        }
    }

    synchronized public int
    sendAsyncRequest(IceInternal.OutgoingAsyncBase out, boolean compress, boolean response, int batchRequestNum)
            throws IceInternal.RetryException
    {
        final IceInternal.BasicStream os = out.getOs();

        if(_exception != null)
        {
            //
            // If the connection is closed before we even have a chance
            // to send our request, we always try to send the request
            // again.
            //
            throw new IceInternal.RetryException((Ice.LocalException) _exception.fillInStackTrace());
        }

        assert (_state > StateNotValidated);
        assert (_state < StateClosing);

        //
        // Ensure the message isn't bigger than what we can send with the
        // transport.
        //
        _transceiver.checkSendSize(os.getBuffer());

        //
        // Notify the request that it's cancelable with this connection.
        // This will throw if the request is canceled.
        //
        out.cancelable(this);

        int requestId = 0;
        if(response)
        {
            //
            // Create a new unique request ID.
            //
            requestId = _nextRequestId++;
            if(requestId <= 0)
            {
                _nextRequestId = 1;
                requestId = _nextRequestId++;
            }

            //
            // Fill in the request ID.
            //
            os.pos(IceInternal.Protocol.headerSize);
            os.writeInt(requestId);
        }
        else if(batchRequestNum > 0)
        {
            os.pos(IceInternal.Protocol.headerSize);
            os.writeInt(batchRequestNum);
        }

        out.attachRemoteObserver(initConnectionInfo(), _endpoint, requestId);

        int status;
        try
        {
            status = sendMessage(new OutgoingMessage(out, os, compress, requestId));
        }
        catch(Ice.LocalException ex)
        {
            setState(StateClosed, ex);
            assert (_exception != null);
            throw (Ice.LocalException) _exception.fillInStackTrace();
        }

        if(response)
        {
            //
            // Add to the async requests map.
            //
            _asyncRequests.put(requestId, out);
        }
        return status;
    }

    public IceInternal.BatchRequestQueue
    getBatchRequestQueue()
    {
        return _batchRequestQueue;
    }

    @Override
    public void flushBatchRequests()
    {
        end_flushBatchRequests(begin_flushBatchRequests());
    }

    private static final String __flushBatchRequests_name = "flushBatchRequests";

    @Override
    public Ice.AsyncResult begin_flushBatchRequests()
    {
        return begin_flushBatchRequestsInternal(null);
    }

    @Override
    public Ice.AsyncResult begin_flushBatchRequests(Callback cb)
    {
        return begin_flushBatchRequestsInternal(cb);
    }

    @Override
    public Ice.AsyncResult begin_flushBatchRequests(Callback_Connection_flushBatchRequests cb)
    {
        return begin_flushBatchRequestsInternal(cb);
    }

    @Override
    public AsyncResult begin_flushBatchRequests(IceInternal.Functional_VoidCallback __responseCb,
            IceInternal.Functional_GenericCallback1<Ice.Exception> __exceptionCb,
            IceInternal.Functional_BoolCallback __sentCb)
    {
        return begin_flushBatchRequestsInternal(new IceInternal.Functional_CallbackBase(false, __exceptionCb, __sentCb)
        {
            @Override
            public final void __completed(AsyncResult __result)
            {
                try
                {
                    __result.getConnection().end_flushBatchRequests(__result);
                }
                catch(Exception __ex)
                {
                    __exceptionCb.apply(__ex);
                }
            }
        });
    }

    private Ice.AsyncResult begin_flushBatchRequestsInternal(IceInternal.CallbackBase cb)
    {
        IceInternal.ConnectionFlushBatch result =
            new IceInternal.ConnectionFlushBatch(this, _communicator, _instance, __flushBatchRequests_name, cb);
        result.invoke();
        return result;
    }

    @Override
    public void end_flushBatchRequests(AsyncResult ir)
    {
        IceInternal.ConnectionFlushBatch r =
            IceInternal.ConnectionFlushBatch.check(ir, this, __flushBatchRequests_name);
        r.__wait();
    }

    @Override
    synchronized public void setCallback(final ConnectionCallback callback)
    {
        synchronized(this)
        {
            if(_state >= StateClosed)
            {
                if(callback != null)
                {
                    _threadPool.dispatch(new IceInternal.DispatchWorkItem(this)
                    {
                        @Override
                        public void run()
                        {
                            try
                            {
                                callback.closed(ConnectionI.this);
                            }
                            catch(Exception ex)
                            {
                                _logger.error("connection callback exception:\n" + ex + '\n' + _desc);
                            }
                        }
                    });
                }
            }
            else
            {
                _callback = callback;
            }
        }
    }

    @Override
    synchronized public void setACM(Ice.IntOptional timeout, Ice.Optional<ACMClose> close,
            Ice.Optional<ACMHeartbeat> heartbeat)
    {
        if(_monitor == null || _state >= StateClosed)
        {
            return;
        }

        if(_state == StateActive)
        {
            _monitor.remove(this);
        }
        _monitor = _monitor.acm(timeout, close, heartbeat);

        if(_monitor.getACM().timeout <= 0)
        {
            _acmLastActivity = -1; // Disable the recording of last activity.
        }
        else if(_state == StateActive && _acmLastActivity == -1)
        {
            _acmLastActivity = IceInternal.Time.currentMonotonicTimeMillis();
        }

        if(_state == StateActive)
        {
            _monitor.add(this);
        }
    }

    @Override
    synchronized public Ice.ACM getACM()
    {
        return _monitor != null ? _monitor.getACM() : new ACM(0, ACMClose.CloseOff, ACMHeartbeat.HeartbeatOff);
    }

    @Override
    synchronized public void asyncRequestCanceled(IceInternal.OutgoingAsyncBase outAsync, Ice.LocalException ex)
    {
        if(_state >= StateClosed)
        {
            return; // The request has already been or will be shortly notified of the failure.
        }

        java.util.Iterator<OutgoingMessage> it = _sendStreams.iterator();
        while(it.hasNext())
        {
            OutgoingMessage o = it.next();
            if(o.outAsync == outAsync)
            {
                if(o.requestId > 0)
                {
                    _asyncRequests.remove(o.requestId);
                }

                if(ex instanceof ConnectionTimeoutException)
                {
                    setState(StateClosed, ex);
                }
                else
                {
                    //
                    // If the request is being sent, don't remove it from the send
                    // streams, it will be removed once the sending is finished.
                    //
                    // Note that since we swapped the message stream to _writeStream
                    // it's fine if the OutgoingAsync output stream is released (and
                    // as long as canceled requests cannot be retried).
                    //
                    o.canceled();
                    if(o != _sendStreams.getFirst())
                    {
                        it.remove();
                    }
                    if(outAsync.completed(ex))
                    {
                        outAsync.invokeCompletedAsync();
                    }
                }
                return;
            }
        }

        if(outAsync instanceof IceInternal.OutgoingAsync)
        {
            IceInternal.OutgoingAsync o = (IceInternal.OutgoingAsync) outAsync;
            java.util.Iterator<IceInternal.OutgoingAsyncBase> it2 = _asyncRequests.values().iterator();
            while(it2.hasNext())
            {
                if(it2.next() == o)
                {
                    if(ex instanceof ConnectionTimeoutException)
                    {
                        setState(StateClosed, ex);
                    }
                    else
                    {
                        it2.remove();
                        if(outAsync.completed(ex))
                        {
                            outAsync.invokeCompletedAsync();
                        }
                    }
                    return;
                }
            }
        }
    }

    @Override
    synchronized public void sendResponse(int requestId, IceInternal.BasicStream os, byte compressFlag, boolean amd)
    {
        assert (_state > StateNotValidated);

        try
        {
            if(--_dispatchCount == 0)
            {
                if(_state == StateFinished)
                {
                    reap();
                }
                notifyAll();
            }

            if(_state >= StateClosed)
            {
                assert (_exception != null);
                throw (Ice.LocalException) _exception.fillInStackTrace();
            }

            sendMessage(new OutgoingMessage(os, compressFlag != 0, true));

            if(_state == StateClosing && _dispatchCount == 0)
            {
                initiateShutdown();
            }
        }
        catch(LocalException ex)
        {
            setState(StateClosed, ex);
        }
    }

    @Override
    synchronized public void sendNoResponse()
    {
        assert (_state > StateNotValidated);
        try
        {
            if(--_dispatchCount == 0)
            {
                if(_state == StateFinished)
                {
                    reap();
                }
                notifyAll();
            }

            if(_state >= StateClosed)
            {
                assert (_exception != null);
                throw (Ice.LocalException) _exception.fillInStackTrace();
            }

            if(_state == StateClosing && _dispatchCount == 0)
            {
                initiateShutdown();
            }
        }
        catch(LocalException ex)
        {
            setState(StateClosed, ex);
        }
    }

    @Override
    public boolean systemException(int requestId, Ice.SystemException ex, boolean amd)
    {
        return false; // System exceptions aren't marshalled.
    }

    @Override
    public synchronized void invokeException(int requestId, LocalException ex, int invokeNum, boolean amd)
    {
        //
        // Fatal exception while invoking a request. Since
        // sendResponse/sendNoResponse isn't
        // called in case of a fatal exception we decrement _dispatchCount here.
        //

        setState(StateClosed, ex);

        if(invokeNum > 0)
        {
            assert (_dispatchCount > 0);
            _dispatchCount -= invokeNum;
            assert (_dispatchCount >= 0);
            if(_dispatchCount == 0)
            {
                if(_state == StateFinished)
                {
                    reap();
                }
                notifyAll();
            }
        }
    }

    public IceInternal.EndpointI endpoint()
    {
        return _endpoint; // No mutex protection necessary, _endpoint is
                          // immutable.
    }

    public IceInternal.Connector connector()
    {
        return _connector; // No mutex protection necessary, _connector is
                           // immutable.
    }

    @Override
    public synchronized void setAdapter(ObjectAdapter adapter)
    {
        if(_state <= StateNotValidated || _state >= StateClosing)
        {
            return;
        }

        _adapter = adapter;

        if(_adapter != null)
        {
            _servantManager = ((ObjectAdapterI) _adapter).getServantManager();
            if(_servantManager == null)
            {
                _adapter = null;
            }
        }
        else
        {
            _servantManager = null;
        }

        //
        // We never change the thread pool with which we were
        // initially registered, even if we add or remove an object
        // adapter.
        //
    }

    @Override
    public synchronized ObjectAdapter getAdapter()
    {
        return _adapter;
    }

    @Override
    public Endpoint getEndpoint()
    {
        return _endpoint; // No mutex protection necessary, _endpoint is
                          // immutable.
    }

    @Override
    public ObjectPrx createProxy(Identity ident)
    {
        //
        // Create a reference and return a reverse proxy for this
        // reference.
        //
        return _instance.proxyFactory().referenceToProxy(_instance.referenceFactory().create(ident, this));
    }

    //
    // Operations from EventHandler
    //
    @Override
    public void message(IceInternal.ThreadPoolCurrent current)
    {
        StartCallback startCB = null;
        java.util.List<OutgoingMessage> sentCBs = null;
        MessageInfo info = null;
        int dispatchCount = 0;

        synchronized(this)
        {
            if(_state >= StateClosed)
            {
                return;
            }

            if(!current.ioReady())
            {
                return;
            }

            int readyOp = current.operation;
            try
            {
                unscheduleTimeout(current.operation);

                int writeOp = IceInternal.SocketOperation.None;
                int readOp = IceInternal.SocketOperation.None;

                if((readyOp & IceInternal.SocketOperation.Write) != 0)
                {
                    final IceInternal.Buffer buf = _writeStream.getBuffer();
                    if(_observer != null)
                    {
                        observerStartWrite(buf);
                    }
                    writeOp = write(buf);
                    if(_observer != null && (writeOp & IceInternal.SocketOperation.Write) == 0)
                    {
                        observerFinishWrite(buf);
                    }
                }

                while((readyOp & IceInternal.SocketOperation.Read) != 0)
                {
                    final IceInternal.Buffer buf = _readStream.getBuffer();
                    if(_observer != null && !_readHeader)
                    {
                        observerStartRead(buf);
                    }

                    readOp = read(buf);
                    if((readOp & IceInternal.SocketOperation.Read) != 0)
                    {
                        break;
                    }
                    if(_observer != null && !_readHeader)
                    {
                        assert (!buf.b.hasRemaining());
                        observerFinishRead(buf);
                    }

                    if(_readHeader) // Read header if necessary.
                    {
                        _readHeader = false;

                        if(_observer != null)
                        {
                            _observer.receivedBytes(IceInternal.Protocol.headerSize);
                        }

                        int pos = _readStream.pos();
                        if(pos < IceInternal.Protocol.headerSize)
                        {
                            //
                            // This situation is possible for small UDP packets.
                            //
                            throw new Ice.IllegalMessageSizeException();
                        }

                        _readStream.pos(0);
                        byte[] m = new byte[4];
                        m[0] = _readStream.readByte();
                        m[1] = _readStream.readByte();
                        m[2] = _readStream.readByte();
                        m[3] = _readStream.readByte();
                        if(m[0] != IceInternal.Protocol.magic[0] || m[1] != IceInternal.Protocol.magic[1] ||
                           m[2] != IceInternal.Protocol.magic[2] || m[3] != IceInternal.Protocol.magic[3])
                        {
                            Ice.BadMagicException ex = new Ice.BadMagicException();
                            ex.badMagic = m;
                            throw ex;
                        }

                        _readProtocol.__read(_readStream);
                        IceInternal.Protocol.checkSupportedProtocol(_readProtocol);

                        _readProtocolEncoding.__read(_readStream);
                        IceInternal.Protocol.checkSupportedProtocolEncoding(_readProtocolEncoding);

                        _readStream.readByte(); // messageType
                        _readStream.readByte(); // compress
                        int size = _readStream.readInt();
                        if(size < IceInternal.Protocol.headerSize)
                        {
                            throw new Ice.IllegalMessageSizeException();
                        }
                        if(size > _messageSizeMax)
                        {
                            IceInternal.Ex.throwMemoryLimitException(size, _messageSizeMax);
                        }
                        if(size > _readStream.size())
                        {
                            _readStream.resize(size, true);
                        }
                        _readStream.pos(pos);
                    }

                    if(_readStream.pos() != _readStream.size())
                    {
                        if(_endpoint.datagram())
                        {
                            // The message was truncated.
                            throw new Ice.DatagramLimitException();
                        }
                        continue;
                    }
                    break;
                }

                int newOp = readOp | writeOp;
                readyOp = readyOp & ~newOp;
                assert (readyOp != 0 || newOp != 0);

                if(_state <= StateNotValidated)
                {
                    if(newOp != 0)
                    {
                        //
                        // Wait for all the transceiver conditions to be
                        // satisfied before continuing.
                        //
                        scheduleTimeout(newOp);
                        _threadPool.update(this, current.operation, newOp);
                        return;
                    }

                    if(_state == StateNotInitialized && !initialize(current.operation))
                    {
                        return;
                    }

                    if(_state <= StateNotValidated && !validate(current.operation))
                    {
                        return;
                    }

                    _threadPool.unregister(this, current.operation);

                    //
                    // We start out in holding state.
                    //
                    setState(StateHolding);
                    if(_startCallback != null)
                    {
                        startCB = _startCallback;
                        _startCallback = null;
                        if(startCB != null)
                        {
                            ++dispatchCount;
                        }
                    }
                }
                else
                {
                    assert (_state <= StateClosingPending);

                    //
                    // We parse messages first, if we receive a close
                    // connection message we won't send more messages.
                    //
                    if((readyOp & IceInternal.SocketOperation.Read) != 0)
                    {
                        // Optimization: use the thread's stream.
                        info = new MessageInfo(current.stream);
                        newOp |= parseMessage(info);
                        dispatchCount += info.messageDispatchCount;
                    }

                    if((readyOp & IceInternal.SocketOperation.Write) != 0)
                    {
                        sentCBs = new java.util.LinkedList<OutgoingMessage>();
                        newOp |= sendNextMessage(sentCBs);
                        if(!sentCBs.isEmpty())
                        {
                            ++dispatchCount;
                        }
                        else
                        {
                            sentCBs = null;
                        }
                    }

                    if(_state < StateClosed)
                    {
                        scheduleTimeout(newOp);
                        _threadPool.update(this, current.operation, newOp);
                    }
                }

                if(_acmLastActivity > 0)
                {
                    _acmLastActivity = IceInternal.Time.currentMonotonicTimeMillis();
                }

                if(dispatchCount == 0)
                {
                    return; // Nothing to dispatch we're done!
                }

                _dispatchCount += dispatchCount;
                current.ioCompleted();
            }
            catch(DatagramLimitException ex) // Expected.
            {
                if(_warnUdp)
                {
                    _logger.warning("maximum datagram size of " + _readStream.pos() + " exceeded");
                }
                _readStream.resize(IceInternal.Protocol.headerSize, true);
                _readStream.pos(0);
                _readHeader = true;
                return;
            }
            catch(SocketException ex)
            {
                setState(StateClosed, ex);
                return;
            }
            catch(LocalException ex)
            {
                if(_endpoint.datagram())
                {
                    if(_warn)
                    {
                        String s = "datagram connection exception:\n" + ex + '\n' + _desc;
                        _logger.warning(s);
                    }
                    _readStream.resize(IceInternal.Protocol.headerSize, true);
                    _readStream.pos(0);
                    _readHeader = true;
                }
                else
                {
                    setState(StateClosed, ex);
                }
                return;
            }
        }

        if(!_dispatcher) // Optimization, call dispatch() directly if there's no
                         // dispatcher.
        {
            dispatch(startCB, sentCBs, info);
        }
        else
        {
            // No need for the stream if heartbeat callback
            if(info != null && info.heartbeatCallback == null)
            {
                //
                // Create a new stream for the dispatch instead of using the
                // thread pool's thread stream.
                //
                assert (info.stream == current.stream);
                IceInternal.BasicStream stream = info.stream;
                info.stream = new IceInternal.BasicStream(_instance, IceInternal.Protocol.currentProtocolEncoding);
                info.stream.swap(stream);
            }

            final StartCallback finalStartCB = startCB;
            final java.util.List<OutgoingMessage> finalSentCBs = sentCBs;
            final MessageInfo finalInfo = info;
            _threadPool.dispatchFromThisThread(new IceInternal.DispatchWorkItem(this)
            {
                @Override
                public void run()
                {
                    dispatch(finalStartCB, finalSentCBs, finalInfo);
                }
            });
        }
    }

    protected void dispatch(StartCallback startCB, java.util.List<OutgoingMessage> sentCBs, MessageInfo info)
    {
        int dispatchedCount = 0;

        //
        // Notify the factory that the connection establishment and
        // validation has completed.
        //
        if(startCB != null)
        {
            startCB.connectionStartCompleted(this);
            ++dispatchedCount;
        }

        //
        // Notify AMI calls that the message was sent.
        //
        if(sentCBs != null)
        {
            for(OutgoingMessage msg : sentCBs)
            {
                msg.outAsync.invokeSent();
            }
            ++dispatchedCount;
        }

        if(info != null)
        {
            //
            // Asynchronous replies must be handled outside the thread
            // synchronization, so that nested calls are possible.
            //
            if(info.outAsync != null)
            {
                info.outAsync.invokeCompleted();
                ++dispatchedCount;
            }

            if(info.heartbeatCallback != null)
            {
                try
                {
                    info.heartbeatCallback.heartbeat(this);
                }
                catch(Exception ex)
                {
                    _logger.error("connection callback exception:\n" + ex + '\n' + _desc);
                }
                ++dispatchedCount;
            }

            //
            // Method invocation (or multiple invocations for batch messages)
            // must be done outside the thread synchronization, so that nested
            // calls are possible.
            //
            if(info.invokeNum > 0)
            {
                invokeAll(info.stream, info.invokeNum, info.requestId, info.compress, info.servantManager, info.adapter);

                //
                // Don't increase dispatchedCount, the dispatch count is
                // decreased when the incoming reply is sent.
                //
            }
        }

        //
        // Decrease dispatch count.
        //
        if(dispatchedCount > 0)
        {
            boolean queueShutdown = false;

            synchronized(this)
            {
                _dispatchCount -= dispatchedCount;
                if(_dispatchCount == 0)
                {
                    //
                    // Only initiate shutdown if not already done. It might
                    // have already been done if the sent callback or AMI
                    // callback was dispatched when the connection was already
                    // in the closing state.
                    //
                    if(_state == StateClosing)
                    {
                        if(_instance.queueRequests())
                        {
                            //
                            // We can't call initiateShutdown() from this thread in certain
                            // situations (such as in Android).
                            //
                            queueShutdown = true;
                        }
                        else
                        {
                            try
                            {
                                initiateShutdown();
                            }
                            catch(Ice.LocalException ex)
                            {
                                setState(StateClosed, ex);
                            }
                        }
                    }
                    else if(_state == StateFinished)
                    {
                        reap();
                    }
                    if(!queueShutdown)
                    {
                        notifyAll();
                    }
                }
            }

            if(queueShutdown)
            {
                _instance.getQueueExecutor().executeNoThrow(new Callable<Void>()
                {
                    @Override
                    public Void call() throws Exception
                    {
                        synchronized(ConnectionI.this)
                        {
                            try
                            {
                                initiateShutdown();
                            }
                            catch(Ice.LocalException ex)
                            {
                                setState(StateClosed, ex);
                            }
                            ConnectionI.this.notifyAll();
                        }
                        return null;
                    }
                });
            }
        }
    }

    @Override
    public void finished(IceInternal.ThreadPoolCurrent current, final boolean close)
    {
        synchronized(this)
        {
            assert (_state == StateClosed);
            unscheduleTimeout(IceInternal.SocketOperation.Read | IceInternal.SocketOperation.Write);
        }

        //
        // If there are no callbacks to call, we don't call ioCompleted() since
        // we're not going to call code that will potentially block (this avoids
        // promoting a new leader and unecessary thread creation, especially if
        // this is called on shutdown).
        //
        if(_startCallback == null && _sendStreams.isEmpty() && _asyncRequests.isEmpty() && _callback == null)
        {
            finish(close);
            return;
        }

        current.ioCompleted();
        if(!_dispatcher) // Optimization, call finish() directly if there's no
                         // dispatcher.
        {
            finish(close);
        }
        else
        {
            _threadPool.dispatchFromThisThread(new IceInternal.DispatchWorkItem(this)
            {
                @Override
                public void run()
                {
                    finish(close);
                }
            });
        }
    }

    public void finish(boolean close)
    {
        if(!_initialized)
        {
            if(_instance.traceLevels().network >= 2)
            {
                StringBuffer s = new StringBuffer("failed to ");
                s.append(_connector != null ? "establish" : "accept");
                s.append(" ");
                s.append(_endpoint.protocol());
                s.append(" connection\n");
                s.append(toString());
                s.append("\n");
                s.append(_exception);
                _instance.initializationData().logger.trace(_instance.traceLevels().networkCat, s.toString());
            }
        }
        else
        {
            if(_instance.traceLevels().network >= 1)
            {
                StringBuffer s = new StringBuffer("closed ");
                s.append(_endpoint.protocol());
                s.append(" connection\n");
                s.append(toString());

                //
                // Trace the cause of unexpected connection closures
                //
                if(!(_exception instanceof CloseConnectionException ||
                     _exception instanceof ForcedCloseConnectionException ||
                     _exception instanceof ConnectionTimeoutException ||
                     _exception instanceof CommunicatorDestroyedException ||
                     _exception instanceof ObjectAdapterDeactivatedException))
                {
                    s.append("\n");
                    s.append(_exception);
                }
                _instance.initializationData().logger.trace(_instance.traceLevels().networkCat, s.toString());
            }
        }

        if(close)
        {
            _transceiver.close();
        }

        if(_startCallback != null)
        {
            if(_instance.queueRequests())
            {
                // The connectStartFailed method might try to connect with another
                // connector.
                _instance.getQueueExecutor().executeNoThrow(new Callable<Void>()
                {
                    @Override
                    public Void call() throws Exception
                    {
                        _startCallback.connectionStartFailed(ConnectionI.this, _exception);
                        return null;
                    }
                });
            }
            else
            {
                _startCallback.connectionStartFailed(this, _exception);
            }
            _startCallback = null;
        }

        if(!_sendStreams.isEmpty())
        {
            if(!_writeStream.isEmpty())
            {
                //
                // Return the stream to the outgoing call. This is important for
                // retriable AMI calls which are not marshalled again.
                //
                OutgoingMessage message = _sendStreams.getFirst();
                _writeStream.swap(message.stream);
            }

            for(OutgoingMessage p : _sendStreams)
            {
                p.completed(_exception);
                if(p.requestId > 0) // Make sure finished isn't called twice.
                {
                    _asyncRequests.remove(p.requestId);
                }
            }
            _sendStreams.clear();
        }

        for(IceInternal.OutgoingAsyncBase p : _asyncRequests.values())
        {
            if(p.completed(_exception))
            {
                p.invokeCompleted();
            }
        }
        _asyncRequests.clear();

        //
        // Don't wait to be reaped to reclaim memory allocated by read/write streams.
        //
        _writeStream.clear();
        _writeStream.getBuffer().clear();
        _readStream.clear();
        _readStream.getBuffer().clear();

        if(_callback != null)
        {
            try
            {
                _callback.closed(this);
            }
            catch(Exception ex)
            {
                _logger.error("connection callback exception:\n" + ex + '\n' + _desc);
            }
            _callback = null;
        }

        //
        // This must be done last as this will cause waitUntilFinished() to
        // return (and communicator objects such as the timer might be destroyed
        // too).
        //
        synchronized(this)
        {
            setState(StateFinished);

            if(_dispatchCount == 0)
            {
                reap();
            }
        }
    }

    @Override
    public String toString()
    {
        return _toString();
    }

    @Override
    public java.nio.channels.SelectableChannel fd()
    {
        return _transceiver.fd();
    }

    public synchronized void timedOut()
    {
        if(_state <= StateNotValidated)
        {
            setState(StateClosed, new ConnectTimeoutException());
        }
        else if(_state < StateClosing)
        {
            setState(StateClosed, new TimeoutException());
        }
        else if(_state < StateClosed)
        {
            setState(StateClosed, new CloseTimeoutException());
        }
    }

    @Override
    public String type()
    {
        return _type; // No mutex lock, _type is immutable.
    }

    @Override
    public int timeout()
    {
        return _endpoint.timeout(); // No mutex protection necessary, _endpoint
                                    // is immutable.
    }

    @Override
    public synchronized ConnectionInfo getInfo()
    {
        if(_state >= StateClosed)
        {
            throw (Ice.LocalException) _exception.fillInStackTrace();
        }
        return initConnectionInfo();
    }

    @Override
    public synchronized void setBufferSize(int rcvSize, int sndSize)
    {
        if(_state >= StateClosed)
        {
            throw (Ice.LocalException) _exception.fillInStackTrace();
        }
        _transceiver.setBufferSize(rcvSize, sndSize);
        _info = null; // Invalidate the cached connection info
    }

    @Override
    public String _toString()
    {
        return _desc; // No mutex lock, _desc is immutable.
    }

    public synchronized void exception(LocalException ex)
    {
        setState(StateClosed, ex);
    }

    public ConnectionI(Communicator communicator, IceInternal.Instance instance, IceInternal.ACMMonitor monitor,
                       IceInternal.Transceiver transceiver, IceInternal.Connector connector,
                       IceInternal.EndpointI endpoint, ObjectAdapterI adapter)
    {
        _communicator = communicator;
        _instance = instance;
        _monitor = monitor;
        _transceiver = transceiver;
        _desc = transceiver.toString();
        _type = transceiver.protocol();
        _connector = connector;
        _endpoint = endpoint;
        _adapter = adapter;
        final Ice.InitializationData initData = instance.initializationData();
        // Cached for better performance.
        _dispatcher = initData.dispatcher != null;
        _logger = initData.logger; // Cached for better performance.
        _traceLevels = instance.traceLevels(); // Cached for better performance.
        _timer = instance.timer();
        _writeTimeout = new TimeoutCallback();
        _writeTimeoutFuture = null;
        _readTimeout = new TimeoutCallback();
        _readTimeoutFuture = null;
        _warn = initData.properties.getPropertyAsInt("Ice.Warn.Connections") > 0;
        _warnUdp = instance.initializationData().properties.getPropertyAsInt("Ice.Warn.Datagrams") > 0;
        _cacheBuffers = instance.cacheMessageBuffers();
        if(_monitor != null && _monitor.getACM().timeout > 0)
        {
            _acmLastActivity = IceInternal.Time.currentMonotonicTimeMillis();
        }
        else
        {
            _acmLastActivity = -1;
        }
        _nextRequestId = 1;
        _messageSizeMax = adapter != null ? adapter.messageSizeMax() : instance.messageSizeMax();
        _batchRequestQueue = new IceInternal.BatchRequestQueue(instance, _endpoint.datagram());
        _readStream = new IceInternal.BasicStream(instance, IceInternal.Protocol.currentProtocolEncoding);
        _readHeader = false;
        _readStreamPos = -1;
        _writeStream = new IceInternal.BasicStream(instance, IceInternal.Protocol.currentProtocolEncoding);
        _writeStreamPos = -1;
        _dispatchCount = 0;
        _state = StateNotInitialized;

        int compressionLevel = initData.properties.getPropertyAsIntWithDefault("Ice.Compression.Level", 1);
        if(compressionLevel < 1)
        {
            compressionLevel = 1;
        }
        else if(compressionLevel > 9)
        {
            compressionLevel = 9;
        }
        _compressionLevel = compressionLevel;

        if(adapter != null)
        {
            _servantManager = adapter.getServantManager();
        }
        else
        {
            _servantManager = null;
        }

        try
        {
            if(adapter != null)
            {
                _threadPool = adapter.getThreadPool();
            }
            else
            {
                _threadPool = _instance.clientThreadPool();
            }
            _threadPool.initialize(this);
        }
        catch(Ice.LocalException ex)
        {
            throw ex;
        }
        catch(java.lang.Exception ex)
        {
            throw new Ice.SyscallException(ex);
        }
    }

    @Override
    protected synchronized void finalize() throws Throwable
    {
        try
        {
            IceUtilInternal.Assert.FinalizerAssert(_startCallback == null);
            IceUtilInternal.Assert.FinalizerAssert(_state == StateFinished);
            IceUtilInternal.Assert.FinalizerAssert(_dispatchCount == 0);
            IceUtilInternal.Assert.FinalizerAssert(_sendStreams.isEmpty());
            IceUtilInternal.Assert.FinalizerAssert(_asyncRequests.isEmpty());
        }
        catch(java.lang.Exception ex)
        {
        }
        finally
        {
            super.finalize();
        }
    }

    private static final int StateNotInitialized = 0;
    private static final int StateNotValidated = 1;
    private static final int StateActive = 2;
    private static final int StateHolding = 3;
    private static final int StateClosing = 4;
    private static final int StateClosingPending = 5;
    private static final int StateClosed = 6;
    private static final int StateFinished = 7;

    private void setState(int state, LocalException ex)
    {
        //
        // If setState() is called with an exception, then only closed
        // and closing states are permissible.
        //
        assert state >= StateClosing;

        if(_state == state) // Don't switch twice.
        {
            return;
        }

        if(_exception == null)
        {
            //
            // If we are in closed state, an exception must be set.
            //
            assert (_state != StateClosed);

            _exception = ex;

            //
            // We don't warn if we are not validated.
            //
            if(_warn && _validated)
            {
                //
                // Don't warn about certain expected exceptions.
                //
                if(!(_exception instanceof CloseConnectionException ||
                     _exception instanceof ForcedCloseConnectionException ||
                     _exception instanceof ConnectionTimeoutException ||
                     _exception instanceof CommunicatorDestroyedException ||
                     _exception instanceof ObjectAdapterDeactivatedException ||
                     (_exception instanceof ConnectionLostException && _state >= StateClosing)))
                {
                    warning("connection exception", _exception);
                }
            }
        }

        //
        // We must set the new state before we notify requests of any
        // exceptions. Otherwise new requests may retry on a
        // connection that is not yet marked as closed or closing.
        //
        setState(state);
    }

    private void setState(int state)
    {
        //
        // We don't want to send close connection messages if the endpoint
        // only supports oneway transmission from client to server.
        //
        if(_endpoint.datagram() && state == StateClosing)
        {
            state = StateClosed;
        }

        //
        // Skip graceful shutdown if we are destroyed before validation.
        //
        if(_state <= StateNotValidated && state == StateClosing)
        {
            state = StateClosed;
        }

        if(_state == state) // Don't switch twice.
        {
            return;
        }

        try
        {
            switch(state)
            {
                case StateNotInitialized:
                {
                    assert (false);
                    break;
                }

                case StateNotValidated:
                {
                    if(_state != StateNotInitialized)
                    {
                        assert (_state == StateClosed);
                        return;
                    }
                    break;
                }

                case StateActive:
                {
                    //
                    // Can only switch from holding or not validated to
                    // active.
                    //
                    if(_state != StateHolding && _state != StateNotValidated)
                    {
                        return;
                    }
                    _threadPool.register(this, IceInternal.SocketOperation.Read);
                    break;
                }

                case StateHolding:
                {
                    //
                    // Can only switch from active or not validated to
                    // holding.
                    //
                    if(_state != StateActive && _state != StateNotValidated)
                    {
                        return;
                    }
                    if(_state == StateActive)
                    {
                        _threadPool.unregister(this, IceInternal.SocketOperation.Read);
                    }
                    break;
                }

                case StateClosing:
                case StateClosingPending:
                {
                    //
                    // Can't change back from closing pending.
                    //
                    if(_state >= StateClosingPending)
                    {
                        return;
                    }
                    break;
                }

                case StateClosed:
                {
                    if(_state == StateFinished)
                    {
                        return;
                    }

                    _batchRequestQueue.destroy(_exception);

                    //
                    // Don't need to close now for connections so only close the transceiver
                    // if the selector request it.
                    //
                    if(_threadPool.finish(this, false))
                    {
                        _transceiver.close();
                    }
                    break;
                }

                case StateFinished:
                {
                    assert (_state == StateClosed);
                    _communicator = null;
                    break;
                }
            }
        }
        catch(Ice.LocalException ex)
        {
            java.io.StringWriter sw = new java.io.StringWriter();
            java.io.PrintWriter pw = new java.io.PrintWriter(sw);
            ex.printStackTrace(pw);
            pw.flush();
            String s = "unexpected connection exception:\n " + _desc + "\n" + sw.toString();
            _instance.initializationData().logger.error(s);
        }

        //
        // We only register with the connection monitor if our new state
        // is StateActive. Otherwise we unregister with the connection
        // monitor, but only if we were registered before, i.e., if our
        // old state was StateActive.
        //
        if(_monitor != null)
        {
            if(state == StateActive)
            {
                if(_acmLastActivity > 0)
                {
                    _acmLastActivity = IceInternal.Time.currentMonotonicTimeMillis();
                }
                _monitor.add(this);
            }
            else if(_state == StateActive)
            {
                _monitor.remove(this);
            }
        }

        if(_instance.initializationData().observer != null)
        {
            Ice.Instrumentation.ConnectionState oldState = toConnectionState(_state);
            Ice.Instrumentation.ConnectionState newState = toConnectionState(state);
            if(oldState != newState)
            {
                _observer = _instance.initializationData().observer.getConnectionObserver(initConnectionInfo(),
                                                                                          _endpoint,
                                                                                          newState,
                                                                                          _observer);
                if(_observer != null)
                {
                    _observer.attach();
                }
                else
                {
                    _writeStreamPos = -1;
                    _readStreamPos = -1;
                }
            }
            if(_observer != null && state == StateClosed && _exception != null)
            {
                if(!(_exception instanceof CloseConnectionException ||
                     _exception instanceof ForcedCloseConnectionException ||
                     _exception instanceof ConnectionTimeoutException ||
                     _exception instanceof CommunicatorDestroyedException ||
                     _exception instanceof ObjectAdapterDeactivatedException ||
                     (_exception instanceof ConnectionLostException && _state >= StateClosing)))
                {
                    _observer.failed(_exception.ice_name());
                }
            }
        }
        _state = state;

        notifyAll();

        if(_state == StateClosing && _dispatchCount == 0)
        {
            try
            {
                initiateShutdown();
            }
            catch(LocalException ex)
            {
                setState(StateClosed, ex);
            }
        }
    }

    private void initiateShutdown()
    {
        assert (_state == StateClosing);
        assert (_dispatchCount == 0);

        if(_shutdownInitiated)
        {
            return;
        }
        _shutdownInitiated = true;

        if(!_endpoint.datagram())
        {
            //
            // Before we shut down, we send a close connection message.
            //
            IceInternal.BasicStream os = new IceInternal.BasicStream(_instance,
                    IceInternal.Protocol.currentProtocolEncoding);
            os.writeBlob(IceInternal.Protocol.magic);
            IceInternal.Protocol.currentProtocol.__write(os);
            IceInternal.Protocol.currentProtocolEncoding.__write(os);
            os.writeByte(IceInternal.Protocol.closeConnectionMsg);
            os.writeByte((byte) 0); // compression status: always report 0 for
                                    // CloseConnection in Java.
            os.writeInt(IceInternal.Protocol.headerSize); // Message size.

            if((sendMessage(new OutgoingMessage(os, false, false)) & IceInternal.AsyncStatus.Sent) > 0)
            {
                setState(StateClosingPending);

                //
                // Notify the the transceiver of the graceful connection
                // closure.
                //
                int op = _transceiver.closing(true, _exception);
                if(op != 0)
                {
                    scheduleTimeout(op);
                    _threadPool.register(this, op);
                }
            }
        }
    }

    private void heartbeat()
    {
        assert (_state == StateActive);

        if(!_endpoint.datagram())
        {
            IceInternal.BasicStream os = new IceInternal.BasicStream(_instance,
                    IceInternal.Protocol.currentProtocolEncoding);
            os.writeBlob(IceInternal.Protocol.magic);
            IceInternal.Protocol.currentProtocol.__write(os);
            IceInternal.Protocol.currentProtocolEncoding.__write(os);
            os.writeByte(IceInternal.Protocol.validateConnectionMsg);
            os.writeByte((byte) 0);
            os.writeInt(IceInternal.Protocol.headerSize); // Message size.

            try
            {
                OutgoingMessage message = new OutgoingMessage(os, false, false);
                sendMessage(message);
            }
            catch(Ice.LocalException ex)
            {
                setState(StateClosed, ex);
                assert (_exception != null);
            }
        }
    }

    private boolean initialize(int operation)
    {
        int s = _transceiver.initialize(_readStream.getBuffer(), _writeStream.getBuffer(), _hasMoreData);
        if(s != IceInternal.SocketOperation.None)
        {
            scheduleTimeout(s);
            _threadPool.update(this, operation, s);
            return false;
        }

        //
        // Update the connection description once the transceiver is
        // initialized.
        //
        _desc = _transceiver.toString();
        _initialized = true;
        setState(StateNotValidated);

        return true;
    }

    private boolean validate(int operation)
    {
        if(!_endpoint.datagram()) // Datagram connections are always implicitly
                                  // validated.
        {
            if(_adapter != null) // The server side has the active role for
                                 // connection validation.
            {
                if(_writeStream.isEmpty())
                {
                    _writeStream.writeBlob(IceInternal.Protocol.magic);
                    IceInternal.Protocol.currentProtocol.__write(_writeStream);
                    IceInternal.Protocol.currentProtocolEncoding.__write(_writeStream);
                    _writeStream.writeByte(IceInternal.Protocol.validateConnectionMsg);
                    _writeStream.writeByte((byte) 0); // Compression status
                                                      // (always zero for
                                                      // validate connection).
                    _writeStream.writeInt(IceInternal.Protocol.headerSize); // Message
                                                                            // size.
                    IceInternal.TraceUtil.traceSend(_writeStream, _logger, _traceLevels);
                    _writeStream.prepareWrite();
                }

                if(_observer != null)
                {
                    observerStartWrite(_writeStream.getBuffer());
                }

                if(_writeStream.pos() != _writeStream.size())
                {
                    int op = write(_writeStream.getBuffer());
                    if(op != 0)
                    {
                        scheduleTimeout(op);
                        _threadPool.update(this, operation, op);
                        return false;
                    }
                }

                if(_observer != null)
                {
                    observerFinishWrite(_writeStream.getBuffer());
                }
            }
            else
            // The client side has the passive role for connection validation.
            {
                if(_readStream.isEmpty())
                {
                    _readStream.resize(IceInternal.Protocol.headerSize, true);
                    _readStream.pos(0);
                }

                if(_observer != null)
                {
                    observerStartRead(_readStream.getBuffer());
                }

                if(_readStream.pos() != _readStream.size())
                {
                    int op = read(_readStream.getBuffer());
                    if(op != 0)
                    {
                        scheduleTimeout(op);
                        _threadPool.update(this, operation, op);
                        return false;
                    }
                }

                if(_observer != null)
                {
                    observerFinishRead(_readStream.getBuffer());
                }

                assert (_readStream.pos() == IceInternal.Protocol.headerSize);
                _readStream.pos(0);
                byte[] m = _readStream.readBlob(4);
                if(m[0] != IceInternal.Protocol.magic[0] || m[1] != IceInternal.Protocol.magic[1] ||
                   m[2] != IceInternal.Protocol.magic[2] || m[3] != IceInternal.Protocol.magic[3])
                {
                    BadMagicException ex = new BadMagicException();
                    ex.badMagic = m;
                    throw ex;
                }

                _readProtocol.__read(_readStream);
                IceInternal.Protocol.checkSupportedProtocol(_readProtocol);

                _readProtocolEncoding.__read(_readStream);
                IceInternal.Protocol.checkSupportedProtocolEncoding(_readProtocolEncoding);

                byte messageType = _readStream.readByte();
                if(messageType != IceInternal.Protocol.validateConnectionMsg)
                {
                    throw new ConnectionNotValidatedException();
                }
                _readStream.readByte(); // Ignore compression status for
                                        // validate connection.
                int size = _readStream.readInt();
                if(size != IceInternal.Protocol.headerSize)
                {
                    throw new IllegalMessageSizeException();
                }
                IceInternal.TraceUtil.traceRecv(_readStream, _logger, _traceLevels);

                _validated = true;
            }
        }

        _writeStream.resize(0, false);
        _writeStream.pos(0);

        _readStream.resize(IceInternal.Protocol.headerSize, true);
        _readStream.pos(0);
        _readHeader = true;

        if(_instance.traceLevels().network >= 1)
        {
            StringBuffer s = new StringBuffer();
            if(_endpoint.datagram())
            {
                s.append("starting to ");
                s.append(_connector != null ? "send" : "receive");
                s.append(" ");
                s.append(_endpoint.protocol());
                s.append(" messages\n");
                s.append(_transceiver.toDetailedString());
            }
            else
            {
                s.append(_connector != null ? "established" : "accepted");
                s.append(" ");
                s.append(_endpoint.protocol());
                s.append(" connection\n");
                s.append(toString());
            }
            _instance.initializationData().logger.trace(_instance.traceLevels().networkCat, s.toString());
        }

        return true;
    }

    private int sendNextMessage(java.util.List<OutgoingMessage> callbacks)
    {
        if(_sendStreams.isEmpty())
        {
            return IceInternal.SocketOperation.None;
        }
        else if(_state == StateClosingPending && _writeStream.pos() == 0)
        {
            // Message wasn't sent, empty the _writeStream, we're not going to
            // send more data.
            OutgoingMessage message = _sendStreams.getFirst();
            _writeStream.swap(message.stream);
            return IceInternal.SocketOperation.None;
        }

        assert (!_writeStream.isEmpty() && _writeStream.pos() == _writeStream.size());
        try
        {
            while(true)
            {
                //
                // Notify the message that it was sent.
                //
                OutgoingMessage message = _sendStreams.getFirst();
                _writeStream.swap(message.stream);
                if(message.sent())
                {
                    callbacks.add(message);
                }
                _sendStreams.removeFirst();

                //
                // If there's nothing left to send, we're done.
                //
                if(_sendStreams.isEmpty())
                {
                    break;
                }

                //
                // If we are in the closed state or if the close is
                // pending, don't continue sending.
                //
                // This can occur if parseMessage (called before
                // sendNextMessage by message()) closes the connection.
                //
                if(_state >= StateClosingPending)
                {
                    return IceInternal.SocketOperation.None;
                }

                //
                // Otherwise, prepare the next message stream for writing.
                //
                message = _sendStreams.getFirst();
                assert (!message.prepared);
                IceInternal.BasicStream stream = message.stream;

                message.stream = doCompress(stream, message.compress);
                message.stream.prepareWrite();
                message.prepared = true;

                if(message.outAsync != null)
                {
                    IceInternal.TraceUtil.trace("sending asynchronous request", stream, _logger, _traceLevels);
                }
                else
                {
                    IceInternal.TraceUtil.traceSend(stream, _logger, _traceLevels);
                }
                _writeStream.swap(message.stream);

                //
                // Send the message.
                //
                if(_observer != null)
                {
                    observerStartWrite(_writeStream.getBuffer());
                }
                if(_writeStream.pos() != _writeStream.size())
                {
                    int op = write(_writeStream.getBuffer());
                    if(op != 0)
                    {
                        return op;
                    }
                }
                if(_observer != null)
                {
                    observerFinishWrite(_writeStream.getBuffer());
                }
            }

            //
            // If all the messages were sent and we are in the closing state, we
            // schedule the close timeout to wait for the peer to close the
            // connection.
            //
            if(_state == StateClosing && _shutdownInitiated)
            {
                setState(StateClosingPending);
                int op = _transceiver.closing(true, _exception);
                if(op != 0)
                {
                    return op;
                }
            }
        }
        catch(Ice.LocalException ex)
        {
            setState(StateClosed, ex);
        }
        return IceInternal.SocketOperation.None;
    }

    private int sendMessage(OutgoingMessage message)
    {
        assert (_state < StateClosed);

        if(!_sendStreams.isEmpty())
        {
            message.adopt();
            _sendStreams.addLast(message);
            return IceInternal.AsyncStatus.Queued;
        }

        //
        // Attempt to send the message without blocking. If the send blocks, we
        // register the connection with the selector thread.
        //

        assert (!message.prepared);

        IceInternal.BasicStream stream = message.stream;

        message.stream = doCompress(stream, message.compress);
        message.stream.prepareWrite();
        message.prepared = true;
        int op;

        if(message.outAsync != null)
        {
            IceInternal.TraceUtil.trace("sending asynchronous request", stream, _logger, _traceLevels);
        }
        else
        {
            IceInternal.TraceUtil.traceSend(stream, _logger, _traceLevels);
        }

        //
        // Send the message without blocking.
        //
        if(_observer != null)
        {
            observerStartWrite(message.stream.getBuffer());
        }
        op = write(message.stream.getBuffer());
        if(op == 0)
        {
            if(_observer != null)
            {
                observerFinishWrite(message.stream.getBuffer());
            }

            int status = IceInternal.AsyncStatus.Sent;
            if(message.sent())
            {
                status |= IceInternal.AsyncStatus.InvokeSentCallback;
            }

            if(_acmLastActivity > 0)
            {
                _acmLastActivity = IceInternal.Time.currentMonotonicTimeMillis();
            }
            return status;
        }

        message.adopt();

        _writeStream.swap(message.stream);
        _sendStreams.addLast(message);
        scheduleTimeout(op);
        _threadPool.register(this, op);
        return IceInternal.AsyncStatus.Queued;
    }

    private IceInternal.BasicStream doCompress(IceInternal.BasicStream uncompressed, boolean compress)
    {
        boolean compressionSupported = false;
        if(compress)
        {
            //
            // Don't check whether compression support is available unless the
            // proxy is configured for compression.
            //
            compressionSupported = IceInternal.BasicStream.compressible();
        }

        if(compressionSupported && uncompressed.size() >= 100)
        {
            //
            // Do compression.
            //
            IceInternal.BasicStream cstream = uncompressed.compress(IceInternal.Protocol.headerSize, _compressionLevel);
            if(cstream != null)
            {
                //
                // Set compression status.
                //
                cstream.pos(9);
                cstream.writeByte((byte) 2);

                //
                // Write the size of the compressed stream into the header.
                //
                cstream.pos(10);
                cstream.writeInt(cstream.size());

                //
                // Write the compression status and size of the compressed
                // stream into the header of the uncompressed stream -- we need
                // this to trace requests correctly.
                //
                uncompressed.pos(9);
                uncompressed.writeByte((byte) 2);
                uncompressed.writeInt(cstream.size());

                return cstream;
            }
        }

        uncompressed.pos(9);
        uncompressed.writeByte((byte) (compressionSupported ? 1 : 0));

        //
        // Not compressed, fill in the message size.
        //
        uncompressed.pos(10);
        uncompressed.writeInt(uncompressed.size());

        return uncompressed;
    }

    private static class MessageInfo
    {
        MessageInfo(IceInternal.BasicStream stream)
        {
            this.stream = stream;
        }

        IceInternal.BasicStream stream;
        int invokeNum;
        int requestId;
        byte compress;
        IceInternal.ServantManager servantManager;
        ObjectAdapter adapter;
        IceInternal.OutgoingAsyncBase outAsync;
        ConnectionCallback heartbeatCallback;
        int messageDispatchCount;
    }

    private int parseMessage(MessageInfo info)
    {
        assert (_state > StateNotValidated && _state < StateClosed);

        _readStream.swap(info.stream);
        _readStream.resize(IceInternal.Protocol.headerSize, true);
        _readStream.pos(0);
        _readHeader = true;

        assert (info.stream.pos() == info.stream.size());

        //
        // Connection is validated on first message. This is only used by
        // setState() to check wether or not we can print a connection
        // warning (a client might close the connection forcefully if the
        // connection isn't validated).
        //
        _validated = true;

        try
        {
            //
            // We don't need to check magic and version here. This has already
            // been done by the ThreadPool which provides us with the stream.
            //
            info.stream.pos(8);
            byte messageType = info.stream.readByte();
            info.compress = info.stream.readByte();
            if(info.compress == (byte) 2)
            {
                if(IceInternal.BasicStream.compressible())
                {
                    info.stream = info.stream.uncompress(IceInternal.Protocol.headerSize, _messageSizeMax);
                }
                else
                {
                    FeatureNotSupportedException ex = new FeatureNotSupportedException();
                    ex.unsupportedFeature = "Cannot uncompress compressed message: "
                                            + "org.apache.tools.bzip2.CBZip2OutputStream was not found";
                    throw ex;
                }
            }
            info.stream.pos(IceInternal.Protocol.headerSize);

            switch(messageType)
            {
                case IceInternal.Protocol.closeConnectionMsg:
                {
                    IceInternal.TraceUtil.traceRecv(info.stream, _logger, _traceLevels);
                    if(_endpoint.datagram())
                    {
                        if(_warn)
                        {
                            _logger.warning("ignoring close connection message for datagram connection:\n" + _desc);
                        }
                    }
                    else
                    {
                        setState(StateClosingPending, new CloseConnectionException());

                        //
                        // Notify the the transceiver of the graceful connection
                        // closure.
                        //
                        int op = _transceiver.closing(false, _exception);
                        if(op != 0)
                        {
                            return op;
                        }
                        setState(StateClosed);
                    }
                    break;
                }

                case IceInternal.Protocol.requestMsg:
                {
                    if(_state >= StateClosing)
                    {
                        IceInternal.TraceUtil.trace("received request during closing\n"
                                                    + "(ignored by server, client will retry)", info.stream, _logger,
                                _traceLevels);
                    }
                    else
                    {
                        IceInternal.TraceUtil.traceRecv(info.stream, _logger, _traceLevels);
                        info.requestId = info.stream.readInt();
                        info.invokeNum = 1;
                        info.servantManager = _servantManager;
                        info.adapter = _adapter;
                        ++info.messageDispatchCount;
                    }
                    break;
                }

                case IceInternal.Protocol.requestBatchMsg:
                {
                    if(_state >= StateClosing)
                    {
                        IceInternal.TraceUtil.trace("received batch request during closing\n"
                                                    + "(ignored by server, client will retry)", info.stream, _logger,
                                _traceLevels);
                    }
                    else
                    {
                        IceInternal.TraceUtil.traceRecv(info.stream, _logger, _traceLevels);
                        info.invokeNum = info.stream.readInt();
                        if(info.invokeNum < 0)
                        {
                            info.invokeNum = 0;
                            throw new UnmarshalOutOfBoundsException();
                        }
                        info.servantManager = _servantManager;
                        info.adapter = _adapter;
                        info.messageDispatchCount += info.invokeNum;
                    }
                    break;
                }

                case IceInternal.Protocol.replyMsg:
                {
                    IceInternal.TraceUtil.traceRecv(info.stream, _logger, _traceLevels);
                    info.requestId = info.stream.readInt();

                    IceInternal.OutgoingAsyncBase outAsync = _asyncRequests.remove(info.requestId);
                    if(outAsync != null && outAsync.completed(info.stream))
                    {
                        info.outAsync = outAsync;
                        ++info.messageDispatchCount;
                    }
                    notifyAll(); // Notify threads blocked in close(false)
                    break;
                }

                case IceInternal.Protocol.validateConnectionMsg:
                {
                    IceInternal.TraceUtil.traceRecv(info.stream, _logger, _traceLevels);
                    if(_callback != null)
                    {
                        info.heartbeatCallback = _callback;
                        ++info.messageDispatchCount;
                    }
                    break;
                }

                default:
                {
                    IceInternal.TraceUtil.trace("received unknown message\n(invalid, closing connection)", info.stream,
                            _logger, _traceLevels);
                    throw new UnknownMessageException();
                }
            }
        }
        catch(LocalException ex)
        {
            if(_endpoint.datagram())
            {
                if(_warn)
                {
                    _logger.warning("datagram connection exception:\n" + ex + '\n' + _desc);
                }
            }
            else
            {
                setState(StateClosed, ex);
            }
        }

        return _state == StateHolding ? IceInternal.SocketOperation.None : IceInternal.SocketOperation.Read;
    }

    private void invokeAll(IceInternal.BasicStream stream, int invokeNum, int requestId, byte compress,
            IceInternal.ServantManager servantManager, ObjectAdapter adapter)
    {
        //
        // Note: In contrast to other private or protected methods, this
        // operation must be called *without* the mutex locked.
        //

        IceInternal.Incoming in = null;
        try
        {
            while(invokeNum > 0)
            {

                //
                // Prepare the invocation.
                //
                boolean response = !_endpoint.datagram() && requestId != 0;
                in = getIncoming(adapter, response, compress, requestId);

                //
                // Dispatch the invocation.
                //
                in.invoke(servantManager, stream);

                --invokeNum;

                reclaimIncoming(in);
                in = null;
            }

            stream.clear();
        }
        catch(LocalException ex)
        {
            invokeException(requestId, ex, invokeNum, false);
        }
        catch(IceInternal.ServantError ex)
        {
            //
            // ServantError is thrown when an Error has been raised by servant (or servant locator)
            // code. We've already attempted to complete the invocation and send a response.
            //
            Throwable t = ex.getCause();
            //
            // Suppress AssertionError and OutOfMemoryError, rethrow everything else.
            //
            if(!(t instanceof java.lang.AssertionError ||
                 t instanceof java.lang.OutOfMemoryError ||
                 t instanceof java.lang.StackOverflowError))
            {
                throw (java.lang.Error)t;
            }
        }
        catch(java.lang.Error ex)
        {
            //
            // An Error was raised outside of servant code (i.e., by Ice code).
            // Attempt to log the error and clean up. This may still fail
            // depending on the severity of the error.
            //
            // Note that this does NOT send a response to the client.
            //
            UnknownException uex = new UnknownException(ex);
            java.io.StringWriter sw = new java.io.StringWriter();
            java.io.PrintWriter pw = new java.io.PrintWriter(sw);
            ex.printStackTrace(pw);
            pw.flush();
            uex.unknown = sw.toString();
            _logger.error(uex.unknown);
            invokeException(requestId, uex, invokeNum, false);
            //
            // Suppress AssertionError and OutOfMemoryError, rethrow everything else.
            //
            if(!(ex instanceof java.lang.AssertionError ||
                 ex instanceof java.lang.OutOfMemoryError ||
                 ex instanceof java.lang.StackOverflowError))
            {
                throw ex;
            }
        }
        finally
        {
            if(in != null)
            {
                reclaimIncoming(in);
            }
        }
    }

    private void scheduleTimeout(int status)
    {
        int timeout;
        if(_state < StateActive)
        {
            IceInternal.DefaultsAndOverrides defaultsAndOverrides = _instance.defaultsAndOverrides();
            if(defaultsAndOverrides.overrideConnectTimeout)
            {
                timeout = defaultsAndOverrides.overrideConnectTimeoutValue;
            }
            else
            {
                timeout = _endpoint.timeout();
            }
        }
        else if(_state < StateClosingPending)
        {
            if(_readHeader) // No timeout for reading the header.
            {
                status &= ~IceInternal.SocketOperation.Read;
            }
            timeout = _endpoint.timeout();
        }
        else
        {
            IceInternal.DefaultsAndOverrides defaultsAndOverrides = _instance.defaultsAndOverrides();
            if(defaultsAndOverrides.overrideCloseTimeout)
            {
                timeout = defaultsAndOverrides.overrideCloseTimeoutValue;
            }
            else
            {
                timeout = _endpoint.timeout();
            }
        }

        if(timeout < 0)
        {
            return;
        }

        try
        {
            if((status & IceInternal.SocketOperation.Read) != 0)
            {
                if(_readTimeoutFuture != null)
                {
                    _readTimeoutFuture.cancel(false);
                }
                _readTimeoutFuture = _timer.schedule(_readTimeout, timeout, java.util.concurrent.TimeUnit.MILLISECONDS);
            }
            if((status & (IceInternal.SocketOperation.Write | IceInternal.SocketOperation.Connect)) != 0)
            {
                if(_writeTimeoutFuture != null)
                {
                    _writeTimeoutFuture.cancel(false);
                }
                _writeTimeoutFuture = _timer.schedule(_writeTimeout, timeout,
                        java.util.concurrent.TimeUnit.MILLISECONDS);
            }
        }
        catch(Throwable ex)
        {
            assert (false);
        }
    }

    private void unscheduleTimeout(int status)
    {
        if((status & IceInternal.SocketOperation.Read) != 0 && _readTimeoutFuture != null)
        {
            _readTimeoutFuture.cancel(false);
            _readTimeoutFuture = null;
        }
        if((status & (IceInternal.SocketOperation.Write | IceInternal.SocketOperation.Connect)) != 0 &&
           _writeTimeoutFuture != null)
        {
            _writeTimeoutFuture.cancel(false);
            _writeTimeoutFuture = null;
        }
    }

    private ConnectionInfo initConnectionInfo()
    {
        if(_state > StateNotInitialized && _info != null) // Update the connection information until it's initialized
        {
            return _info;
        }

        try
        {
            _info = _transceiver.getInfo();
        }
        catch(Ice.LocalException ex)
        {
            _info = new ConnectionInfo();
        }
        _info.connectionId = _endpoint.connectionId();
        _info.adapterName = _adapter != null ? _adapter.getName() : "";
        _info.incoming = _connector == null;
        return _info;
    }

    private Ice.Instrumentation.ConnectionState toConnectionState(int state)
    {
        return connectionStateMap[state];
    }

    private void warning(String msg, java.lang.Exception ex)
    {
        java.io.StringWriter sw = new java.io.StringWriter();
        java.io.PrintWriter pw = new java.io.PrintWriter(sw);
        ex.printStackTrace(pw);
        pw.flush();
        String s = msg + ":\n" + _desc + "\n" + sw.toString();
        _logger.warning(s);
    }

    private void observerStartRead(IceInternal.Buffer buf)
    {
        if(_readStreamPos >= 0)
        {
            assert (!buf.empty());
            _observer.receivedBytes(buf.b.position() - _readStreamPos);
        }
        _readStreamPos = buf.empty() ? -1 : buf.b.position();
    }

    private void observerFinishRead(IceInternal.Buffer buf)
    {
        if(_readStreamPos == -1)
        {
            return;
        }
        assert (buf.b.position() >= _readStreamPos);
        _observer.receivedBytes(buf.b.position() - _readStreamPos);
        _readStreamPos = -1;
    }

    private void observerStartWrite(IceInternal.Buffer buf)
    {
        if(_writeStreamPos >= 0)
        {
            assert (!buf.empty());
            _observer.sentBytes(buf.b.position() - _writeStreamPos);
        }
        _writeStreamPos = buf.empty() ? -1 : buf.b.position();
    }

    private void observerFinishWrite(IceInternal.Buffer buf)
    {
        if(_writeStreamPos == -1)
        {
            return;
        }
        if(buf.b.position() > _writeStreamPos)
        {
            _observer.sentBytes(buf.b.position() - _writeStreamPos);
        }
        _writeStreamPos = -1;
    }

    private IceInternal.Incoming getIncoming(ObjectAdapter adapter, boolean response, byte compress, int requestId)
    {
        IceInternal.Incoming in = null;

        if(_cacheBuffers > 0)
        {
            synchronized(_incomingCacheMutex)
            {
                if(_incomingCache == null)
                {
                    in = new IceInternal.Incoming(_instance, this, this, adapter, response, compress, requestId);
                }
                else
                {
                    in = _incomingCache;
                    _incomingCache = _incomingCache.next;
                    in.reset(_instance, this, this, adapter, response, compress, requestId);
                    in.next = null;
                }
            }
        }
        else
        {
            in = new IceInternal.Incoming(_instance, this, this, adapter, response, compress, requestId);
        }

        return in;
    }

    private void reclaimIncoming(IceInternal.Incoming in)
    {
        if(_cacheBuffers > 0)
        {
            synchronized(_incomingCacheMutex)
            {
                in.next = _incomingCache;
                _incomingCache = in;
                //
                // Clear references to Ice objects as soon as possible.
                //
                _incomingCache.reclaim();
            }
        }
    }

    private void reap()
    {
        if(_monitor != null)
        {
            _monitor.reap(this);
        }
        if(_observer != null)
        {
            _observer.detach();
        }
    }

    private int read(IceInternal.Buffer buf)
    {
        int start = buf.b.position();
        int op = _transceiver.read(buf, _hasMoreData);
        if(_instance.traceLevels().network >= 3 && buf.b.position() != start)
        {
            StringBuffer s = new StringBuffer("received ");
            if(_endpoint.datagram())
            {
                s.append(buf.b.limit());
            }
            else
            {
                s.append(buf.b.position() - start);
                s.append(" of ");
                s.append(buf.b.limit() - start);
            }
            s.append(" bytes via ");
            s.append(_endpoint.protocol());
            s.append("\n");
            s.append(toString());

            _instance.initializationData().logger.trace(_instance.traceLevels().networkCat, s.toString());
        }
        return op;
    }

    private int write(IceInternal.Buffer buf)
    {
        int start = buf.b.position();
        int op = _transceiver.write(buf);
        if(_instance.traceLevels().network >= 3 && buf.b.position() != start)
        {
            StringBuffer s = new StringBuffer("sent ");
            s.append(buf.b.position() - start);
            if(!_endpoint.datagram())
            {
                s.append(" of ");
                s.append(buf.b.limit() - start);
            }
            s.append(" bytes via ");
            s.append(_endpoint.protocol());
            s.append("\n");
            s.append(toString());
            _instance.initializationData().logger.trace(_instance.traceLevels().networkCat, s.toString());
        }
        return op;
    }

    private static class OutgoingMessage
    {
        OutgoingMessage(IceInternal.BasicStream stream, boolean compress, boolean adopt)
        {
            this.stream = stream;
            this.compress = compress;
            this.adopt = adopt;
            this.requestId = 0;
        }

        OutgoingMessage(IceInternal.OutgoingAsyncBase out, IceInternal.BasicStream stream, boolean compress,
                int requestId)
        {
            this.stream = stream;
            this.compress = compress;
            this.outAsync = out;
            this.requestId = requestId;
        }

        public void canceled()
        {
            assert (outAsync != null);
            outAsync = null;
        }

        public void adopt()
        {
            if(adopt)
            {
                IceInternal.BasicStream stream = new IceInternal.BasicStream(this.stream.instance(),
                        IceInternal.Protocol.currentProtocolEncoding);
                stream.swap(this.stream);
                this.stream = stream;
                adopt = false;
            }
        }

        public boolean sent()
        {
            if(outAsync != null)
            {
                return outAsync.sent();
            }
            return false;
        }

        public void completed(Ice.LocalException ex)
        {
            if(outAsync != null && outAsync.completed(ex))
            {
                outAsync.invokeCompleted();
            }
        }

        public IceInternal.BasicStream stream;
        public IceInternal.OutgoingAsyncBase outAsync;
        public boolean compress;
        public int requestId;
        boolean adopt;
        boolean prepared;
    }

    private Communicator _communicator;
    private final IceInternal.Instance _instance;
    private IceInternal.ACMMonitor _monitor;
    private final IceInternal.Transceiver _transceiver;
    private String _desc;
    private final String _type;
    private final IceInternal.Connector _connector;
    private final IceInternal.EndpointI _endpoint;

    private ObjectAdapter _adapter;
    private IceInternal.ServantManager _servantManager;

    private final boolean _dispatcher;
    private final Logger _logger;
    private final IceInternal.TraceLevels _traceLevels;
    private final IceInternal.ThreadPool _threadPool;

    private final java.util.concurrent.ScheduledExecutorService _timer;
    private final Runnable _writeTimeout;
    private java.util.concurrent.Future<?> _writeTimeoutFuture;
    private final Runnable _readTimeout;
    private java.util.concurrent.Future<?> _readTimeoutFuture;

    private StartCallback _startCallback = null;

    private final boolean _warn;
    private final boolean _warnUdp;

    private long _acmLastActivity;

    private final int _compressionLevel;

    private int _nextRequestId;

    private java.util.Map<Integer, IceInternal.OutgoingAsyncBase> _asyncRequests =
        new java.util.HashMap<Integer, IceInternal.OutgoingAsyncBase>();

    private LocalException _exception;

    private final int _messageSizeMax;
    private IceInternal.BatchRequestQueue _batchRequestQueue;

    private java.util.LinkedList<OutgoingMessage> _sendStreams = new java.util.LinkedList<OutgoingMessage>();

    private IceInternal.BasicStream _readStream;
    private boolean _readHeader;
    private IceInternal.BasicStream _writeStream;

    private Ice.Instrumentation.ConnectionObserver _observer;
    private int _readStreamPos;
    private int _writeStreamPos;

    private int _dispatchCount;

    private int _state; // The current state.
    private boolean _shutdownInitiated = false;
    private boolean _initialized = false;
    private boolean _validated = false;

    private IceInternal.Incoming _incomingCache;
    private final java.lang.Object _incomingCacheMutex = new java.lang.Object();

    private Ice.ProtocolVersion _readProtocol = new Ice.ProtocolVersion();
    private Ice.EncodingVersion _readProtocolEncoding = new Ice.EncodingVersion();

    private int _cacheBuffers;

    private Ice.ConnectionInfo _info;

    private ConnectionCallback _callback;

    private static Ice.Instrumentation.ConnectionState connectionStateMap[] = {
            Ice.Instrumentation.ConnectionState.ConnectionStateValidating, // StateNotInitialized
            Ice.Instrumentation.ConnectionState.ConnectionStateValidating, // StateNotValidated
            Ice.Instrumentation.ConnectionState.ConnectionStateActive, // StateActive
            Ice.Instrumentation.ConnectionState.ConnectionStateHolding, // StateHolding
            Ice.Instrumentation.ConnectionState.ConnectionStateClosing, // StateClosing
            Ice.Instrumentation.ConnectionState.ConnectionStateClosing, // StateClosingPending
            Ice.Instrumentation.ConnectionState.ConnectionStateClosed, // StateClosed
            Ice.Instrumentation.ConnectionState.ConnectionStateClosed, // StateFinished
    };

}
