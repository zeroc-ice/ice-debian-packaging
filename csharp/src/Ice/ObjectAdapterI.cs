// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

namespace Ice
{
    using System;
    using System.Collections;
    using System.Collections.Generic;
    using System.Diagnostics;
    using System.Text;

    using IceInternal;

    public sealed class ObjectAdapterI : ObjectAdapter
    {
        public string getName()
        {
            //
            // No mutex lock necessary, _name is immutable.
            //
            return _noConfig ? "" : _name;
        }

        public Communicator getCommunicator()
        {
            return _communicator;
        }

        public void activate()
        {
            LocatorInfo locatorInfo = null;
            bool printAdapterReady = false;

            lock(this)
            {
                checkForDeactivation();

                //
                // If we've previously been initialized we just need to activate the
                // incoming connection factories and we're done.
                //
                if(state_ != StateUninitialized)
                {
                    foreach(IncomingConnectionFactory icf in _incomingConnectionFactories)
                    {
                        icf.activate();
                    }
                    return;
                }

                //
                // One off initializations of the adapter: update the
                // locator registry and print the "adapter ready"
                // message. We set set state to StateActivating to prevent
                // deactivation from other threads while these one off
                // initializations are done.
                //
                state_ = StateActivating;

                locatorInfo = _locatorInfo;
                if(!_noConfig)
                {
                    Properties properties = instance_.initializationData().properties;
                    printAdapterReady = properties.getPropertyAsInt("Ice.PrintAdapterReady") > 0;
                }
            }

            try
            {
                Ice.Identity dummy = new Ice.Identity();
                dummy.name = "dummy";
                updateLocatorRegistry(locatorInfo, createDirectProxy(dummy));
            }
            catch(Ice.LocalException)
            {
                //
                // If we couldn't update the locator registry, we let the
                // exception go through and don't activate the adapter to
                // allow to user code to retry activating the adapter
                // later.
                //
                lock(this)
                {
                    state_ = StateUninitialized;
                    System.Threading.Monitor.PulseAll(this);
                }
                throw;
            }

            if(printAdapterReady)
            {
                System.Console.Out.WriteLine(_name + " ready");
            }

            lock(this)
            {
                Debug.Assert(state_ == StateActivating);

                foreach(IncomingConnectionFactory icf in _incomingConnectionFactories)
                {
                    icf.activate();
                }

                state_ = StateActive;
                System.Threading.Monitor.PulseAll(this);
            }
        }

        public void hold()
        {
            lock(this)
            {
                checkForDeactivation();
                state_ = StateHeld;
                foreach(IncomingConnectionFactory factory in _incomingConnectionFactories)
                {
                    factory.hold();
                }
            }
        }

        public void waitForHold()
        {
            List<IncomingConnectionFactory> incomingConnectionFactories;
            lock(this)
            {
                checkForDeactivation();

                incomingConnectionFactories = new List<IncomingConnectionFactory>(_incomingConnectionFactories);
            }

            foreach(IncomingConnectionFactory factory in incomingConnectionFactories)
            {
                factory.waitUntilHolding();
            }
        }

        public void deactivate()
        {
            lock(this)
            {
                //
                //
                // Wait for activation to complete. This is necessary to not
                // get out of order locator updates.
                //
                while(state_ == StateActivating || state_ == StateDeactivating)
                {
                    System.Threading.Monitor.Wait(this);
                }
                if(state_ > StateDeactivating)
                {
                    return;
                }
                state_ = StateDeactivating;
            }

            //
            // NOTE: the router/locator infos and incoming connection
            // facatory list are immutable at this point.
            //

            if(_routerInfo != null)
            {
                //
                // Remove entry from the router manager.
                //
                instance_.routerManager().erase(_routerInfo.getRouter());

                //
                // Clear this object adapter with the router.
                //
                _routerInfo.setAdapter(null);
            }

            try
            {
                updateLocatorRegistry(_locatorInfo, null);
            }
            catch(Ice.LocalException)
            {
                //
                // We can't throw exceptions in deactivate so we ignore
                // failures to update the locator registry.
                //
            }

            //
            // Must be called outside the thread synchronization, because
            // Connection::destroy() might block when sending a CloseConnection
            // message.
            //
            foreach(IncomingConnectionFactory factory in _incomingConnectionFactories)
            {
                factory.destroy();
            }

            //
            // Must be called outside the thread synchronization, because
            // changing the object adapter might block if there are still
            // requests being dispatched.
            //
            instance_.outgoingConnectionFactory().removeAdapter(this);

            lock(this)
            {
                Debug.Assert(state_ == StateDeactivating);
                state_ = StateDeactivated;
                System.Threading.Monitor.PulseAll(this);
            }
        }

