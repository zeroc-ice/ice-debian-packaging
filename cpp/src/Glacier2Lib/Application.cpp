// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <Glacier2/Application.h>
#include <IceUtil/IceUtil.h>
#include <IceUtil/ArgVector.h>

using namespace std;
using namespace Ice;
    
Ice::ObjectAdapterPtr Glacier2::Application::_adapter;
Glacier2::RouterPrx Glacier2::Application::_router;
Glacier2::SessionPrx Glacier2::Application::_session;
bool Glacier2::Application::_createdSession = false;
string Glacier2::Application::_category;

namespace
{

class ConnectionCallbackI : public Ice::ConnectionCallback
{
public:

    ConnectionCallbackI(Glacier2::Application* app) : _app(app)
    {
    }

    virtual void
    heartbeat(const Ice::ConnectionPtr&)
    {
    }

    virtual void 
    closed(const Ice::ConnectionPtr&)
    {
        _app->sessionDestroyed();
    }
    
private:

    Glacier2::Application* _app;
};
    
}

string
Glacier2::RestartSessionException::ice_name() const
{
    return "RestartSessionException";
}

Glacier2::RestartSessionException*
Glacier2::RestartSessionException::ice_clone() const
{
    return new RestartSessionException(*this);
}

void
Glacier2::RestartSessionException::ice_throw() const
{
    throw *this;
}

Ice::ObjectAdapterPtr
Glacier2::Application::objectAdapter()
{
    if(!_router)
    {
        SessionNotExistException ex;
        throw ex;
    }

    IceUtil::Mutex::Lock lock(*IceInternal::Application::mutex);
    if(!_adapter)
    {
        _adapter = communicator()->createObjectAdapterWithRouter("", _router);
        _adapter->activate();
    }
    return _adapter;
}

Ice::ObjectPrx
Glacier2::Application::addWithUUID(const Ice::ObjectPtr& servant)
{
    return objectAdapter()->add(servant, createCallbackIdentity(IceUtil::generateUUID()));
}

Ice::Identity
Glacier2::Application::createCallbackIdentity(const string& name)
{
    Ice::Identity id;
    id.name = name;
    id.category = categoryForClient();
    return id;
}

std::string
Glacier2::Application::categoryForClient()
{
    if(!_router)
    {
        SessionNotExistException ex;
        throw ex;
    }
    return _category;
}

int
Glacier2::Application::doMain(int argc, char* argv[], const Ice::InitializationData& initData)
{
    // Set the default properties for all Glacier2 applications.
    initData.properties->setProperty("Ice.RetryIntervals", "-1");

    bool restart;
    int ret = 0;
    do
    {
        //
        // A copy of the initialization data and the string seq
        // needs to be passed to doMainInternal, as these can be
        // changed by the application.
        //
        Ice::InitializationData id(initData);
        id.properties = id.properties->clone();
        Ice::StringSeq args = Ice::argsToStringSeq(argc, argv);

        restart = doMain(args, id, ret);
    }
    while(restart);
    return ret;
}

