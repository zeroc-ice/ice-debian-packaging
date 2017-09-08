// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package IceInternal;

class BufSizeWarnInfo
{
    // Whether send size warning has been emitted
    public boolean sndWarn;

    // The send size for which the warning wwas emitted
    public int sndSize;

    // Whether receive size warning has been emitted
    public boolean rcvWarn;

    // The receive size for which the warning wwas emitted
    public int rcvSize;
}