        public void waitForDeactivate()
        {
            IncomingConnectionFactory[] incomingConnectionFactories = null;
            lock(this)
            {
                //
                // Wait for deactivation of the adapter itself, and
                // for the return of all direct method calls using this
                // adapter.
                //
                while((state_ < StateDeactivated) || _directCount > 0)
                {
                    System.Threading.Monitor.Wait(this);
                }
                if(state_ > StateDeactivated)
                {
                    return;
                }

                incomingConnectionFactories = _incomingConnectionFactories.ToArray();
            }

            //
            // Now we wait for until all incoming connection factories are
            // finished.
            //
            foreach(IncomingConnectionFactory factory in incomingConnectionFactories)
            {
                factory.waitUntilFinished();
            }
        }

        public bool isDeactivated()
        {
            lock(this)
            {
                return state_ >= StateDeactivated;
            }
        }

        public void destroy()
        {
            //
            // Deactivate and wait for completion.
            //
            deactivate();
            waitForDeactivate();

            lock(this)
            {
                //
                // Only a single thread is allowed to destroy the object
                // adapter. Other threads wait for the destruction to be
                // completed.
                //
                while(state_ == StateDestroying)
                {
                    System.Threading.Monitor.Wait(this);
                }
                if(state_ == StateDestroyed)
                {
                    return;
                }
                state_ = StateDestroying;
            }

            //
            // Now it's also time to clean up our servants and servant
            // locators.
            //
            _servantManager.destroy();

            //
            // Destroy the thread pool.
            //
            if(_threadPool != null)
            {
                _threadPool.destroy();
                _threadPool.joinWithAllThreads();
            }

            if(_objectAdapterFactory != null)
            {
                _objectAdapterFactory.removeObjectAdapter(this);
            }

            lock(this)
            {
                //
                // We're done, now we can throw away all incoming connection
                // factories.
                //
                _incomingConnectionFactories.Clear();

                //
                // Remove object references (some of them cyclic).
                //
                instance_ = null;
                _threadPool = null;
                _routerEndpoints = null;
                _routerInfo = null;
                _publishedEndpoints = null;
                _locatorInfo = null;
                _reference = null;
                _objectAdapterFactory = null;

                state_ = StateDestroyed;
                System.Threading.Monitor.PulseAll(this);
            }
        }

        public ObjectPrx add(Ice.Object obj, Identity ident)
        {
            return addFacet(obj, ident, "");
        }

        public ObjectPrx addFacet(Ice.Object obj, Identity ident, string facet)
        {
            lock(this)
            {
                checkForDeactivation();
                checkIdentity(ident);
                checkServant(obj);

                //
                // Create a copy of the Identity argument, in case the caller
                // reuses it.
                //
                Identity id = new Identity();
                id.category = ident.category;
                id.name = ident.name;

                _servantManager.addServant(obj, id, facet);

                return newProxy(id, facet);
            }
        }

        public ObjectPrx addWithUUID(Ice.Object obj)
        {
            return addFacetWithUUID(obj, "");
        }

        public ObjectPrx addFacetWithUUID(Ice.Object obj, string facet)
        {
            Identity ident = new Identity();
            ident.category = "";
            ident.name = Guid.NewGuid().ToString();

            return addFacet(obj, ident, facet);
        }

        public void addDefaultServant(Ice.Object servant, string category)
        {
            checkServant(servant);

            lock(this)
            {
                checkForDeactivation();

                _servantManager.addDefaultServant(servant, category);
            }
        }

        public Ice.Object remove(Identity ident)
        {
            return removeFacet(ident, "");
        }

        public Ice.Object removeFacet(Identity ident, string facet)
        {
            lock(this)
            {
                checkForDeactivation();
                checkIdentity(ident);

                return _servantManager.removeServant(ident, facet);
            }
        }

        public Dictionary<string, Ice.Object> removeAllFacets(Identity ident)
        {
            lock(this)
            {
                checkForDeactivation();
                checkIdentity(ident);

                return _servantManager.removeAllFacets(ident);
            }
        }

        public Ice.Object removeDefaultServant(string category)
        {
            lock(this)
            {
                checkForDeactivation();

                return _servantManager.removeDefaultServant(category);
            }
        }

        public Ice.Object find(Identity ident)
        {
            return findFacet(ident, "");
        }

        public Ice.Object findFacet(Identity ident, string facet)
        {
            lock(this)
            {
                checkForDeactivation();
                checkIdentity(ident);

                return _servantManager.findServant(ident, facet);
            }
        }

        public Dictionary<string, Ice.Object> findAllFacets(Identity ident)
        {
            lock(this)
            {
                checkForDeactivation();
                checkIdentity(ident);

                return _servantManager.findAllFacets(ident);
            }
        }

        public Ice.Object findByProxy(ObjectPrx proxy)
        {
            lock(this)
            {
                checkForDeactivation();

                Reference @ref = ((ObjectPrxHelperBase)proxy).reference__();
                return findFacet(@ref.getIdentity(), @ref.getFacet());
            }
        }