bool
Glacier2::Application::doMain(Ice::StringSeq& args, const Ice::InitializationData& initData, int& status)
{
    //
    // Reset internal state variables from Ice.Application. The
    // remainder are reset at the end of this method.
    //
    IceInternal::Application::_callbackInProgress = false;
    IceInternal::Application::_destroyed = false;
    IceInternal::Application::_interrupted = false;

    bool restart = false;
    status = 0;

    try
    {
        IceInternal::Application::_communicator = Ice::initialize(args, initData);
        _router = Glacier2::RouterPrx::uncheckedCast(communicator()->getDefaultRouter());
        
        if(!_router)
        {
            Error out(getProcessLogger());
            out << IceInternal::Application::_appName << ": no glacier2 router configured";
            status = 1;
        }
        else
        {
            //
            // The default is to destroy when a signal is received.
            //
            if(IceInternal::Application::_signalPolicy == Ice::HandleSignals)
            {
                destroyOnInterrupt();
            }

            // If createSession throws, we're done.
            try
            {
                _session = createSession();
                _createdSession = true;
            }
            catch(const Ice::LocalException& ex)
            {
                Error out(getProcessLogger());
                out << IceInternal::Application::_appName << ": " << ex;
                status = 1;
            }

            if(_createdSession)
            {
                Ice::Int acmTimeout = 0;
                try
                { 
                    acmTimeout = _router->getACMTimeout();
                }
                catch(const Ice::OperationNotExistException&)
                {
                }
                if(acmTimeout <= 0)
                {
                    acmTimeout = static_cast<Ice::Int>(_router->getSessionTimeout());
                }

                if(acmTimeout > 0)
                {
                    Ice::ConnectionPtr connection = _router->ice_getCachedConnection();
                    assert(connection);
                    connection->setACM(acmTimeout, IceUtil::None, Ice::HeartbeatAlways);
                    connection->setCallback(new ConnectionCallbackI(this));
                }

                _category = _router->getCategoryForClient();
                IceUtilInternal::ArgVector a(args);
                status = runWithSession(a.argc, a.argv);
            }
        }
    }
    // We want to restart on those exceptions which indicate a
    // break down in communications, but not those exceptions that
    // indicate a programming logic error (ie: marshal, protocol
    // failure, etc).
    catch(const RestartSessionException&)
    {
        restart = true;
    }
    catch(const Ice::ConnectionRefusedException& ex)
    {
        Error out(getProcessLogger());
        out << IceInternal::Application::_appName << ": " << ex;
        restart = true;
    }
    catch(const Ice::ConnectionLostException& ex)
    {
        Error out(getProcessLogger());
        out << IceInternal::Application::_appName << ": " << ex;
        restart = true;
    }
    catch(const Ice::UnknownLocalException& ex)
    {
        Error out(getProcessLogger());
        out << IceInternal::Application::_appName << ": " << ex;
        restart = true;
    }
    catch(const Ice::RequestFailedException& ex)
    {
        Error out(getProcessLogger());
        out << IceInternal::Application::_appName << ": " << ex;
        restart = true;
    }
    catch(const Ice::TimeoutException& ex)
    {
        Error out(getProcessLogger());
        out << IceInternal::Application::_appName << ": " << ex;
        restart = true;
    }
    catch(const Ice::LocalException& ex)
    {
        Error out(getProcessLogger());
        out << IceInternal::Application::_appName << ": " << ex;
        status = 1;
    }
    catch(const std::exception& ex)
    {
        Error out(getProcessLogger());
        out << IceInternal::Application::_appName << ": std::exception " << ex;
        status = 1;
    }
    catch(const std::string& ex)
    {
        Error out(getProcessLogger());
        out << IceInternal::Application::_appName << ": c++ exception " << ex;
        status = 1;
    }
    catch(const char* ex)
    {
        Error out(getProcessLogger());
        out << IceInternal::Application::_appName << ": char* exception " << ex;
        status = 1;
    }
    catch(...)
    {
        Error out(getProcessLogger());
        out << IceInternal::Application::_appName << ": unknown exception";
        status = 1;
    }

    //
    // Don't want any new interrupt and at this point (post-run),
    // it would not make sense to release a held signal to run
    // shutdown or destroy.
    //
    if(IceInternal::Application::_signalPolicy == HandleSignals)
    {
        ignoreInterrupt();
    }

    {
        IceUtil::Mutex::Lock lock(*IceInternal::Application::mutex);
        while(IceInternal::Application::_callbackInProgress)
        {
            IceInternal::Application::_condVar->wait(lock);
        }
        if(IceInternal::Application::_destroyed)
        {
            IceInternal::Application::_communicator = 0;
        }
        else
        {
            IceInternal::Application::_destroyed = true;
            //
            // And _communicator != 0, meaning will be destroyed
            // next, _destroyed = true also ensures that any
            // remaining callback won't do anything
            //
        }
        IceInternal::Application::_application = 0;
    }

    if(_createdSession && _router)
    {
        try
        {
            _router->destroySession();
        }
        catch(const Ice::ConnectionLostException&)
        {
            // Expected if another thread invoked on an object from the session concurrently.
        }
        catch(const Glacier2::SessionNotExistException&)
        {
            // This can also occur.
        }
        catch(const exception& ex)
        {
            // Not expected.
            Error out(getProcessLogger());
            out << "unexpected exception when destroying the session:\n" << ex;
        }
        _router = 0;
    }

    if(IceInternal::Application::_communicator)
    {
        try
        {
            IceInternal::Application::_communicator->destroy();
        }
        catch(const Ice::LocalException& ex)
        {
            Error out(getProcessLogger());
            out << IceInternal::Application::_appName << ": " << ex;
            status = 1;
        }
        catch(const exception& ex)
        {
            Error out(getProcessLogger());
            out << "unknown exception:\n" << ex;
            status = 1;
        }
        IceInternal::Application::_communicator = 0;
    }

    //
    // Reset internal state. We cannot reset the Application state
    // here, since _destroyed must remain true until we re-run
    // this method.
    //
    _adapter = 0;
    _router = 0;
    _session = 0;
    _createdSession = false;
    _category.clear();

    return restart;
}
