// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

using System.Diagnostics;

namespace IceInternal
{
    sealed class BatchRequestI : Ice.BatchRequest
    {
        public BatchRequestI(BatchRequestQueue queue)
        {
            _queue = queue;
        }

        public void reset(Ice.ObjectPrx proxy, string operation, int size)
        {
            _proxy = proxy;
            _operation = operation;
            _size = size;
        }

        public void enqueue()
        {
            _queue.enqueueBatchRequest();
        }

        public Ice.ObjectPrx getProxy()
        {
            return _proxy;
        }

        public string getOperation()
        {
            return _operation;
        }

        public int getSize()
        {
            return _size;
        }

        private BatchRequestQueue _queue;
        private Ice.ObjectPrx _proxy;
        private string _operation;
        private int _size;
    };


    public sealed class BatchRequestQueue
    {
        public BatchRequestQueue(Instance instance, bool datagram)
        {
            Ice.InitializationData initData = instance.initializationData();
            _interceptor = initData.batchRequestInterceptor;
            _batchStreamInUse = false;
            _batchRequestNum = 0;
            _batchStream = new BasicStream(instance, Ice.Util.currentProtocolEncoding);
            _batchStream.writeBlob(Protocol.requestBatchHdr);
            _batchMarker = _batchStream.size();
            _request = new BatchRequestI(this);

            _maxSize = instance.batchAutoFlushSize();
            if(_maxSize > 0 && datagram)
            {
                int udpSndSize = initData.properties.getPropertyAsIntWithDefault("Ice.UDP.SndSize",
                                                                                 65535 - _udpOverhead);
                if(udpSndSize < _maxSize)
                {
                    _maxSize = udpSndSize;
                }
            }
        }

        public void
        prepareBatchRequest(BasicStream os)
        {
            lock(this)
            {
                if(_exception != null)
                {
                    throw _exception;
                }
                waitStreamInUse(false);
                _batchStreamInUse = true;
                _batchStream.swap(os);
            }
        }

        public void
        finishBatchRequest(BasicStream os, Ice.ObjectPrx proxy, string operation)
        {
            //
            // No need for synchronization, no other threads are supposed
            // to modify the queue since we set _batchStreamInUse to true.
            //
            Debug.Assert(_batchStreamInUse);
            _batchStream.swap(os);

            try
            {
                _batchStreamCanFlush = true; // Allow flush to proceed even if the stream is marked in use.

                if(_maxSize > 0 && _batchStream.size() >= _maxSize)
                {
                    proxy.begin_ice_flushBatchRequests(); // Auto flush
                }

                Debug.Assert(_batchMarker < _batchStream.size());
                if(_interceptor != null)
                {
                    _request.reset(proxy, operation, _batchStream.size() - _batchMarker);
                    _interceptor.enqueue(_request, _batchRequestNum, _batchMarker);
                }
                else
                {
                    _batchMarker = _batchStream.size();
                    ++_batchRequestNum;
                }
            }
            finally
            {
                lock(this)
                {
                    _batchStream.resize(_batchMarker, false);
                    _batchStreamInUse = false;
                    _batchStreamCanFlush = false;
                    System.Threading.Monitor.PulseAll(this);
                }
            }
        }

        public void
        abortBatchRequest(BasicStream os)
        {
            lock(this)
            {
                if(_batchStreamInUse)
                {
                    _batchStream.swap(os);
                    _batchStream.resize(_batchMarker, false);
                    _batchStreamInUse = false;
                    System.Threading.Monitor.PulseAll(this);
                }
            }
        }

        public int
        swap(BasicStream os)
        {
            lock(this)
            {
                if(_batchRequestNum == 0)
                {
                    return 0;
                }

                waitStreamInUse(true);

                byte[] lastRequest = null;
                if(_batchMarker < _batchStream.size())
                {
                    lastRequest = new byte[_batchStream.size() - _batchMarker];
                    Buffer buffer = _batchStream.getBuffer();
                    buffer.b.position(_batchMarker);
                    buffer.b.get(lastRequest);
                    _batchStream.resize(_batchMarker, false);
                }

                int requestNum = _batchRequestNum;
                _batchStream.swap(os);

                //
                // Reset the batch.
                //
                _batchRequestNum = 0;
                _batchStream.writeBlob(Protocol.requestBatchHdr);
                _batchMarker = _batchStream.size();
                if(lastRequest != null)
                {
                    _batchStream.writeBlob(lastRequest);
                }
                return requestNum;
            }
        }

        public void
        destroy(Ice.LocalException ex)
        {
            lock(this)
            {
                _exception = ex;
            }
        }

        public bool
        isEmpty()
        {
            lock(this)
            {
                return _batchStream.size() == Protocol.requestBatchHdr.Length;
            }
        }

        private void
        waitStreamInUse(bool flush)
        {
            //
            // This is similar to a mutex lock in that the stream is
            // only "locked" while marshaling. As such we don't permit the wait
            // to be interrupted. Instead the interrupted status is saved and
            // restored.
            //
            while(_batchStreamInUse && !(flush && _batchStreamCanFlush))
            {
                System.Threading.Monitor.Wait(this);
            }
        }

        internal void enqueueBatchRequest()
        {
            Debug.Assert(_batchMarker < _batchStream.size());
            _batchMarker = _batchStream.size();
            ++_batchRequestNum;
        }

        private Ice.BatchRequestInterceptor _interceptor;
        private BasicStream _batchStream;
        private bool _batchStreamInUse;
        private bool _batchStreamCanFlush;
        private int _batchRequestNum;
        private int _batchMarker;
        private BatchRequestI _request;
        private Ice.LocalException _exception;
        private int _maxSize;

        private static int _udpOverhead = 20 + 8;
    }
};