        public Ice.Object findDefaultServant(string category)
        {
            lock(this)
            {
                checkForDeactivation();

                return _servantManager.findDefaultServant(category);
            }
        }

        public void addServantLocator(ServantLocator locator, string prefix)
        {
            lock(this)
            {
                checkForDeactivation();

                _servantManager.addServantLocator(locator, prefix);
            }
        }

        public ServantLocator removeServantLocator(string prefix)
        {
            lock(this)
            {
                checkForDeactivation();

                return _servantManager.removeServantLocator(prefix);
            }
        }

        public ServantLocator findServantLocator(string prefix)
        {
            lock(this)
            {
                checkForDeactivation();

                return _servantManager.findServantLocator(prefix);
            }
        }

        public ObjectPrx createProxy(Identity ident)
        {
            lock(this)
            {
                checkForDeactivation();
                checkIdentity(ident);

                return newProxy(ident, "");
            }
        }

        public ObjectPrx createDirectProxy(Identity ident)
        {
            lock(this)
            {
                checkForDeactivation();
                checkIdentity(ident);

                return newDirectProxy(ident, "");
            }
        }

        public ObjectPrx createIndirectProxy(Identity ident)
        {
            lock(this)
            {
                checkForDeactivation();
                checkIdentity(ident);

                return newIndirectProxy(ident, "", _id);
            }
        }

        public void setLocator(LocatorPrx locator)
        {
            lock(this)
            {
                checkForDeactivation();

                _locatorInfo = instance_.locatorManager().get(locator);
            }
        }

        public LocatorPrx getLocator()
        {
            lock(this)
            {
                checkForDeactivation();

                if(_locatorInfo == null)
                {
                    return null;
                }
                else
                {
                    return _locatorInfo.getLocator();
                }
            }
        }

        public void refreshPublishedEndpoints()
        {
            LocatorInfo locatorInfo = null;
            List<EndpointI> oldPublishedEndpoints;

            lock(this)
            {
                checkForDeactivation();

                oldPublishedEndpoints = _publishedEndpoints;
                _publishedEndpoints = parsePublishedEndpoints();

                locatorInfo = _locatorInfo;
            }

            try
            {
                Ice.Identity dummy = new Ice.Identity();
                dummy.name = "dummy";
                updateLocatorRegistry(locatorInfo, createDirectProxy(dummy));
            }
            catch(Ice.LocalException)
            {
                lock(this)
                {
                    //
                    // Restore the old published endpoints.
                    //
                    _publishedEndpoints = oldPublishedEndpoints;
                    throw;
                }
            }
        }

        public Endpoint[] getEndpoints()
        {
            lock(this)
            {
                List<Endpoint> endpoints = new List<Endpoint>();
                foreach(IncomingConnectionFactory factory in _incomingConnectionFactories)
                {
                    endpoints.Add(factory.endpoint());
                }
                return endpoints.ToArray();
            }
        }

        public Endpoint[] getPublishedEndpoints()
        {
            lock(this)
            {
                return _publishedEndpoints.ToArray();
            }
        }

        public bool isLocal(ObjectPrx proxy)
        {
            //
            // NOTE: it's important that isLocal() doesn't perform any blocking operations as
            // it can be called for AMI invocations if the proxy has no delegate set yet.
            //

            Reference r = ((ObjectPrxHelperBase)proxy).reference__();
            if(r.isWellKnown())
            {
                //
                // Check the active servant map to see if the well-known
                // proxy is for a local object.
                //
                return _servantManager.hasServant(r.getIdentity());
            }
            else if(r.isIndirect())
            {
                //
                // Proxy is local if the reference adapter id matches this
                // adapter id or replica group id.
                //
                return r.getAdapterId().Equals(_id) || r.getAdapterId().Equals(_replicaGroupId);
            }
            else
            {
                EndpointI[] endpoints = r.getEndpoints();

                lock(this)
                {
                    checkForDeactivation();

                    //
                    // Proxies which have at least one endpoint in common with the
                    // endpoints used by this object adapter's incoming connection
                    // factories are considered local.
                    //
                    for(int i = 0; i < endpoints.Length; ++i)
                    {
                        foreach(EndpointI endpoint in _publishedEndpoints)
                        {
                            if(endpoints[i].equivalent(endpoint))
                            {
                                return true;
                            }
                        }
                        foreach(IncomingConnectionFactory factory in _incomingConnectionFactories)
                        {
                            if(endpoints[i].equivalent(factory.endpoint()))
                            {
                                return true;
                            }
                        }
                    }

                    //
                    // Proxies which have at least one endpoint in common with the
                    // router's server proxy endpoints (if any), are also considered
                    // local.
                    //
                    if(_routerInfo != null && _routerInfo.getRouter().Equals(proxy.ice_getRouter()))
                    {
                        for(int i = 0; i < endpoints.Length; ++i)
                        {
                            foreach(EndpointI endpoint in _routerEndpoints)
                            {
                                if(endpoints[i].equivalent(endpoint))
                                {
                                    return true;
                                }
                            }
                        }
                    }

                    return false;
                }
            }
        }

