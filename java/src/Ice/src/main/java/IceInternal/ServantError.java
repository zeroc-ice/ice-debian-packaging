// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package IceInternal;

final public class ServantError extends java.lang.Error
{
    public ServantError(String reason)
    {
        super(reason);
    }

    public ServantError(String reason, Throwable cause)
    {
        super(reason, cause);
    }

    public ServantError(Throwable cause)
    {
        super(cause);
    }
}
