// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package Ice;

/**
 * Base interface for listening to batch request queues.
 **/
public interface BatchRequestInterceptor
{
    /**
     * Called by the Ice runtime when a batch request is about to be
     * added to the batch request queue of a proxy or connection.
     *
     * The implementation of this method must call enqueue() on the
     * request to confirm its addition to the queue, if not called
     * the request isn't added to the queue. The implementation can
     * raise an Ice local exception to notify the caller of a failure.
     *
     **/
    void enqueue(Ice.BatchRequest request, int queueBatchRequestCount, int queueBatchRequestSize);
}