        public void flushAsyncBatchRequests(CommunicatorFlushBatch outAsync)
        {
            List<IncomingConnectionFactory> f;
            lock(this)
            {
                f = new List<IncomingConnectionFactory>(_incomingConnectionFactories);
            }

            foreach(IncomingConnectionFactory factory in f)
            {
                factory.flushAsyncBatchRequests(outAsync);
            }
        }

        public void updateConnectionObservers()
        {
            List<IncomingConnectionFactory> f;
            lock(this)
            {
                f = new List<IncomingConnectionFactory>(_incomingConnectionFactories);
            }

            foreach(IncomingConnectionFactory p in f)
            {
                p.updateConnectionObservers();
            }
        }

        public void  updateThreadObservers()
        {
            ThreadPool threadPool = null;
            lock(this)
            {
                threadPool = _threadPool;
            }

            if(threadPool != null)
            {
                threadPool.updateObservers();
            }
        }

        public void incDirectCount()
        {
            lock(this)
            {
                checkForDeactivation();

                Debug.Assert(_directCount >= 0);
                ++_directCount;
            }
        }

        public void decDirectCount()
        {
            lock(this)
            {
                // Not check for deactivation here!

                Debug.Assert(instance_ != null); // Must not be called after destroy().

                Debug.Assert(_directCount > 0);
                if(--_directCount == 0)
                {
                    System.Threading.Monitor.PulseAll(this);
                }
            }
        }

        public ThreadPool getThreadPool()
        {
            // No mutex lock necessary, _threadPool and instance_ are
            // immutable after creation until they are removed in
            // destroy().

            // Not check for deactivation here!

            Debug.Assert(instance_ != null); // Must not be called after destroy().

            if(_threadPool != null)
            {
                return _threadPool;
            }
            else
            {
                return instance_.serverThreadPool();
            }

        }

        public ServantManager getServantManager()
        {
            //
            // No mutex lock necessary, _servantManager is immutable.
            //
            return _servantManager;
        }

        public ACMConfig getACM()
        {
            // Not check for deactivation here!

            Debug.Assert(instance_ != null); // Must not be called after destroy().
            return _acm;
        }

        public int messageSizeMax()
        {
            // No mutex lock, immutable.
            return _messageSizeMax;
        }

        //
        // Only for use by ObjectAdapterFactory
        //
        public ObjectAdapterI(Instance instance, Communicator communicator,
                              ObjectAdapterFactory objectAdapterFactory, string name,
                              RouterPrx router, bool noConfig)
        {
            instance_ = instance;
            _communicator = communicator;
            _objectAdapterFactory = objectAdapterFactory;
            _servantManager = new ServantManager(instance, name);
            _name = name;
            _incomingConnectionFactories = new List<IncomingConnectionFactory>();
            _publishedEndpoints = new List<EndpointI>();
            _routerEndpoints = new List<EndpointI>();
            _routerInfo = null;
            _directCount = 0;
            _noConfig = noConfig;

            if(_noConfig)
            {
                _id = "";
                _replicaGroupId = "";
                _reference = instance_.referenceFactory().create("dummy -t", "");
                _acm = instance_.serverACM();
                return;
            }

            Properties properties = instance_.initializationData().properties;
            List<string> unknownProps = new List<string>();
            bool noProps = filterProperties(unknownProps);

            //
            // Warn about unknown object adapter properties.
            //
            if(unknownProps.Count != 0 && properties.getPropertyAsIntWithDefault("Ice.Warn.UnknownProperties", 1) > 0)
            {
                StringBuilder message = new StringBuilder("found unknown properties for object adapter `");
                message.Append(_name);
                message.Append("':");
                foreach(string s in unknownProps)
                {
                    message.Append("\n    ");
                    message.Append(s);
                }
                instance_.initializationData().logger.warning(message.ToString());
            }

            //
            // Make sure named adapter has configuration.
            //
            if(router == null && noProps)
            {
                //
                // These need to be set to prevent warnings/asserts in the destructor.
                //
                state_ = StateDestroyed;
                instance_ = null;
                _incomingConnectionFactories = null;

                InitializationException ex = new InitializationException();
                ex.reason = "object adapter `" + _name + "' requires configuration";
                throw ex;
            }

            _id = properties.getProperty(_name + ".AdapterId");
            _replicaGroupId = properties.getProperty(_name + ".ReplicaGroupId");

            //
            // Setup a reference to be used to get the default proxy options
            // when creating new proxies. By default, create twoway proxies.
            //
            string proxyOptions = properties.getPropertyWithDefault(_name + ".ProxyOptions", "-t");
            try
            {
                _reference = instance_.referenceFactory().create("dummy " + proxyOptions, "");
            }
            catch(ProxyParseException)
            {
                InitializationException ex = new InitializationException();
                ex.reason = "invalid proxy options `" + proxyOptions + "' for object adapter `" + _name + "'";
                throw ex;
            }

            _acm = new ACMConfig(properties, communicator.getLogger(), _name + ".ACM", instance_.serverACM());

            {
                int defaultMessageSizeMax = instance.messageSizeMax() / 1024;
                int num = properties.getPropertyAsIntWithDefault(_name + ".MessageSizeMax", defaultMessageSizeMax);
                if(num < 1 || num > 0x7fffffff / 1024)
                {
                    _messageSizeMax = 0x7fffffff;
                }
                else
                {
                    _messageSizeMax = num * 1024; // Property is in kilobytes, _messageSizeMax in bytes
                }
            }

            try
            {
                int threadPoolSize = properties.getPropertyAsInt(_name + ".ThreadPool.Size");
                int threadPoolSizeMax = properties.getPropertyAsInt(_name + ".ThreadPool.SizeMax");
                if(threadPoolSize > 0 || threadPoolSizeMax > 0)
                {
                    _threadPool = new ThreadPool(instance_, _name + ".ThreadPool", 0);
                }

                if(router == null)
                {
                    router = RouterPrxHelper.uncheckedCast(
                        instance_.proxyFactory().propertyToProxy(_name + ".Router"));
                }
                if(router != null)
                {
                    _routerInfo = instance_.routerManager().get(router);
                    if(_routerInfo != null)
                    {
                        //
                        // Make sure this router is not already registered with another adapter.
                        //
                        if(_routerInfo.getAdapter() != null)
                        {
                            Ice.AlreadyRegisteredException ex = new Ice.AlreadyRegisteredException();
                            ex.kindOfObject = "object adapter with router";
                            ex.id = instance_.identityToString(router.ice_getIdentity());
                            throw ex;
                        }

                        //
                        // Add the router's server proxy endpoints to this object
                        // adapter.
                        //
                        EndpointI[] endpoints = _routerInfo.getServerEndpoints();
                        for(int i = 0; i < endpoints.Length; ++i)
                        {
                            _routerEndpoints.Add(endpoints[i]);
                        }
                        _routerEndpoints.Sort(); // Must be sorted.

                        //
                        // Remove duplicate endpoints, so we have a list of unique endpoints.
                        //
                        for(int i = 0; i < _routerEndpoints.Count-1;)
                        {
                            EndpointI e1 = _routerEndpoints[i];
                            EndpointI e2 = _routerEndpoints[i + 1];
                            if(e1.Equals(e2))
                            {
                                _routerEndpoints.RemoveAt(i);
                            }
                            else
                            {
                                ++i;
                            }
                        }

                        //
                        // Associate this object adapter with the router. This way,
                        // new outgoing connections to the router's client proxy will
                        // use this object adapter for callbacks.
                        //
                        _routerInfo.setAdapter(this);

                        //
                        // Also modify all existing outgoing connections to the
                        // router's client proxy to use this object adapter for
                        // callbacks.
                        //
                        instance_.outgoingConnectionFactory().setRouterInfo(_routerInfo);
                    }
                }
                else
                {
                    //
                    // Parse the endpoints, but don't store them in the adapter. The connection
                    // factory might change it, for example, to fill in the real port number.
                    //
                    List<EndpointI> endpoints =  parseEndpoints(properties.getProperty(_name + ".Endpoints"), true);
                    foreach(EndpointI endp in endpoints)
                    {
                        IncomingConnectionFactory factory = new IncomingConnectionFactory(instance, endp, this);
                        _incomingConnectionFactories.Add(factory);
                    }
                    if(endpoints.Count == 0)
                    {
                        TraceLevels tl = instance_.traceLevels();
                        if(tl.network >= 2)
                        {
                            instance_.initializationData().logger.trace(tl.networkCat, "created adapter `" + _name +
                                                                        "' without endpoints");
                        }
                    }

                    //
                    // Parse published endpoints.
                    //
                    _publishedEndpoints = parsePublishedEndpoints();
                }

                if(properties.getProperty(_name + ".Locator").Length > 0)
                {
                    setLocator(LocatorPrxHelper.uncheckedCast(
                        instance_.proxyFactory().propertyToProxy(_name + ".Locator")));
                }
                else
                {
                    setLocator(instance_.referenceFactory().getDefaultLocator());
                }
            }
            catch(LocalException)
            {
                destroy();
                throw;
            }
        }

        /*
        ~ObjectAdapterI()
        {
            if(!_deactivated)
            {
                string msg = "object adapter `" + getName() + "' has not been deactivated";
                if(!Environment.HasShutdownStarted)
                {
                    instance_.initializationData().logger.warning(msg);
                }
                else
                {
                    Console.Error.WriteLine(msg);
                }
            }
            else if(!_destroyed)
            {
                string msg = "object adapter `" + getName() + "' has not been destroyed";
                if(!Environment.HasShutdownStarted)
                {
                    instance_.initializationData().logger.warning(msg);
                }
                else
                {
                    Console.Error.WriteLine(msg);
                }
            }
        }
        */

        private ObjectPrx newProxy(Identity ident, string facet)
        {
            if(_id.Length == 0)
            {
                return newDirectProxy(ident, facet);
            }
            else if(_replicaGroupId.Length == 0)
            {
                return newIndirectProxy(ident, facet, _id);
            }
            else
            {
                return newIndirectProxy(ident, facet, _replicaGroupId);
            }
        }

        private ObjectPrx newDirectProxy(Identity ident, string facet)
        {
            EndpointI[] endpoints;

            //
            // Use the published endpoints, otherwise use the endpoints from all
            // incoming connection factories.
            //
            int sz = _publishedEndpoints.Count;
            endpoints = new EndpointI[sz + _routerEndpoints.Count];
            for(int i = 0; i < sz; ++i)
            {
                endpoints[i] = _publishedEndpoints[i];
            }

            //
            // Now we also add the endpoints of the router's server proxy, if
            // any. This way, object references created by this object adapter
            // will also point to the router's server proxy endpoints.
            //
            for(int i = 0; i < _routerEndpoints.Count; ++i)
            {
                endpoints[sz + i] = _routerEndpoints[i];
            }

            //
            // Create a reference and return a proxy for this reference.
            //
            Reference reference = instance_.referenceFactory().create(ident, facet, _reference, endpoints);
            return instance_.proxyFactory().referenceToProxy(reference);
        }

        private ObjectPrx newIndirectProxy(Identity ident, string facet, string id)
        {
            //
            // Create a reference with the adapter id and return a
            // proxy for the reference.
            //
            Reference reference = instance_.referenceFactory().create(ident, facet, _reference, id);
            return instance_.proxyFactory().referenceToProxy(reference);
        }

        private void checkForDeactivation()
        {
            if(state_ >= StateDeactivating)
            {
                ObjectAdapterDeactivatedException ex = new ObjectAdapterDeactivatedException();
                ex.name = getName();
                throw ex;
            }
        }

        private static void checkIdentity(Identity ident)
        {
            if(ident.name == null || ident.name.Length == 0)
            {
                throw new IllegalIdentityException(ident);
            }
            if(ident.category == null)
            {
                ident.category = "";
            }
        }

        private static void checkServant(Ice.Object servant)
        {
            if(servant == null)
            {
                throw new IllegalServantException("cannot add null servant to Object Adapter");
            }
        }

        private List<EndpointI> parseEndpoints(string endpts, bool oaEndpoints)
        {
            int beg;
            int end = 0;

            string delim = " \t\n\r";

            List<EndpointI> endpoints = new List<EndpointI>();
            while(end < endpts.Length)
            {
                beg = IceUtilInternal.StringUtil.findFirstNotOf(endpts, delim, end);
                if(beg == -1)
                {
                    break;
                }

                end = beg;
                while(true)
                {
                    end = endpts.IndexOf((System.Char) ':', end);
                    if(end == -1)
                    {
                        end = endpts.Length;
                        break;
                    }
                    else
                    {
                        bool quoted = false;
                        int quote = beg;
                        while(true)
                        {
                            quote = endpts.IndexOf((System.Char) '\"', quote);
                            if(quote == -1 || end < quote)
                            {
                                break;
                            }
                            else
                            {
                                quote = endpts.IndexOf((System.Char) '\"', ++quote);
                                if(quote == -1)
                                {
                                    break;
                                }
                                else if(end < quote)
                                {
                                    quoted = true;
                                    break;
                                }
                                ++quote;
                            }
                        }
                        if(!quoted)
                        {
                            break;
                        }
                        ++end;
                    }
                }

                if(end == beg)
                {
                    ++end;
                    continue;
                }

                string s = endpts.Substring(beg, (end) - (beg));
                EndpointI endp = instance_.endpointFactoryManager().create(s, oaEndpoints);
                if(endp == null)
                {
#if COMPACT
                    if(s.StartsWith("ssl", StringComparison.Ordinal) || s.StartsWith("wss", StringComparison.Ordinal))
                    {
                        instance_.initializationData().logger.warning(
                            "ignoring endpoint `" + s +
                            "': IceSSL is not supported with the .NET Compact Framework");
                        ++end;
                        continue;
                    }
#else
                    if(AssemblyUtil.runtime_ == AssemblyUtil.Runtime.Mono &&
                       (s.StartsWith("ssl", StringComparison.Ordinal) || s.StartsWith("wss", StringComparison.Ordinal)))
                    {
                        instance_.initializationData().logger.warning(
                            "ignoring endpoint `" + s + "': IceSSL is not supported with Mono");
                        ++end;
                        continue;
                    }
#endif
                    Ice.EndpointParseException e2 = new Ice.EndpointParseException();
                    e2.str = "invalid object adapter endpoint `" + s + "'";
                    throw e2;
                }
                endpoints.Add(endp);

                ++end;
            }

            return endpoints;
        }

        private List<EndpointI> parsePublishedEndpoints()
        {
            //
            // Parse published endpoints. If set, these are used in proxies
            // instead of the connection factory endpoints.
            //
            string endpts = instance_.initializationData().properties.getProperty(_name + ".PublishedEndpoints");
            List<EndpointI> endpoints = parseEndpoints(endpts, false);
            if(endpoints.Count == 0)
            {
                //
                // If the PublishedEndpoints property isn't set, we compute the published enpdoints
                // from the OA endpoints, expanding any endpoints that may be listening on INADDR_ANY
                // to include actual addresses in the published endpoints.
                //
                foreach(IncomingConnectionFactory factory in _incomingConnectionFactories)
                {
                    endpoints.AddRange(factory.endpoint().expand());
                }
            }

            if(instance_.traceLevels().network >= 1 && endpoints.Count > 0)
            {
                 StringBuilder s = new StringBuilder("published endpoints for object adapter `");
                 s.Append(_name);
                 s.Append("':\n");
                 bool first = true;
                 foreach(EndpointI endpoint in endpoints)
                 {
                     if(!first)
                     {
                         s.Append(":");
                     }
                     s.Append(endpoint.ToString());
                     first = false;
                 }
                 instance_.initializationData().logger.trace(instance_.traceLevels().networkCat, s.ToString());
             }
             return endpoints;
        }

        private void updateLocatorRegistry(LocatorInfo locatorInfo, ObjectPrx proxy)
        {
            if(_id.Length == 0 || locatorInfo == null)
            {
                return; // Nothing to update.
            }

            //
            // Call on the locator registry outside the synchronization to
            // blocking other threads that need to lock this OA.
            //
            LocatorRegistryPrx locatorRegistry = locatorInfo.getLocatorRegistry();
            if(locatorRegistry == null)
            {
                return;
            }

            try
            {
                if(_replicaGroupId.Length == 0)
                {
                    locatorRegistry.setAdapterDirectProxy(_id, proxy);
                }
                else
                {
                    locatorRegistry.setReplicatedAdapterDirectProxy(_id, _replicaGroupId, proxy);
                }
            }
            catch(AdapterNotFoundException)
            {
                if(instance_.traceLevels().location >= 1)
                {
                    System.Text.StringBuilder s = new System.Text.StringBuilder();
                    s.Append("couldn't update object adapter `" + _id + "' endpoints with the locator registry:\n");
                    s.Append("the object adapter is not known to the locator registry");
                    instance_.initializationData().logger.trace(instance_.traceLevels().locationCat, s.ToString());
                }

                NotRegisteredException ex1 = new NotRegisteredException();
                ex1.kindOfObject = "object adapter";
                ex1.id = _id;
                throw ex1;
            }
            catch(InvalidReplicaGroupIdException)
            {
                if(instance_.traceLevels().location >= 1)
                {
                    System.Text.StringBuilder s = new System.Text.StringBuilder();
                    s.Append("couldn't update object adapter `" + _id + "' endpoints with the locator registry:\n");
                    s.Append("the replica group `" + _replicaGroupId + "' is not known to the locator registry");
                    instance_.initializationData().logger.trace(instance_.traceLevels().locationCat, s.ToString());
                }

                NotRegisteredException ex1 = new NotRegisteredException();
                ex1.kindOfObject = "replica group";
                ex1.id = _replicaGroupId;
                throw ex1;
            }
            catch(AdapterAlreadyActiveException)
            {
                if(instance_.traceLevels().location >= 1)
                {
                    System.Text.StringBuilder s = new System.Text.StringBuilder();
                    s.Append("couldn't update object adapter `" + _id + "' endpoints with the locator registry:\n");
                    s.Append("the object adapter endpoints are already set");
                    instance_.initializationData().logger.trace(instance_.traceLevels().locationCat, s.ToString());
                }

                ObjectAdapterIdInUseException ex1 = new ObjectAdapterIdInUseException();
                ex1.id = _id;
                throw;
            }
            catch(ObjectAdapterDeactivatedException)
            {
                // Expected if collocated call and OA is deactivated, ignore.
            }
            catch(CommunicatorDestroyedException)
            {
                // Ignore
            }
            catch(LocalException e)
            {
                if(instance_.traceLevels().location >= 1)
                {
                    System.Text.StringBuilder s = new System.Text.StringBuilder();
                    s.Append("couldn't update object adapter `" + _id + "' endpoints with the locator registry:\n");
                    s.Append(e.ToString());
                    instance_.initializationData().logger.trace(instance_.traceLevels().locationCat, s.ToString());
                }
                throw; // TODO: Shall we raise a special exception instead of a non obvious local exception?
            }

            if(instance_.traceLevels().location >= 1)
            {
                System.Text.StringBuilder s = new System.Text.StringBuilder();
                s.Append("updated object adapter `" + _id + "' endpoints with the locator registry\n");
                s.Append("endpoints = ");
                if(proxy != null)
                {
                    Ice.Endpoint[] endpoints = proxy.ice_getEndpoints();
                    for(int i = 0; i < endpoints.Length; i++)
                    {
                        s.Append(endpoints[i].ToString());
                        if(i + 1 < endpoints.Length)
                        {
                            s.Append(":");
                        }
                    }
                }
                instance_.initializationData().logger.trace(instance_.traceLevels().locationCat, s.ToString());
            }
        }

        static private readonly string[] _suffixes =
        {
            "ACM",
            "ACM.Timeout",
            "ACM.Heartbeat",
            "ACM.Close",
            "AdapterId",
            "Endpoints",
            "Locator",
            "Locator.EncodingVersion",
            "Locator.EndpointSelection",
            "Locator.ConnectionCached",
            "Locator.PreferSecure",
            "Locator.CollocationOptimized",
            "Locator.Router",
            "MessageSizeMax",
            "PublishedEndpoints",
            "ReplicaGroupId",
            "Router",
            "Router.EncodingVersion",
            "Router.EndpointSelection",
            "Router.ConnectionCached",
            "Router.PreferSecure",
            "Router.CollocationOptimized",
            "Router.Locator",
            "Router.Locator.EndpointSelection",
            "Router.Locator.ConnectionCached",
            "Router.Locator.PreferSecure",
            "Router.Locator.CollocationOptimized",
            "Router.Locator.LocatorCacheTimeout",
            "Router.Locator.InvocationTimeout",
            "Router.LocatorCacheTimeout",
            "Router.InvocationTimeout",
            "ProxyOptions",
            "ThreadPool.Size",
            "ThreadPool.SizeMax",
            "ThreadPool.SizeWarn",
            "ThreadPool.StackSize",
            "ThreadPool.Serialize"
        };

        private bool filterProperties(List<string> unknownProps)
        {
            //
            // Do not create unknown properties list if Ice prefix, ie Ice, Glacier2, etc
            //
            bool addUnknown = true;
            String prefix = _name + ".";
            for(int i = 0; PropertyNames.clPropNames[i] != null; ++i)
            {
                if(prefix.StartsWith(PropertyNames.clPropNames[i] + ".", StringComparison.Ordinal))
                {
                    addUnknown = false;
                    break;
                }
            }

            bool noProps = true;
            Dictionary<string, string> props =
                instance_.initializationData().properties.getPropertiesForPrefix(prefix);
            foreach(String prop in props.Keys)
            {
                bool valid = false;
                for(int i = 0; i < _suffixes.Length; ++i)
                {
                    if(prop.Equals(prefix + _suffixes[i]))
                    {
                        noProps = false;
                        valid = true;
                        break;
                    }
                }

                if(!valid && addUnknown)
                {
                    unknownProps.Add(prop);
                }
            }

            return noProps;
        }

        private const int StateUninitialized = 0; // Just constructed.
        private const int StateHeld = 1;
        private const int StateActivating = 2;
        private const int StateActive = 3;
        private const int StateDeactivating = 4;
        private const int StateDeactivated = 5;
        private const int StateDestroying  = 6;
        private const int StateDestroyed  = 7;

        private int state_ = StateUninitialized;
        private Instance instance_;
        private Communicator _communicator;
        private ObjectAdapterFactory _objectAdapterFactory;
        private ThreadPool _threadPool;
        private ACMConfig _acm;
        private ServantManager _servantManager;
        private readonly string _name;
        private readonly string _id;
        private readonly string _replicaGroupId;
        private Reference _reference;
        private List<IncomingConnectionFactory> _incomingConnectionFactories;
        private List<EndpointI> _routerEndpoints;
        private RouterInfo _routerInfo;
        private List<EndpointI> _publishedEndpoints;
        private LocatorInfo _locatorInfo;
        private int _directCount;  // The number of direct proxies dispatching on this object adapter.
        private bool _noConfig;
        private int _messageSizeMax;
    }
}
