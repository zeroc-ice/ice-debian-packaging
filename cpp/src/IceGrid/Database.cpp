// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <IceUtil/StringUtil.h>
#include <IceUtil/Random.h>
#include <IceUtil/Functional.h>
#include <Ice/LoggerUtil.h>
#include <Ice/Communicator.h>
#include <Ice/ObjectAdapter.h>
#include <IceGrid/Database.h>
#include <IceGrid/TraceLevels.h>
#include <IceGrid/Util.h>
#include <IceGrid/DescriptorHelper.h>
#include <IceGrid/NodeSessionI.h>
#include <IceGrid/ReplicaSessionI.h>
#include <IceGrid/Session.h>
#include <IceGrid/Topics.h>
#include <IceGrid/IceGrid.h>
#include <IceGrid/SerialsDict.h>

#include <algorithm>
#include <functional>
#include <iterator>

using namespace std;
using namespace IceGrid;
using namespace Freeze;

namespace
{

const string applicationsDbName = "applications";
const string adaptersDbName = "adapters";
const string objectsDbName = "objects";
const string internalObjectsDbName = "internal-objects";
const string serialsDbName = "serials";

struct ObjectLoadCI : binary_function<pair<Ice::ObjectPrx, float>&, pair<Ice::ObjectPrx, float>&, bool>
{
    bool operator()(const pair<Ice::ObjectPrx, float>& lhs, const pair<Ice::ObjectPrx, float>& rhs)
    {
        return lhs.second < rhs.second;
    }
};

template<typename K, typename V, typename C, typename Comp> vector<V>
toVector(const Map<K, V, C, Comp>& m)
{
    vector<V> v;
    for(typename Map<K, V, C, Comp>::const_iterator p = m.begin(); p != m.end(); ++p)
    {
        v.push_back(p->second);
    }
    return v;
}

template<typename K, typename V, typename C, typename Comp> map<K, V>
toMap(const Map<K, V, C, Comp>& d)
{
    std::map<K, V> m;
    for(typename Map<K, V, C, Comp>::const_iterator p = d.begin(); p != d.end(); ++p)
    {
#ifdef __SUNPRO_CC
        std::map<Key, Value>::value_type v(p->first, p->second);
        m.insert(v);
#else
        m.insert(*p);
#endif
    }
    return m;
}

void
halt(const Ice::CommunicatorPtr& com, const DatabaseException& ex)
{
    {
        Ice::Error error(com->getLogger());
        error << "fatal exception: " << ex << "\n*** Aborting application ***";
    }

    abort();
}

void
filterAdapterInfos(const string& filter,
                   const string& replicaGroupId,
                   const RegistryPluginFacadeIPtr& pluginFacade,
                   const Ice::ConnectionPtr& con,
                   const Ice::Context& ctx,
                   AdapterInfoSeq& infos)
{
    if(infos.empty() || !pluginFacade->hasReplicaGroupFilters())
    {
        return;
    }

    vector<ReplicaGroupFilterPtr> filters = pluginFacade->getReplicaGroupFilters(filter);
    if(filters.empty())
    {
        return;
    }

    Ice::StringSeq adapterIds;
    adapterIds.reserve(infos.size());
    for(vector<AdapterInfo>::const_iterator p = infos.begin(); p != infos.end(); ++p)
    {
        adapterIds.push_back(p->id);
    }

    for(vector<ReplicaGroupFilterPtr>::const_iterator q = filters.begin(); q != filters.end(); ++q)
    {
        adapterIds = (*q)->filter(replicaGroupId, adapterIds, con, ctx);
    }

    vector<AdapterInfo> filteredAdpts;
    filteredAdpts.reserve(infos.size());
    for(Ice::StringSeq::const_iterator q = adapterIds.begin(); q != adapterIds.end(); ++q)
    {
        for(vector<AdapterInfo>::const_iterator r = infos.begin(); r != infos.end(); ++r)
        {
            if(*q == r->id)
            {
                filteredAdpts.push_back(*r);
                break;
            }
        }
    }
    infos.swap(filteredAdpts);
}

Ice::Long
getSerial(const Freeze::ConnectionPtr& connection, const string& dbName)
{
    SerialsDict dict(connection, serialsDbName);

    //
    // If a serial number is provided, juste update the serial number from the database,
    // otherwise if the serial is 0, we increment the serial from the database.
    //
    SerialsDict::iterator p = dict.find(dbName);
    if(p == dict.end())
    {
        dict.insert(SerialsDict::value_type(dbName, 1));
        return 1;
    }
    return p->second;
}

Ice::Long
updateSerial(const Freeze::ConnectionPtr& connection, const string& dbName, Ice::Long serial = 0)
{
    if(serial == -1) // Master doesn't support serials.
    {
        return -1;
    }

    SerialsDict dict(connection, serialsDbName);

    //
    // If a serial number is provided, juste update the serial number from the database,
    // otherwise if the serial is 0, we increment the serial from the database.
    //
    SerialsDict::iterator p = dict.find(dbName);
    if(p == dict.end())
    {
        dict.insert(SerialsDict::value_type(dbName, serial == 0 ? 1 : serial));
        return 1;
    }
    else
    {
        p.set(serial == 0 ? p->second + 1 : serial);
        return p->second;
    }
}

vector<AdapterInfo>
findByReplicaGroupId(const StringAdapterInfoDict& dict, const string& name)
{
    vector<AdapterInfo> result;
    for(StringAdapterInfoDict::const_iterator p = dict.findByReplicaGroupId(name, true); p != dict.end(); ++p)
    {
        result.push_back(p->second);
    }
    return result;
}

vector<ObjectInfo>
findByType(const IdentityObjectInfoDict& dict, const string& type)
{
    vector<ObjectInfo> result;
    for(IdentityObjectInfoDict::const_iterator p = dict.findByType(type); p != dict.end(); ++p)
    {
        result.push_back(p->second);
    }
    return result;
}

}

Database::Database(const Ice::ObjectAdapterPtr& registryAdapter,
                   const IceStorm::TopicManagerPrx& topicManager,
                   const string& instanceName,
                   const TraceLevelsPtr& traceLevels,
                   const RegistryInfo& info,
                   const Freeze::ConnectionPtr& connection,
                   const string& envName,
                   bool readonly) :
    _communicator(registryAdapter->getCommunicator()),
    _internalAdapter(registryAdapter),
    _topicManager(topicManager),
    _instanceName(instanceName),
    _traceLevels(traceLevels),
    _master(info.name == "Master"),
    _readonly(readonly || !_master),
    _replicaCache(_communicator, topicManager),
    _nodeCache(_communicator, _replicaCache, _readonly && _master ? string("Master (read-only)") : info.name),
    _adapterCache(_communicator),
    _objectCache(_communicator),
    _allocatableObjectCache(_communicator),
    _serverCache(_communicator, _instanceName, _nodeCache, _adapterCache, _objectCache, _allocatableObjectCache),
    _connection(connection),
    _envName(envName),
    _applications(_connection, applicationsDbName),
    _adapters(_connection, adaptersDbName),
    _objects(_connection, objectsDbName),
    _internalObjects(_connection, internalObjectsDbName),
    _pluginFacade(RegistryPluginFacadeIPtr::dynamicCast(getRegistryPluginFacade())),
    _lock(0)
{
    ServerEntrySeq entries;

    for(StringApplicationInfoDict::iterator p = _applications.begin(); p != _applications.end(); ++p)
    {
        try
        {
            load(ApplicationHelper(_communicator, p->second.descriptor), entries, p->second.uuid, p->second.revision);
        }
        catch(const DeploymentException& ex)
        {
            Ice::Error err(_traceLevels->logger);
            err << "invalid application `" << p->first << "':\n" << ex.reason;
        }
    }

    _serverCache.setTraceLevels(_traceLevels);
    _nodeCache.setTraceLevels(_traceLevels);
    _replicaCache.setTraceLevels(_traceLevels);
    _adapterCache.setTraceLevels(_traceLevels);
    _objectCache.setTraceLevels(_traceLevels);
    _allocatableObjectCache.setTraceLevels(_traceLevels);

    _nodeObserverTopic = new NodeObserverTopic(_topicManager, _internalAdapter);
    _registryObserverTopic = new RegistryObserverTopic(_topicManager);
    _applicationObserverTopic = new ApplicationObserverTopic(_topicManager, toMap(_applications), getSerial(_connection, applicationsDbName));
    _adapterObserverTopic = new AdapterObserverTopic(_topicManager, toMap(_adapters), getSerial(_connection, adaptersDbName));
    _objectObserverTopic = new ObjectObserverTopic(_topicManager, toMap(_objects), getSerial(_connection, objectsDbName));

    _registryObserverTopic->registryUp(info);

    _pluginFacade->setDatabase(this);
}

std::string
Database::getInstanceName() const
{
    return _instanceName;
}

void
Database::destroy()
{
    _pluginFacade->setDatabase(0);

    _registryObserverTopic->destroy();
    _nodeObserverTopic->destroy();
    _applicationObserverTopic->destroy();
    _adapterObserverTopic->destroy();
    _objectObserverTopic->destroy();
}

ObserverTopicPtr
Database::getObserverTopic(TopicName name) const
{
    switch(name)
    {
    case RegistryObserverTopicName:
        return _registryObserverTopic;
    case NodeObserverTopicName:
        return _nodeObserverTopic;
    case ApplicationObserverTopicName:
        return _applicationObserverTopic;
    case AdapterObserverTopicName:
        return _adapterObserverTopic;
    case ObjectObserverTopicName:
        return _objectObserverTopic;
    default:
        break;
    }
    return 0;
}

void
Database::checkSessionLock(AdminSessionI* session)
{
    if(_lock != 0 && session != _lock)
    {
        throw AccessDeniedException(_lockUserId); // Lock held by another session.
    }
}

int
Database::lock(AdminSessionI* session, const string& userId)
{
    Lock sync(*this);

    if(_lock != 0 && session != _lock)
    {
        throw AccessDeniedException(_lockUserId); // Lock held by another session.
    }
    assert(_lock == 0 || _lock == session);

    _lock = session;
    _lockUserId = userId;

    return _applicationObserverTopic->getSerial();
}

void
Database::unlock(AdminSessionI* session)
{
    Lock sync(*this);
    if(_lock != session)
    {
        throw AccessDeniedException();
    }

    _lock = 0;
    _lockUserId.clear();
}

void
Database::syncApplications(const ApplicationInfoSeq& newApplications, Ice::Long dbSerial)
{
    assert(dbSerial != 0);
    int serial = 0;
    {
        Lock sync(*this);

        map<string, ApplicationInfo> oldApplications;
        for(;;)
        {
            try
            {
                TransactionHolder txHolder(_connection);
                oldApplications = toMap(_applications);
                _applications.clear();
                for(ApplicationInfoSeq::const_iterator p = newApplications.begin(); p != newApplications.end(); ++p)
                {
                    _applications.put(StringApplicationInfoDict::value_type(p->descriptor.name, *p));
                }
                dbSerial = updateSerial(_connection, applicationsDbName, dbSerial);
                txHolder.commit();
                break;
            }
            catch(const DeadlockException&)
            {
                continue;
            }
            catch(const DatabaseException& ex)
            {
                halt(_communicator, ex);
            }
        }

        ServerEntrySeq entries;
        set<string> names;

        for(ApplicationInfoSeq::const_iterator p = newApplications.begin(); p != newApplications.end(); ++p)
        {
            try
            {
                map<string, ApplicationInfo>::const_iterator q = oldApplications.find(p->descriptor.name);
                if(q != oldApplications.end())
                {
                    ApplicationHelper previous(_communicator, q->second.descriptor);
                    ApplicationHelper helper(_communicator, p->descriptor);
                    reload(previous, helper, entries, p->uuid, p->revision, false);
                }
                else
                {
                    load(ApplicationHelper(_communicator, p->descriptor), entries, p->uuid, p->revision);
                }
            }
            catch(const DeploymentException& ex)
            {
                Ice::Warning warn(_traceLevels->logger);
                warn << "invalid application `" << p->descriptor.name << "':\n" << ex.reason;
            }
            names.insert(p->descriptor.name);
        }

        for(map<string, ApplicationInfo>::iterator s = oldApplications.begin(); s != oldApplications.end(); ++s)
        {
            if(names.find(s->first) == names.end())
            {
                unload(ApplicationHelper(_communicator, s->second.descriptor), entries);
            }
        }

        for_each(entries.begin(), entries.end(), IceUtil::voidMemFun(&ServerEntry::sync));

        if(_traceLevels->application > 0)
        {
            Ice::Trace out(_traceLevels->logger, _traceLevels->applicationCat);
            out << "synchronized applications (serial = `" << dbSerial << "')";
        }

        serial = _applicationObserverTopic->applicationInit(dbSerial, newApplications);
    }
    _applicationObserverTopic->waitForSyncedSubscribers(serial);
}

void
Database::syncAdapters(const AdapterInfoSeq& adapters, Ice::Long dbSerial)
{
    assert(dbSerial != 0);
    int serial = 0;
    {
        Lock sync(*this);
        for(;;)
        {
            try
            {
                TransactionHolder txHolder(_connection);
                _adapters.clear();
                for(AdapterInfoSeq::const_iterator r = adapters.begin(); r != adapters.end(); ++r)
                {
                    _adapters.put(StringAdapterInfoDict::value_type(r->id, *r));
                }
                dbSerial = updateSerial(_connection, adaptersDbName, dbSerial);
                txHolder.commit();
                break;
            }
            catch(const DeadlockException&)
            {
                continue;
            }
            catch(const DatabaseException& ex)
            {
                halt(_communicator, ex);
            }
        }

        if(_traceLevels->adapter > 0)
        {
            Ice::Trace out(_traceLevels->logger, _traceLevels->adapterCat);
            out << "synchronized adapters (serial = `" << dbSerial << "')";
        }

        serial = _adapterObserverTopic->adapterInit(dbSerial, adapters);
    }
    _adapterObserverTopic->waitForSyncedSubscribers(serial);
}

void
Database::syncObjects(const ObjectInfoSeq& objects, Ice::Long dbSerial)
{
    assert(dbSerial != 0);
    int serial = 0;
    {
        Lock sync(*this);
        for(;;)
        {
            try
            {
                TransactionHolder txHolder(_connection);
                _objects.clear();
                for(ObjectInfoSeq::const_iterator q = objects.begin(); q != objects.end(); ++q)
                {
                    _objects.put(IdentityObjectInfoDict::value_type(q->proxy->ice_getIdentity(), *q));
                }
                dbSerial = updateSerial(_connection, objectsDbName, dbSerial);
                txHolder.commit();
                break;
            }
            catch(const DeadlockException&)
            {
                continue;
            }
            catch(const DatabaseException& ex)
            {
                halt(_communicator, ex);
            }
        }

        if(_traceLevels->object > 0)
        {
            Ice::Trace out(_traceLevels->logger, _traceLevels->objectCat);
            out << "synchronized objects (serial = `" << dbSerial << "')";
        }

        serial = _objectObserverTopic->objectInit(dbSerial, objects);
    }
    _objectObserverTopic->waitForSyncedSubscribers(serial);
}

ApplicationInfoSeq
Database::getApplications(Ice::Long& serial) const
{
    for(;;)
    {
        try
        {
            ConnectionPtr connection = Freeze::createConnection(_communicator, _envName);
            TransactionHolder txHolder(connection);
            StringApplicationInfoDict applications(connection, applicationsDbName);
            serial = getSerial(connection, applicationsDbName);
            return toVector(applications);
        }
        catch(const DeadlockException&)
        {
            continue;
        }
        catch(const DatabaseException& ex)
        {
            halt(_communicator, ex);
        }
    }
}

AdapterInfoSeq
Database::getAdapters(Ice::Long& serial) const
{
    for(;;)
    {
        try
        {
            ConnectionPtr connection = Freeze::createConnection(_communicator, _envName);
            TransactionHolder txHolder(connection);
            StringAdapterInfoDict adapters(connection, adaptersDbName);
            serial = getSerial(connection, adaptersDbName);
            return toVector(adapters);
        }
        catch(const DeadlockException&)
        {
            continue;
        }
        catch(const DatabaseException& ex)
        {
            halt(_communicator, ex);
        }
    }
}

ObjectInfoSeq
Database::getObjects(Ice::Long& serial) const
{
    for(;;)
    {
        try
        {
            ConnectionPtr connection = Freeze::createConnection(_communicator, _envName);
            TransactionHolder txHolder(connection);
            IdentityObjectInfoDict objects(connection, objectsDbName);
            serial = getSerial(connection, objectsDbName);
            return toVector(objects);
        }
        catch(const DeadlockException&)
        {
            continue;
        }
        catch(const DatabaseException& ex)
        {
            halt(_communicator, ex);
        }
    }
}

StringLongDict
Database::getSerials() const
{
    SerialsDict serials(Freeze::createConnection(_communicator, _envName), serialsDbName);
    return toMap(serials);
}

void
Database::addApplication(const ApplicationInfo& info, AdminSessionI* session, Ice::Long dbSerial)
{
    assert(dbSerial != 0 || _master);

    int serial = 0; // Initialize to prevent warning.
    ServerEntrySeq entries;
    try
    {
        Lock sync(*this);
        checkSessionLock(session);

        waitForUpdate(info.descriptor.name);

        StringApplicationInfoDict::const_iterator i = _applications.find(info.descriptor.name);
        if(i != _applications.end())
        {
            throw DeploymentException("application `" + info.descriptor.name + "' already exists");
        }

        ApplicationHelper helper(_communicator, info.descriptor, true);
        checkForAddition(helper, _connection);
        dbSerial = saveApplication(info, _connection, dbSerial);
        load(helper, entries, info.uuid, info.revision);
        startUpdating(info.descriptor.name, info.uuid, info.revision);

        for_each(entries.begin(), entries.end(), IceUtil::voidMemFun(&ServerEntry::sync));
        serial = _applicationObserverTopic->applicationAdded(dbSerial, info);
    }
    catch(const DatabaseException& ex)
    {
        halt(_communicator, ex);
    }

    _applicationObserverTopic->waitForSyncedSubscribers(serial); // Wait for replicas to be updated.

    //
    // Mark the application as updated. All the replicas received the update so it's now safe
    // for the nodes to start the servers.
    //
    {
        Lock sync(*this);
        vector<UpdateInfo>::iterator p = find(_updating.begin(), _updating.end(), info.descriptor.name);
        assert(p != _updating.end());
        p->markUpdated();
    }

    if(_master)
    {
        try
        {
            for(ServerEntrySeq::const_iterator p = entries.begin(); p != entries.end(); ++p)
            {
                try
                {
                    (*p)->waitForSync();
                }
                catch(const NodeUnreachableException&)
                {
                    // Ignore.
                }
            }
        }
        catch(const DeploymentException& ex)
        {
            try
            {
                Lock sync(*this);
                entries.clear();
                unload(ApplicationHelper(_communicator, info.descriptor), entries);
                dbSerial = removeApplication(info.descriptor.name, _connection);

                for_each(entries.begin(), entries.end(), IceUtil::voidMemFun(&ServerEntry::sync));
                serial = _applicationObserverTopic->applicationRemoved(dbSerial, info.descriptor.name);
            }
            catch(const DeploymentException& ex)
            {
                Ice::Error err(_traceLevels->logger);
                err << "failed to rollback previous application `" << info.descriptor.name << "':\n" << ex.reason;
            }
            catch(const DatabaseException& ex)
            {
                halt(_communicator, ex);
            }
            _applicationObserverTopic->waitForSyncedSubscribers(serial);
            for_each(entries.begin(), entries.end(), IceUtil::voidMemFun(&ServerEntry::waitForSyncNoThrow));
            finishUpdating(info.descriptor.name);
            throw ex;
        }
    }

    if(_traceLevels->application > 0)
    {
        Ice::Trace out(_traceLevels->logger, _traceLevels->applicationCat);
        out << "added application `" << info.descriptor.name << "' (serial = `" << dbSerial << "')";
    }
    finishUpdating(info.descriptor.name);
}

void
Database::updateApplication(const ApplicationUpdateInfo& updt, bool noRestart, AdminSessionI* session,
                            Ice::Long dbSerial)
{
    assert(dbSerial != 0 || _master);

    ApplicationInfo oldApp;
    ApplicationUpdateInfo update = updt;
    IceUtil::UniquePtr<ApplicationHelper> previous;
    IceUtil::UniquePtr<ApplicationHelper> helper;
    try
    {
        Lock sync(*this);
        checkSessionLock(session);

        waitForUpdate(update.descriptor.name);

        StringApplicationInfoDict::const_iterator i = _applications.find(update.descriptor.name);
        if(i == _applications.end())
        {
            throw ApplicationNotExistException(update.descriptor.name);
        }
        oldApp = i->second;

        if(update.revision < 0)
        {
            update.revision = oldApp.revision + 1;
        }

        previous.reset(new ApplicationHelper(_communicator, oldApp.descriptor));
        helper.reset(new ApplicationHelper(_communicator, previous->update(update.descriptor), true));

        startUpdating(update.descriptor.name, oldApp.uuid, oldApp.revision + 1);
    }
    catch(const DatabaseException& ex)
    {
        halt(_communicator, ex);
    }

    finishApplicationUpdate(update, oldApp, *previous, *helper, session, noRestart, dbSerial);
}

void
Database::syncApplicationDescriptor(const ApplicationDescriptor& newDesc, bool noRestart, AdminSessionI* session)
{
    assert(_master);

    ApplicationUpdateInfo update;
    ApplicationInfo oldApp;
    IceUtil::UniquePtr<ApplicationHelper> previous;
    IceUtil::UniquePtr<ApplicationHelper> helper;
    try
    {
        Lock sync(*this);
        checkSessionLock(session);

        waitForUpdate(newDesc.name);

        StringApplicationInfoDict::const_iterator i = _applications.find(newDesc.name);
        if(i == _applications.end())
        {
            throw ApplicationNotExistException(newDesc.name);
        }
        oldApp = i->second;

        previous.reset(new ApplicationHelper(_communicator, oldApp.descriptor));
        helper.reset(new ApplicationHelper(_communicator, newDesc, true));

        update.updateTime = IceUtil::Time::now().toMilliSeconds();
        update.updateUser = _lockUserId;
        update.revision = oldApp.revision + 1;
        update.descriptor = helper->diff(*previous);

        startUpdating(update.descriptor.name, oldApp.uuid, oldApp.revision + 1);
    }
    catch(const DatabaseException& ex)
    {
        halt(_communicator, ex);
    }

    finishApplicationUpdate(update, oldApp, *previous, *helper, session, noRestart);
}

void
Database::instantiateServer(const string& application,
                            const string& node,
                            const ServerInstanceDescriptor& instance,
                            AdminSessionI* session)
{
    assert(_master);

    ApplicationUpdateInfo update;
    ApplicationInfo oldApp;
    IceUtil::UniquePtr<ApplicationHelper> previous;
    IceUtil::UniquePtr<ApplicationHelper> helper;

    try
    {
        Lock sync(*this);
        checkSessionLock(session);

        waitForUpdate(application);

        StringApplicationInfoDict::const_iterator i = _applications.find(application);
        if(i == _applications.end())
        {
            throw ApplicationNotExistException(application);

        }
        oldApp = i->second;

        previous.reset(new ApplicationHelper(_communicator, oldApp.descriptor));
        helper.reset(new ApplicationHelper(_communicator, previous->instantiateServer(node, instance), true));

        update.updateTime = IceUtil::Time::now().toMilliSeconds();
        update.updateUser = _lockUserId;
        update.revision = oldApp.revision + 1;
        update.descriptor = helper->diff(*previous);

        startUpdating(update.descriptor.name, oldApp.uuid, oldApp.revision + 1);
    }
    catch(const DatabaseException& ex)
    {
        halt(_communicator, ex);
    }

    finishApplicationUpdate(update, oldApp, *previous, *helper, session, true);
}

void
Database::removeApplication(const string& name, AdminSessionI* session, Ice::Long dbSerial)
{
    assert(dbSerial != 0 || _master);
    ServerEntrySeq entries;

    int serial = 0; // Initialize to prevent warning.
    try
    {
        Lock sync(*this);
        checkSessionLock(session);

        waitForUpdate(name);

        ApplicationInfo appInfo;

        StringApplicationInfoDict::const_iterator i = _applications.find(name);
        if(i == _applications.end())
        {
            throw ApplicationNotExistException(name);
        }
        appInfo = i->second;

        bool init = false;
        try
        {
            ApplicationHelper helper(_communicator, appInfo.descriptor);
            init = true;
            checkForRemove(helper);
            unload(helper, entries);
        }
        catch(const DeploymentException&)
        {
            if(init)
            {
                throw;
            }
        }

        dbSerial = removeApplication(name, _connection, dbSerial);
        startUpdating(name, appInfo.uuid, appInfo.revision);

        for_each(entries.begin(), entries.end(), IceUtil::voidMemFun(&ServerEntry::sync));
        serial = _applicationObserverTopic->applicationRemoved(dbSerial, name);
    }
    catch(const DatabaseException& ex)
    {
        halt(_communicator, ex);
    }
    _applicationObserverTopic->waitForSyncedSubscribers(serial);

    if(_master)
    {
        for_each(entries.begin(), entries.end(), IceUtil::voidMemFun(&ServerEntry::waitForSyncNoThrow));
    }

    if(_traceLevels->application > 0)
    {
        Ice::Trace out(_traceLevels->logger, _traceLevels->applicationCat);
        out << "removed application `" << name << "' (serial = `" << dbSerial << "')";
    }

    finishUpdating(name);
}

ApplicationInfo
Database::getApplicationInfo(const std::string& name)
{
    ConnectionPtr connection = Freeze::createConnection(_communicator, _envName);
    StringApplicationInfoDict applications(connection, applicationsDbName);
    StringApplicationInfoDict::const_iterator i = applications.find(name);
    if(i == applications.end())
    {
        throw ApplicationNotExistException(name);
    }
    return i->second;
}

Ice::StringSeq
Database::getAllApplications(const string& expression)
{
    ConnectionPtr connection = Freeze::createConnection(_communicator, _envName);
    StringApplicationInfoDict applications(connection, applicationsDbName);
    return getMatchingKeys<map<string, ApplicationInfo> >(toMap(applications), expression);
}

void
Database::waitForApplicationUpdate(const AMD_NodeSession_waitForApplicationUpdatePtr& cb,
                                   const string& uuid,
                                   int revision)
{
    Lock sync(*this);

    vector<UpdateInfo>::iterator p = find(_updating.begin(), _updating.end(), make_pair(uuid, revision));
    if(p != _updating.end() && !p->updated)
    {
        p->cbs.push_back(cb);
    }
    else
    {
        cb->ice_response();
    }
}

NodeCache&
Database::getNodeCache()
{
    return _nodeCache;
}

NodeEntryPtr
Database::getNode(const string& name, bool create) const
{
    return _nodeCache.get(name, create);
}

ReplicaCache&
Database::getReplicaCache()
{
    return _replicaCache;
}

ReplicaEntryPtr
Database::getReplica(const string& name) const
{
    return _replicaCache.get(name);
}

ServerCache&
Database::getServerCache()
{
    return _serverCache;
}

ServerEntryPtr
Database::getServer(const string& id) const
{
    return _serverCache.get(id);
}

AllocatableObjectCache&
Database::getAllocatableObjectCache()
{
    return _allocatableObjectCache;
}

AllocatableObjectEntryPtr
Database::getAllocatableObject(const Ice::Identity& id) const
{
    return _allocatableObjectCache.get(id);
}

void
Database::setAdapterDirectProxy(const string& adapterId, const string& replicaGroupId, const Ice::ObjectPrx& proxy,
                                Ice::Long dbSerial)
{
    assert(dbSerial != 0 || _master);

    int serial = 0; // Initialize to prevent warning.
    {
        Lock sync(*this);
        if(_adapterCache.has(adapterId))
        {
            throw AdapterExistsException(adapterId);
        }

        AdapterInfo info;
        info.id = adapterId;
        info.proxy = proxy;
        info.replicaGroupId = replicaGroupId;

        bool updated = false;
        for(;;)
        {
            try
            {
                TransactionHolder txHolder(_connection);
                StringAdapterInfoDict::iterator i = _adapters.find(adapterId);
                if(proxy)
                {
                    if(i == _adapters.end())
                    {
                        _adapters.put(StringAdapterInfoDict::value_type(adapterId, info));
                    }
                    else
                    {
                        updated = true;
                        i.set(info);
                    }
                }
                else
                {
                    if(i == _adapters.end())
                    {
                        return;
                    }
                    _adapters.erase(i);
                }
                dbSerial = updateSerial(_connection, adaptersDbName, dbSerial);
                txHolder.commit();
                break;
            }
            catch(const DeadlockException&)
            {
                continue;
            }
            catch(const DatabaseException& ex)
            {
                halt(_communicator, ex);
            }
        }

        if(_traceLevels->adapter > 0)
        {
            Ice::Trace out(_traceLevels->logger, _traceLevels->adapterCat);
            out << (proxy ? (updated ? "updated" : "added") : "removed") << " adapter `" << adapterId << "'";
            if(!replicaGroupId.empty())
            {
                out << " with replica group `" << replicaGroupId << "'";
            }
	    out << " (serial = `" << dbSerial << "')";
        }

        if(proxy)
        {
            if(updated)
            {
                serial = _adapterObserverTopic->adapterUpdated(dbSerial, info);
            }
            else
            {
                serial = _adapterObserverTopic->adapterAdded(dbSerial, info);
            }
        }
        else
        {
            serial = _adapterObserverTopic->adapterRemoved(dbSerial, adapterId);
        }
    }
    _adapterObserverTopic->waitForSyncedSubscribers(serial);
}

Ice::ObjectPrx
Database::getAdapterDirectProxy(const string& id, const Ice::EncodingVersion& encoding, const Ice::ConnectionPtr& con,
                                const Ice::Context& ctx)
{
    ConnectionPtr connection = Freeze::createConnection(_communicator, _envName);
    StringAdapterInfoDict adapters(connection, adaptersDbName);
    StringAdapterInfoDict::const_iterator i = adapters.find(id);
    if(i != adapters.end())
    {
        return i->second.proxy;
    }

    Ice::EndpointSeq endpoints;
    vector<AdapterInfo> infos = findByReplicaGroupId(adapters, id);
    filterAdapterInfos("", id, _pluginFacade, con, ctx, infos);
    for(unsigned int i = 0; i < infos.size(); ++i)
    {
        if(IceInternal::isSupported(encoding, infos[i].proxy->ice_getEncodingVersion()))
        {
            Ice::EndpointSeq edpts = infos[i].proxy->ice_getEndpoints();
            endpoints.insert(endpoints.end(), edpts.begin(), edpts.end());
        }
    }
    if(!endpoints.empty())
    {
        return _communicator->stringToProxy("dummy:default")->ice_endpoints(endpoints);
    }

    throw AdapterNotExistException(id);
}

void
Database::removeAdapter(const string& adapterId)
{
    assert(_master);

    int serial = 0; // Initialize to prevent warning.
    {
        Lock sync(*this);
        if(_adapterCache.has(adapterId))
        {
            AdapterEntryPtr adpt = _adapterCache.get(adapterId);
            DeploymentException ex;
            ex.reason = "removing adapter `" + adapterId + "' is not allowed:\n";
            ex.reason += "the adapter was added with the application descriptor `" + adpt->getApplication() + "'";
            throw ex;
        }

        AdapterInfoSeq infos;
        Ice::Long dbSerial = 0;
        for(;;)
        {
            try
            {
                TransactionHolder txHolder(_connection);
                StringAdapterInfoDict::iterator i = _adapters.find(adapterId);
                if(i != _adapters.end())
                {
                    _adapters.erase(i);
                }
                else
                {
                    infos = findByReplicaGroupId(_adapters, adapterId);
                    if(infos.empty())
                    {
                        throw AdapterNotExistException(adapterId);
                    }
                    for(AdapterInfoSeq::iterator p = infos.begin(); p != infos.end(); ++p)
                    {
                        p->replicaGroupId.clear();
                        _adapters.put(StringAdapterInfoDict::value_type(p->id, *p));
                    }
                }
                dbSerial = updateSerial(_connection, adaptersDbName);
                txHolder.commit();
                break;
            }
            catch(const DeadlockException&)
            {
                continue;
            }
            catch(const DatabaseException& ex)
            {
                halt(_communicator, ex);
            }
        }

        if(_traceLevels->adapter > 0)
        {
            Ice::Trace out(_traceLevels->logger, _traceLevels->adapterCat);
            out << "removed " << (infos.empty() ? "adapter" : "replica group") << " `" << adapterId << "' (serial = `" << dbSerial << "')";
        }

        if(infos.empty())
        {
            serial = _adapterObserverTopic->adapterRemoved(dbSerial, adapterId);
        }
        else
        {
            for(AdapterInfoSeq::const_iterator p = infos.begin(); p != infos.end(); ++p)
            {
                serial = _adapterObserverTopic->adapterUpdated(dbSerial, *p);
            }
        }
    }
    _adapterObserverTopic->waitForSyncedSubscribers(serial);
}

AdapterPrx
Database::getAdapterProxy(const string& adapterId, const string& replicaGroupId, bool upToDate)
{
    Lock sync(*this); // Make sure this isn't call during an update.
    return _adapterCache.get(adapterId)->getProxy(replicaGroupId, upToDate);
}

void
Database::getLocatorAdapterInfo(const string& id,
                                const Ice::ConnectionPtr& connection,
                                const Ice::Context& context,
                                LocatorAdapterInfoSeq& adpts,
                                int& count,
                                bool& replicaGroup,
                                bool& roundRobin,
                                const set<string>& excludes)
{
    string filter;
    {
        Lock sync(*this); // Make sure this isn't call during an update.
        _adapterCache.get(id)->getLocatorAdapterInfo(adpts, count, replicaGroup, roundRobin, filter, excludes);
    }

    if(_pluginFacade->hasReplicaGroupFilters() && !adpts.empty())
    {
        vector<ReplicaGroupFilterPtr> filters = _pluginFacade->getReplicaGroupFilters(filter);
        if(!filters.empty())
        {
            Ice::StringSeq adapterIds;
            for(LocatorAdapterInfoSeq::const_iterator q = adpts.begin(); q != adpts.end(); ++q)
            {
                adapterIds.push_back(q->id);
            }

            for(vector<ReplicaGroupFilterPtr>::const_iterator q = filters.begin(); q != filters.end(); ++q)
            {
                adapterIds = (*q)->filter(id, adapterIds, connection, context);
            }

            LocatorAdapterInfoSeq filteredAdpts;
            filteredAdpts.reserve(adpts.size());
            for(Ice::StringSeq::const_iterator q = adapterIds.begin(); q != adapterIds.end(); ++q)
            {
                for(LocatorAdapterInfoSeq::const_iterator r = adpts.begin(); r != adpts.end(); ++r)
                {
                    if(*q == r->id)
                    {
                        filteredAdpts.push_back(*r);
                        break;
                    }
                }
            }
            adpts.swap(filteredAdpts);
        }
    }
}

bool
Database::addAdapterSyncCallback(const string& id,
                                 const SynchronizationCallbackPtr& callback,
                                 const std::set<std::string>& excludes)
{
    Lock sync(*this); // Make sure this isn't call during an update.
    return _adapterCache.get(id)->addSyncCallback(callback, excludes);
}

AdapterInfoSeq
Database::getAdapterInfo(const string& id)
{
    //
    // First we check if the given adapter id is associated to a
    // server, if that's the case we get the adapter proxy from the
    // server.
    //
    try
    {
        Lock sync(*this); // Make sure this isn't call during an update.
        return _adapterCache.get(id)->getAdapterInfo();
    }
    catch(const AdapterNotExistException&)
    {
    }

    //
    // Otherwise, we check the adapter endpoint table -- if there's an
    // entry the adapter is managed by the registry itself.
    //
    ConnectionPtr connection = Freeze::createConnection(_communicator, _envName);
    StringAdapterInfoDict adapters(connection, adaptersDbName);
    AdapterInfoSeq infos;
    StringAdapterInfoDict::const_iterator i = adapters.find(id);
    if(i != adapters.end())
    {
        infos.push_back(i->second);
    }
    else
    {
        //
        // If it's not a regular object adapter, perhaps it's a replica
        // group...
        //
        infos = findByReplicaGroupId(adapters, id);
        if(infos.empty())
        {
            throw AdapterNotExistException(id);
        }
    }
    return infos;
}

AdapterInfoSeq
Database::getFilteredAdapterInfo(const string& id, const Ice::ConnectionPtr& con, const Ice::Context& ctx)
{
    //
    // First we check if the given adapter id is associated to a
    // server, if that's the case we get the adapter proxy from the
    // server.
    //
    try
    {
        AdapterInfoSeq infos;
        ReplicaGroupEntryPtr replicaGroup;
        {
            Lock sync(*this); // Make sure this isn't call during an update.

            AdapterEntryPtr entry = _adapterCache.get(id);
            infos = entry->getAdapterInfo();
            replicaGroup = ReplicaGroupEntryPtr::dynamicCast(entry);
        }
        if(replicaGroup)
        {
            filterAdapterInfos(replicaGroup->getFilter(), id, _pluginFacade, con, ctx, infos);
        }
        return infos;
    }
    catch(const AdapterNotExistException&)
    {
    }

    //
    // Otherwise, we check the adapter endpoint table -- if there's an
    // entry the adapter is managed by the registry itself.
    //
    ConnectionPtr connection = Freeze::createConnection(_communicator, _envName);
    StringAdapterInfoDict adapters(connection, adaptersDbName);
    AdapterInfoSeq infos;
    StringAdapterInfoDict::const_iterator i = adapters.find(id);
    if(i != adapters.end())
    {
        infos.push_back(i->second);
    }
    else
    {
        //
        // If it's not a regular object adapter, perhaps it's a replica
        // group...
        //
        infos = findByReplicaGroupId(adapters, id);
        if(infos.empty())
        {
            throw AdapterNotExistException(id);
        }
        filterAdapterInfos("", id, _pluginFacade, con, ctx, infos);
    }
    return infos;
}

string
Database::getAdapterServer(const string& id) const
{
    try
    {
        Lock sync(*this); // Make sure this isn't call during an update.
        ServerAdapterEntryPtr adapter = ServerAdapterEntryPtr::dynamicCast(_adapterCache.get(id));
        if(adapter)
        {
            return adapter->getServerId();
        }
    }
    catch(const AdapterNotExistException&)
    {
    }
    return "";
}

string
Database::getAdapterApplication(const string& id) const
{
    try
    {
        Lock sync(*this); // Make sure this isn't call during an update.
        return _adapterCache.get(id)->getApplication();
    }
    catch(const AdapterNotExistException&)
    {
    }
    return "";
}

string
Database::getAdapterNode(const string& id) const
{
    try
    {
        Lock sync(*this); // Make sure this isn't call during an update.
        ServerAdapterEntryPtr adapter = ServerAdapterEntryPtr::dynamicCast(_adapterCache.get(id));
        if(adapter)
        {
            return adapter->getNodeName();
        }
    }
    catch(const AdapterNotExistException&)
    {
    }
    return "";
}

Ice::StringSeq
Database::getAllAdapters(const string& expression)
{
    Lock sync(*this);
    vector<string> result;
    vector<string> ids = _adapterCache.getAll(expression);
    result.swap(ids);
    set<string> groups;

    for(StringAdapterInfoDict::const_iterator p = _adapters.begin(); p != _adapters.end(); ++p)
    {
        if(expression.empty() || IceUtilInternal::match(p->first, expression, true))
        {
            result.push_back(p->first);
        }
        string replicaGroupId = p->second.replicaGroupId;
        if(!replicaGroupId.empty() && (expression.empty() || IceUtilInternal::match(replicaGroupId, expression, true)))
        {
            groups.insert(replicaGroupId);
        }
    }
    //
    // COMPILERFIX: We're not using result.insert() here, this doesn't compile on Sun.
    //
    //result.insert(result.end(), groups.begin(), groups.end())
    for(set<string>::const_iterator q = groups.begin(); q != groups.end(); ++q)
    {
        result.push_back(*q);
    }
    return result;
}

void
Database::addObject(const ObjectInfo& info)
{
    assert(_master);

    int serial = 0;
    {
        Lock sync(*this);
        const Ice::Identity id = info.proxy->ice_getIdentity();

        if(_objectCache.has(id))
        {
            throw ObjectExistsException(id);
        }

        Ice::Long dbSerial = 0;
        for(;;)
        {
            try
            {
                TransactionHolder txHolder(_connection);
                IdentityObjectInfoDict::const_iterator i = _objects.find(id);
                if(i != _objects.end())
                {
                    throw ObjectExistsException(id);
                }
                _objects.put(IdentityObjectInfoDict::value_type(id, info));
                dbSerial = updateSerial(_connection, objectsDbName);
                txHolder.commit();
                break;
            }
            catch(const DeadlockException&)
            {
                continue;
            }
            catch(const DatabaseException& ex)
            {
                halt(_communicator, ex);
            }
        }

        serial = _objectObserverTopic->objectAdded(dbSerial, info);

        if(_traceLevels->object > 0)
        {
            Ice::Trace out(_traceLevels->logger, _traceLevels->objectCat);
            out << "added object `" << _communicator->identityToString(id) << "' (serial = `" << dbSerial << "')";
        }
    }
    _objectObserverTopic->waitForSyncedSubscribers(serial);
}

void
Database::addOrUpdateObject(const ObjectInfo& info, Ice::Long dbSerial)
{
    assert(dbSerial != 0 || _master);

    int serial = 0; // Initialize to prevent warning.
    {
        Lock sync(*this);
        const Ice::Identity id = info.proxy->ice_getIdentity();

        if(_objectCache.has(id))
        {
            throw ObjectExistsException(id);
        }

        bool update = false;
        for(;;)
        {
            try
            {
                TransactionHolder txHolder(_connection);
                IdentityObjectInfoDict::iterator i = _objects.find(id);
                if(i != _objects.end())
                {
                    update = true;
                    i.set(info);
                }
                else
                {
                    _objects.put(IdentityObjectInfoDict::value_type(id, info));
                }
                dbSerial = updateSerial(_connection, objectsDbName, dbSerial);
                txHolder.commit();
                break;
            }
            catch(const DeadlockException&)
            {
                continue;
            }
            catch(const DatabaseException& ex)
            {
                halt(_communicator, ex);
            }
        }

        if(update)
        {
            serial = _objectObserverTopic->objectUpdated(dbSerial, info);
        }
        else
        {
            serial = _objectObserverTopic->objectAdded(dbSerial, info);
        }

        if(_traceLevels->object > 0)
        {
            Ice::Trace out(_traceLevels->logger, _traceLevels->objectCat);
            out << (!update ? "added" : "updated") << " object `" << _communicator->identityToString(id) << "' (serial = `" << dbSerial << "')";
        }
    }
    _objectObserverTopic->waitForSyncedSubscribers(serial);
}

void
Database::removeObject(const Ice::Identity& id, Ice::Long dbSerial)
{
    assert(dbSerial != 0 || _master);

    int serial = 0; // Initialize to prevent warning.
    {
        Lock sync(*this);
        if(_objectCache.has(id))
        {
            DeploymentException ex;
            ex.reason = "removing object `" + _communicator->identityToString(id) + "' is not allowed:\n";
            ex.reason += "the object was added with the application descriptor `";
            ex.reason += _objectCache.get(id)->getApplication();
            ex.reason += "'";
            throw ex;
        }

        for(;;)
        {
            try
            {
                TransactionHolder txHolder(_connection);
                IdentityObjectInfoDict::iterator i = _objects.find(id);
                if(i == _objects.end())
                {
                    ObjectNotRegisteredException ex;
                    ex.id = id;
                    throw ex;
                }

                _objects.erase(i);
                dbSerial = updateSerial(_connection, objectsDbName, dbSerial);
                txHolder.commit();
                break;
            }
            catch(const DeadlockException&)
            {
                continue;
            }
            catch(const DatabaseException& ex)
            {
                halt(_communicator, ex);
            }
        }

        serial = _objectObserverTopic->objectRemoved(dbSerial, id);

        if(_traceLevels->object > 0)
        {
            Ice::Trace out(_traceLevels->logger, _traceLevels->objectCat);
            out << "removed object `" << _communicator->identityToString(id) << "' (serial = `" << dbSerial << "')";
        }
    }
    _objectObserverTopic->waitForSyncedSubscribers(serial);
}

void
Database::updateObject(const Ice::ObjectPrx& proxy)
{
    assert(_master);

    int serial = 0;
    {
        Lock sync(*this);

        const Ice::Identity id = proxy->ice_getIdentity();
        if(_objectCache.has(id))
        {
            DeploymentException ex;
            ex.reason = "updating object `" + _communicator->identityToString(id) + "' is not allowed:\n";
            ex.reason += "the object was added with the application descriptor `";
            ex.reason += _objectCache.get(id)->getApplication();
            ex.reason += "'";
            throw ex;
        }

        ObjectInfo info;
        Ice::Long dbSerial = 0;
        for(;;)
        {
            try
            {
                TransactionHolder txHolder(_connection);
                IdentityObjectInfoDict::iterator i = _objects.find(id);
                if(i == _objects.end())
                {
                    ObjectNotRegisteredException ex;
                    ex.id = id;
                    throw ex;
                }
                info = i->second;
                info.proxy = proxy;
                i.set(info);
                dbSerial = updateSerial(_connection, objectsDbName);
                txHolder.commit();
                break;
            }
            catch(const DeadlockException&)
            {
                continue;
            }
            catch(const DatabaseException& ex)
            {
                halt(_communicator, ex);
            }
        }

        serial = _objectObserverTopic->objectUpdated(dbSerial, info);
        if(_traceLevels->object > 0)
        {
            Ice::Trace out(_traceLevels->logger, _traceLevels->objectCat);
            out << "updated object `" << _communicator->identityToString(id) << "' (serial = `" << dbSerial << "')";
        }
    }
    _objectObserverTopic->waitForSyncedSubscribers(serial);
}

int
Database::addOrUpdateRegistryWellKnownObjects(const ObjectInfoSeq& objects)
{
    Lock sync(*this);
    for(;;)
    {
        try
        {
            TransactionHolder txHolder(_connection);
            for(ObjectInfoSeq::const_iterator p = objects.begin(); p != objects.end(); ++p)
            {
                _objects.put(IdentityObjectInfoDict::value_type(p->proxy->ice_getIdentity(), *p));
            }
            txHolder.commit();
            break;
        }
        catch(const DeadlockException&)
        {
            continue;
        }
        catch(const DatabaseException& ex)
        {
            halt(_communicator, ex);
        }
    }
    return _objectObserverTopic->wellKnownObjectsAddedOrUpdated(objects);
}

int
Database::removeRegistryWellKnownObjects(const ObjectInfoSeq& objects)
{
    Lock sync(*this);
    for(;;)
    {
        try
        {
            TransactionHolder txHolder(_connection);
            for(ObjectInfoSeq::const_iterator p = objects.begin(); p != objects.end(); ++p)
            {
                _objects.erase(p->proxy->ice_getIdentity());
            }
            txHolder.commit();
            break;
        }
        catch(const DeadlockException&)
        {
            continue;
        }
        catch(const DatabaseException& ex)
        {
            halt(_communicator, ex);
        }
    }
    return _objectObserverTopic->wellKnownObjectsRemoved(objects);
}

Ice::ObjectPrx
Database::getObjectProxy(const Ice::Identity& id)
{
    try
    {
        //
        // Only return proxies for non allocatable objects.
        //
        return _objectCache.get(id)->getProxy();
    }
    catch(const ObjectNotRegisteredException&)
    {
    }

    ConnectionPtr connection = Freeze::createConnection(_communicator, _envName);
    IdentityObjectInfoDict objects(connection, objectsDbName);
    IdentityObjectInfoDict::const_iterator i = objects.find(id);
    if(i == objects.end())
    {
        ObjectNotRegisteredException ex;
        ex.id = id;
        throw ex;
    }
    return i->second.proxy;
}

Ice::ObjectPrx
Database::getObjectByType(const string& type, const Ice::ConnectionPtr& con, const Ice::Context& ctx)
{
    Ice::ObjectProxySeq objs = getObjectsByType(type, con, ctx);
    if(objs.empty())
    {
        return 0;
    }
    return objs[IceUtilInternal::random(static_cast<int>(objs.size()))];
}

Ice::ObjectPrx
Database::getObjectByTypeOnLeastLoadedNode(const string& type, LoadSample sample, const Ice::ConnectionPtr& con,
                                           const Ice::Context& ctx)
{
    Ice::ObjectProxySeq objs = getObjectsByType(type, con, ctx);
    if(objs.empty())
    {
        return 0;
    }

    RandomNumberGenerator rng;
    random_shuffle(objs.begin(), objs.end(), rng);
    vector<pair<Ice::ObjectPrx, float> > objectsWithLoad;
    objectsWithLoad.reserve(objs.size());
    for(Ice::ObjectProxySeq::const_iterator p = objs.begin(); p != objs.end(); ++p)
    {
        float load = 1.0f;
        if(!(*p)->ice_getAdapterId().empty())
        {
            try
            {
                load = _adapterCache.get((*p)->ice_getAdapterId())->getLeastLoadedNodeLoad(sample);
            }
            catch(const AdapterNotExistException&)
            {
            }
        }
        objectsWithLoad.push_back(make_pair(*p, load));
    }
    return min_element(objectsWithLoad.begin(), objectsWithLoad.end(), ObjectLoadCI())->first;
}


Ice::ObjectProxySeq
Database::getObjectsByType(const string& type, const Ice::ConnectionPtr& con, const Ice::Context& ctx)
{
    Ice::ObjectProxySeq proxies = _objectCache.getObjectsByType(type);

    ConnectionPtr connection = Freeze::createConnection(_communicator, _envName);
    IdentityObjectInfoDict objects(connection, objectsDbName);
    vector<ObjectInfo> infos = findByType(objects, type);
    for(unsigned int i = 0; i < infos.size(); ++i)
    {
        proxies.push_back(infos[i].proxy);
    }

    if(con && !proxies.empty() && _pluginFacade->hasTypeFilters())
    {
        vector<TypeFilterPtr> filters = _pluginFacade->getTypeFilters(type);
        if(!filters.empty())
        {
            for(vector<TypeFilterPtr>::const_iterator p = filters.begin(); p != filters.end(); ++p)
            {
                proxies = (*p)->filter(type, proxies, con, ctx);
            }
        }
    }
    return proxies;
}

ObjectInfo
Database::getObjectInfo(const Ice::Identity& id)
{
    try
    {
        ObjectEntryPtr object = _objectCache.get(id);
        return object->getObjectInfo();
    }
    catch(const ObjectNotRegisteredException&)
    {
    }

    ConnectionPtr connection = Freeze::createConnection(_communicator, _envName);
    IdentityObjectInfoDict objects(connection, objectsDbName);
    IdentityObjectInfoDict::const_iterator i = objects.find(id);
    if(i == objects.end())
    {
        throw ObjectNotRegisteredException(id);
    }
    return i->second;
}

ObjectInfoSeq
Database::getAllObjectInfos(const string& expression)
{
    ObjectInfoSeq infos = _objectCache.getAll(expression);

    ConnectionPtr connection = Freeze::createConnection(_communicator, _envName);
    IdentityObjectInfoDict objects(connection, objectsDbName);
    for(IdentityObjectInfoDict::const_iterator p = objects.begin(); p != objects.end(); ++p)
    {
        if(expression.empty() || IceUtilInternal::match(_communicator->identityToString(p->first), expression, true))
        {
            infos.push_back(p->second);
        }
    }
    return infos;
}

ObjectInfoSeq
Database::getObjectInfosByType(const string& type)
{
    ObjectInfoSeq infos = _objectCache.getAllByType(type);

    ConnectionPtr connection = Freeze::createConnection(_communicator, _envName);
    IdentityObjectInfoDict objects(connection, objectsDbName);
    ObjectInfoSeq dbInfos = findByType(objects, type);
    for(unsigned int i = 0; i < dbInfos.size(); ++i)
    {
        infos.push_back(dbInfos[i]);
    }
    return infos;
}

void
Database::addInternalObject(const ObjectInfo& info, bool replace)
{
    Lock sync(*this);
    const Ice::Identity id = info.proxy->ice_getIdentity();

    for(;;)
    {
        try
        {
            TransactionHolder txHolder(_connection);
            if(!replace)
            {
                IdentityObjectInfoDict::const_iterator i = _internalObjects.find(id);
                if(i != _internalObjects.end())
                {
                    throw ObjectExistsException(id);
                }
            }
            _internalObjects.put(IdentityObjectInfoDict::value_type(id, info));
            txHolder.commit();
            break;
        }
        catch(const DeadlockException&)
        {
            continue;
        }
        catch(const DatabaseException& ex)
        {
            halt(_communicator, ex);
        }
    }
}

void
Database::removeInternalObject(const Ice::Identity& id)
{
    Lock sync(*this);

    for(;;)
    {
        try
        {
            TransactionHolder txHolder(_connection);
            IdentityObjectInfoDict::iterator i = _internalObjects.find(id);
            if(i == _internalObjects.end())
            {
                ObjectNotRegisteredException ex;
                ex.id = id;
                throw ex;
            }
            _internalObjects.erase(i);
            txHolder.commit();
            break;
        }
        catch(const DeadlockException&)
        {
            continue;
        }
        catch(const DatabaseException& ex)
        {
            halt(_communicator, ex);
        }
    }
}

Ice::ObjectProxySeq
Database::getInternalObjectsByType(const string& type)
{
    Ice::ObjectProxySeq proxies;

    ConnectionPtr connection = Freeze::createConnection(_communicator, _envName);
    IdentityObjectInfoDict internalObjects(connection, internalObjectsDbName);
    vector<ObjectInfo> infos = findByType(internalObjects, type);
    for(unsigned int i = 0; i < infos.size(); ++i)
    {
        proxies.push_back(infos[i].proxy);
    }
    return proxies;
}

void
Database::checkForAddition(const ApplicationHelper& app, const ConnectionPtr& connection)
{
    set<string> serverIds;
    set<string> adapterIds;
    set<Ice::Identity> objectIds;

    app.getIds(serverIds, adapterIds, objectIds);

    for_each(serverIds.begin(), serverIds.end(), objFunc(*this, &Database::checkServerForAddition));
    if(!adapterIds.empty())
    {
        StringAdapterInfoDict adapters(connection, adaptersDbName);
        for(set<string>::const_iterator p = adapterIds.begin(); p != adapterIds.end(); ++p)
        {
            checkAdapterForAddition(*p, adapters);
        }
    }
    if(!objectIds.empty())
    {
        IdentityObjectInfoDict objects(connection, objectsDbName);
        for(set<Ice::Identity>::const_iterator p = objectIds.begin(); p != objectIds.end(); ++p)
        {
            checkObjectForAddition(*p, objects);
        }
    }

    set<string> repGrps;
    set<string> adptRepGrps;
    app.getReplicaGroups(repGrps, adptRepGrps);
    for_each(adptRepGrps.begin(), adptRepGrps.end(), objFunc(*this, &Database::checkReplicaGroupExists));
}

void
Database::checkForUpdate(const ApplicationHelper& origApp,
                         const ApplicationHelper& newApp,
                         const ConnectionPtr& connection)
{
    set<string> oldSvrs, newSvrs;
    set<string> oldAdpts, newAdpts;
    set<Ice::Identity> oldObjs, newObjs;

    origApp.getIds(oldSvrs, oldAdpts, oldObjs);
    newApp.getIds(newSvrs, newAdpts, newObjs);

    Ice::StringSeq addedSvrs;
    set_difference(newSvrs.begin(), newSvrs.end(), oldSvrs.begin(), oldSvrs.end(), back_inserter(addedSvrs));
    for_each(addedSvrs.begin(), addedSvrs.end(), objFunc(*this, &Database::checkServerForAddition));

    Ice::StringSeq addedAdpts;
    set_difference(newAdpts.begin(), newAdpts.end(), oldAdpts.begin(), oldAdpts.end(), back_inserter(addedAdpts));
    if(!addedAdpts.empty())
    {
        StringAdapterInfoDict adapters(connection, adaptersDbName);
        for(Ice::StringSeq::const_iterator p = addedAdpts.begin(); p != addedAdpts.end(); ++p)
        {
            checkAdapterForAddition(*p, adapters);
        }
    }

    vector<Ice::Identity> addedObjs;
    set_difference(newObjs.begin(), newObjs.end(), oldObjs.begin(), oldObjs.end(), back_inserter(addedObjs));
    if(!addedObjs.empty())
    {
        IdentityObjectInfoDict objects(connection, objectsDbName);
        for(vector<Ice::Identity>::const_iterator p = addedObjs.begin(); p != addedObjs.end(); ++p)
        {
            checkObjectForAddition(*p, objects);
        }
    }

    set<string> oldRepGrps, newRepGrps;
    set<string> oldAdptRepGrps, newAdptRepGrps;
    origApp.getReplicaGroups(oldRepGrps, oldAdptRepGrps);
    newApp.getReplicaGroups(newRepGrps, newAdptRepGrps);

    set<string> rmRepGrps;
    set_difference(oldRepGrps.begin(), oldRepGrps.end(), newRepGrps.begin(),newRepGrps.end(), set_inserter(rmRepGrps));
    for_each(rmRepGrps.begin(), rmRepGrps.end(), objFunc(*this, &Database::checkReplicaGroupForRemove));

    set<string> addedAdptRepGrps;
    set_difference(newAdptRepGrps.begin(),newAdptRepGrps.end(), oldAdptRepGrps.begin(), oldAdptRepGrps.end(),
                   set_inserter(addedAdptRepGrps));
    for_each(addedAdptRepGrps.begin(), addedAdptRepGrps.end(), objFunc(*this, &Database::checkReplicaGroupExists));

    vector<string> invalidAdptRepGrps;
    set_intersection(rmRepGrps.begin(), rmRepGrps.end(), newAdptRepGrps.begin(), newAdptRepGrps.end(),
                     back_inserter(invalidAdptRepGrps));
    if(!invalidAdptRepGrps.empty())
    {
        DeploymentException ex;
        ex.reason = "couldn't find replica group `" + invalidAdptRepGrps.front() + "'";
        throw ex;
    }
}

void
Database::checkForRemove(const ApplicationHelper& app)
{
    set<string> replicaGroups;
    set<string> adapterReplicaGroups;
    app.getReplicaGroups(replicaGroups, adapterReplicaGroups);
    for_each(replicaGroups.begin(), replicaGroups.end(), objFunc(*this, &Database::checkReplicaGroupForRemove));
}

void
Database::checkServerForAddition(const string& id)
{
    if(_serverCache.has(id))
    {
        DeploymentException ex;
        ex.reason = "server `" + id + "' is already registered";
        throw ex;
    }
}

void
Database::checkAdapterForAddition(const string& id, const StringAdapterInfoDict& adapters)
{
    bool found = false;
    if(_adapterCache.has(id))
    {
        found = true;
    }
    else
    {
        StringAdapterInfoDict::const_iterator i = adapters.find(id);
        if(i != adapters.end())
        {
            found = true;
        }
        else
        {
            if(!findByReplicaGroupId(adapters, id).empty())
            {
                found = true;
            }
        }
    }

    if(found)
    {
        DeploymentException ex;
        ex.reason = "adapter `" + id + "' is already registered";
        throw ex;
    }
}

void
Database::checkObjectForAddition(const Ice::Identity& objectId, const IdentityObjectInfoDict& objects)
{
    bool found = false;
    if(_objectCache.has(objectId) || _allocatableObjectCache.has(objectId))
    {
        found = true;
    }
    else
    {
        IdentityObjectInfoDict::const_iterator i = objects.find(objectId);
        if(i != objects.end())
        {
            found = true;
        }
    }

    if(found)
    {
        DeploymentException ex;
        ex.reason = "object `" + _communicator->identityToString(objectId) + "' is already registered";
        throw ex;
    }
}

void
Database::checkReplicaGroupExists(const string& replicaGroup)
{
    ReplicaGroupEntryPtr entry;
    try
    {
        entry = ReplicaGroupEntryPtr::dynamicCast(_adapterCache.get(replicaGroup));
    }
    catch(const AdapterNotExistException&)
    {
    }

    if(!entry)
    {
        DeploymentException ex;
        ex.reason = "couldn't find replica group `" + replicaGroup + "'";
        throw ex;
    }
}

void
Database::checkReplicaGroupForRemove(const string& replicaGroup)
{
    ReplicaGroupEntryPtr entry;
    try
    {
        entry = ReplicaGroupEntryPtr::dynamicCast(_adapterCache.get(replicaGroup));
    }
    catch(const AdapterNotExistException&)
    {
    }

    if(!entry)
    {
        //
        // This would indicate an inconsistency with the cache and
        // database. We don't print an error, it will be printed
        // when the application is actually removed.
        //
        return;
    }

    if(entry->hasAdaptersFromOtherApplications())
    {
        DeploymentException ex;
        ex.reason = "couldn't remove application because the replica group `" + replicaGroup +
            "' is used by object adapters from other applications.";
        throw ex;
    }
}

void
Database::load(const ApplicationHelper& app, ServerEntrySeq& entries, const string& uuid, int revision)
{
    const NodeDescriptorDict& nodes = app.getInstance().nodes;
    const string application = app.getInstance().name;
    for(NodeDescriptorDict::const_iterator n = nodes.begin(); n != nodes.end(); ++n)
    {
        _nodeCache.get(n->first, true)->addDescriptor(application, n->second);
    }

    const ReplicaGroupDescriptorSeq& adpts = app.getInstance().replicaGroups;
    for(ReplicaGroupDescriptorSeq::const_iterator r = adpts.begin(); r != adpts.end(); ++r)
    {
        assert(!r->id.empty());
        _adapterCache.addReplicaGroup(*r, application);
        for(ObjectDescriptorSeq::const_iterator o = r->objects.begin(); o != r->objects.end(); ++o)
        {
            _objectCache.add(toObjectInfo(_communicator, *o, r->id), application);
        }
    }

    map<string, ServerInfo> servers = app.getServerInfos(uuid, revision);
    for(map<string, ServerInfo>::const_iterator p = servers.begin(); p != servers.end(); ++p)
    {
        entries.push_back(_serverCache.add(p->second));
    }
}

void
Database::unload(const ApplicationHelper& app, ServerEntrySeq& entries)
{
    map<string, ServerInfo> servers = app.getServerInfos("", 0);
    for(map<string, ServerInfo>::const_iterator p = servers.begin(); p != servers.end(); ++p)
    {
        entries.push_back(_serverCache.remove(p->first, false));
    }

    const ReplicaGroupDescriptorSeq& adpts = app.getInstance().replicaGroups;
    for(ReplicaGroupDescriptorSeq::const_iterator r = adpts.begin(); r != adpts.end(); ++r)
    {
        for(ObjectDescriptorSeq::const_iterator o = r->objects.begin(); o != r->objects.end(); ++o)
        {
            _objectCache.remove(o->id);
        }
        _adapterCache.removeReplicaGroup(r->id);
    }

    const NodeDescriptorDict& nodes = app.getInstance().nodes;
    const string application = app.getInstance().name;
    for(NodeDescriptorDict::const_iterator n = nodes.begin(); n != nodes.end(); ++n)
    {
        _nodeCache.get(n->first)->removeDescriptor(application);
    }
}

void
Database::reload(const ApplicationHelper& oldApp,
                 const ApplicationHelper& newApp,
                 ServerEntrySeq& entries,
                 const string& uuid,
                 int revision,
                 bool noRestart)
{
    const string application = oldApp.getInstance().name;

    //
    // Remove destroyed servers.
    //
    map<string, ServerInfo> oldServers = oldApp.getServerInfos(uuid, revision);
    map<string, ServerInfo> newServers = newApp.getServerInfos(uuid, revision);
    vector<pair<bool, ServerInfo> > load;
    for(map<string, ServerInfo>::const_iterator p = newServers.begin(); p != newServers.end(); ++p)
    {
        map<string, ServerInfo>::const_iterator q = oldServers.find(p->first);
        if(q == oldServers.end())
        {
            load.push_back(make_pair(false, p->second));
        }
        else if(isServerUpdated(p->second, q->second))
        {
            _serverCache.preUpdate(p->second, noRestart);
            load.push_back(make_pair(true, p->second));
        }
        else
        {
            ServerEntryPtr server = _serverCache.get(p->first);
            server->update(q->second, noRestart); // Just update the server revision on the node.
            entries.push_back(server);
        }
    }
    for(map<string, ServerInfo>::const_iterator p = oldServers.begin(); p != oldServers.end(); ++p)
    {
        map<string, ServerInfo>::const_iterator q = newServers.find(p->first);
        if(q == newServers.end())
        {
            entries.push_back(_serverCache.remove(p->first, noRestart));
        }
    }

    //
    // Remove destroyed replica groups.
    //
    const ReplicaGroupDescriptorSeq& oldAdpts = oldApp.getInstance().replicaGroups;
    const ReplicaGroupDescriptorSeq& newAdpts = newApp.getInstance().replicaGroups;
    for(ReplicaGroupDescriptorSeq::const_iterator r = oldAdpts.begin(); r != oldAdpts.end(); ++r)
    {
        ReplicaGroupDescriptorSeq::const_iterator t;
        for(t = newAdpts.begin(); t != newAdpts.end(); ++t)
        {
            if(t->id == r->id)
            {
                break;
            }
        }
        for(ObjectDescriptorSeq::const_iterator o = r->objects.begin(); o != r->objects.end(); ++o)
        {
            _objectCache.remove(o->id);
        }
        if(t == newAdpts.end())
        {
            _adapterCache.removeReplicaGroup(r->id);
        }
    }

    //
    // Remove all the node descriptors.
    //
    const NodeDescriptorDict& oldNodes = oldApp.getInstance().nodes;
    for(NodeDescriptorDict::const_iterator n = oldNodes.begin(); n != oldNodes.end(); ++n)
    {
        _nodeCache.get(n->first)->removeDescriptor(application);
    }

    //
    // Add back node descriptors.
    //
    const NodeDescriptorDict& newNodes = newApp.getInstance().nodes;
    for(NodeDescriptorDict::const_iterator n = newNodes.begin(); n != newNodes.end(); ++n)
    {
        _nodeCache.get(n->first, true)->addDescriptor(application, n->second);
    }

    //
    // Add back replica groups.
    //
    for(ReplicaGroupDescriptorSeq::const_iterator r = newAdpts.begin(); r != newAdpts.end(); ++r)
    {
        try
        {
            ReplicaGroupEntryPtr entry = ReplicaGroupEntryPtr::dynamicCast(_adapterCache.get(r->id));
            assert(entry);
            entry->update(application, r->loadBalancing, r->filter);
        }
        catch(const AdapterNotExistException&)
        {
            _adapterCache.addReplicaGroup(*r, application);
        }

        for(ObjectDescriptorSeq::const_iterator o = r->objects.begin(); o != r->objects.end(); ++o)
        {
            _objectCache.add(toObjectInfo(_communicator, *o, r->id), application);
        }
    }

    //
    // Add back servers.
    //
    for(vector<pair<bool, ServerInfo> >::const_iterator q = load.begin(); q != load.end(); ++q)
    {
        if(q->first) // Update
        {
            entries.push_back(_serverCache.postUpdate(q->second, noRestart));
        }
        else
        {
            entries.push_back(_serverCache.add(q->second));
        }
    }
}

Ice::Long
Database::saveApplication(const ApplicationInfo& info, const ConnectionPtr& connection, Ice::Long dbSerial)
{
    assert(dbSerial != 0 || _master);
    for(;;)
    {
        try
        {
            StringApplicationInfoDict applications(connection, applicationsDbName);
            TransactionHolder txHolder(connection);
            applications.put(StringApplicationInfoDict::value_type(info.descriptor.name, info));
            dbSerial = updateSerial(connection, applicationsDbName, dbSerial);
            txHolder.commit();
            break;
        }
        catch(const DeadlockException&)
        {
            continue;
        }
        catch(const DatabaseException& ex)
        {
            halt(_communicator, ex);
        }
    }
    return dbSerial;
}

Ice::Long
Database::removeApplication(const string& name, const ConnectionPtr& connection, Ice::Long dbSerial)
{
    assert(dbSerial != 0 || _master);
    for(;;)
    {
        try
        {
            StringApplicationInfoDict applications(connection, applicationsDbName);
            TransactionHolder txHolder(connection);
            applications.erase(name);
            dbSerial = updateSerial(connection, applicationsDbName, dbSerial);
            txHolder.commit();
            break;
        }
        catch(const DeadlockException&)
        {
            continue;
        }
        catch(const DatabaseException& ex)
        {
            halt(_communicator, ex);
        }
    }
    return dbSerial;
}

void
Database::checkUpdate(const ApplicationHelper& oldApp,
                      const ApplicationHelper& newApp,
                      const string& uuid,
                      int revision,
                      bool noRestart)
{
    const string application = oldApp.getInstance().name;

    map<string, ServerInfo> oldServers = oldApp.getServerInfos(uuid, revision);
    map<string, ServerInfo> newServers = newApp.getServerInfos(uuid, revision + 1);

    map<string, ServerInfo>::const_iterator p;
    vector<string> servers;
    vector<string> reasons;
    vector<CheckUpdateResultPtr> results;
    set<string> unreachableNodes;

    if(noRestart)
    {
        for(p = oldServers.begin(); p != oldServers.end(); ++p)
        {
            map<string, ServerInfo>::const_iterator q = newServers.find(p->first);
            if(q == newServers.end())
            {
                try
                {
                    ServerInfo info = p->second;
                    info.descriptor = 0; // Clear the descriptor to indicate removal.
                    CheckUpdateResultPtr result = _serverCache.get(p->first)->checkUpdate(info, true);
                    if(result)
                    {
                        results.push_back(result);
                    }
                }
                catch(const NodeUnreachableException& ex)
                {
                    unreachableNodes.insert(ex.name);
                }
                catch(const DeploymentException& ex)
                {
                    servers.push_back(p->first);
                    reasons.push_back(ex.reason);
                }
            }
        }
    }

    for(p = newServers.begin(); p != newServers.end(); ++p)
    {
        map<string, ServerInfo>::const_iterator q = oldServers.find(p->first);
        if(q != oldServers.end() && isServerUpdated(p->second, q->second))
        {
            if(noRestart &&
               p->second.node == q->second.node &&
               isServerUpdated(p->second, q->second, true)) // Ignore properties
            {
                //
                // The updates are not only property updates and noRestart is required, no
                // need to check the server update on the node, we know already it requires
                // a restart.
                //
                servers.push_back(p->first);
                reasons.push_back("update requires the server `" + p->first + "' to be stopped");
            }
            else
            {
                //
                // Ask the node to check the server update.
                //
                try
                {
                    CheckUpdateResultPtr result = _serverCache.get(p->first)->checkUpdate(p->second, noRestart);
                    if(result)
                    {
                        results.push_back(result);
                    }
                }
                catch(const NodeUnreachableException& ex)
                {
                    unreachableNodes.insert(ex.name);
                }
                catch(const DeploymentException& ex)
                {
                    servers.push_back(p->first);
                    reasons.push_back(ex.reason);
                }
            }
        }
    }

    for(vector<CheckUpdateResultPtr>::const_iterator q = results.begin(); q != results.end(); ++q)
    {
        try
        {
            (*q)->getResult();
        }
        catch(const NodeUnreachableException& ex)
        {
            unreachableNodes.insert(ex.name);
        }
        catch(const DeploymentException& ex)
        {
            servers.push_back((*q)->getServer());
            reasons.push_back(ex.reason);
        }
    }

    if(noRestart)
    {
        if(!servers.empty() || !unreachableNodes.empty())
        {
            if(_traceLevels->application > 0)
            {
                Ice::Trace out(_traceLevels->logger, _traceLevels->applicationCat);
                out << "check for application `" << application << "' update failed:";
                if(!unreachableNodes.empty())
                {
#if defined(__SUNPRO_CC) && defined(_RWSTD_NO_MEMBER_TEMPLATES)
                    Ice::StringSeq nodes;
                    for(set<string>::const_iterator p = unreachableNodes.begin(); p != unreachableNodes.end(); ++p)
                    {
                        nodes.push_back(*p);
                    }
#else
                    Ice::StringSeq nodes(unreachableNodes.begin(), unreachableNodes.end());
#endif
                    if(nodes.size() == 1)
                    {
                        out << "\nthe node `" << nodes[0] << "' is down";
                    }
                    else
                    {
                        out << "\nthe nodes `" << toString(nodes, ", ") << "' are down";
                    }
                }
                if(!reasons.empty())
                {
                    for(vector<string>::const_iterator p = reasons.begin(); p != reasons.end(); ++p)
                    {
                        out << "\n" << *p;
                    }
                }
            }

            ostringstream os;
            os << "check for application `" << application << "' update failed:";
            if(!servers.empty())
            {
                if(servers.size() == 1)
                {
                    os << "\nthe server `" << servers[0] << "' would need to be stopped";
                }
                else
                {
                    os << "\nthe servers `" << toString(servers, ", ") << "' would need to be stopped";
                }
            }
            if(!unreachableNodes.empty())
            {
#if defined(__SUNPRO_CC) && defined(_RWSTD_NO_MEMBER_TEMPLATES)
                Ice::StringSeq nodes;
                for(set<string>::const_iterator p = unreachableNodes.begin(); p != unreachableNodes.end(); ++p)
                {
                    nodes.push_back(*p);
                }
#else
                Ice::StringSeq nodes(unreachableNodes.begin(), unreachableNodes.end());
#endif
                if(nodes.size() == 1)
                {
                    os << "\nthe node `" << nodes[0] << "' is down";
                }
                else
                {
                    os << "\nthe nodes `" << toString(nodes, ", ") << "' are down";
                }
            }
            throw DeploymentException(os.str());
        }
    }
    else if(!reasons.empty())
    {
        ostringstream os;
        os << "check for application `" << application << "' update failed:";
        for(vector<string>::const_iterator p = reasons.begin(); p != reasons.end(); ++p)
        {
            os << "\n" << *p;
        }
        throw DeploymentException(os.str());
    }
}

void
Database::finishApplicationUpdate(const ApplicationUpdateInfo& update,
                                  const ApplicationInfo& oldApp,
                                  const ApplicationHelper& previous,
                                  const ApplicationHelper& helper,
                                  AdminSessionI* /*session*/,
                                  bool noRestart,
                                  Ice::Long dbSerial)
{
    const ApplicationDescriptor& newDesc = helper.getDefinition();
    ConnectionPtr connection = Freeze::createConnection(_communicator, _envName);

    ServerEntrySeq entries;
    int serial = 0;
    try
    {
        if(_master)
        {
            checkUpdate(previous, helper, oldApp.uuid, oldApp.revision, noRestart);
        }

        Lock sync(*this);
        checkForUpdate(previous, helper, connection);
        reload(previous, helper, entries, oldApp.uuid, oldApp.revision + 1, noRestart);

        for_each(entries.begin(), entries.end(), IceUtil::voidMemFun(&ServerEntry::sync));

        ApplicationInfo info = oldApp;
        info.updateTime = update.updateTime;
        info.updateUser = update.updateUser;
        info.revision = update.revision;
        info.descriptor = newDesc;
        dbSerial = saveApplication(info, connection, dbSerial);

        serial = _applicationObserverTopic->applicationUpdated(dbSerial, update);
    }
    catch(const DeploymentException&)
    {
        finishUpdating(update.descriptor.name);
        throw;
    }

    _applicationObserverTopic->waitForSyncedSubscribers(serial); // Wait for replicas to be updated.

    //
    // Mark the application as updated. All the replicas received the update so it's now safe
    // for the nodes to start servers.
    //
    {
        Lock sync(*this);
        vector<UpdateInfo>::iterator p = find(_updating.begin(), _updating.end(), update.descriptor.name);
        assert(p != _updating.end());
        p->markUpdated();
    }

    if(_master)
    {
        try
        {
            for(ServerEntrySeq::const_iterator p = entries.begin(); p != entries.end(); ++p)
            {
                try
                {
                    (*p)->waitForSync();
                }
                catch(const NodeUnreachableException&)
                {
                    // Ignore.
                }
            }
        }
        catch(const DeploymentException& ex)
        {
            ApplicationUpdateInfo newUpdate;
            {
                Lock sync(*this);
                entries.clear();
                ApplicationHelper previous(_communicator, newDesc);
                ApplicationHelper helper(_communicator, oldApp.descriptor);

                ApplicationInfo info = oldApp;
                info.revision = update.revision + 1;
                dbSerial = saveApplication(info, connection);
                reload(previous, helper, entries, info.uuid, info.revision, noRestart);

                newUpdate.updateTime = IceUtil::Time::now().toMilliSeconds();
                newUpdate.updateUser = _lockUserId;
                newUpdate.revision = info.revision;
                newUpdate.descriptor = helper.diff(previous);

                vector<UpdateInfo>::iterator p = find(_updating.begin(), _updating.end(), update.descriptor.name);
                assert(p != _updating.end());
                p->unmarkUpdated();

                for_each(entries.begin(), entries.end(), IceUtil::voidMemFun(&ServerEntry::sync));

                serial = _applicationObserverTopic->applicationUpdated(dbSerial, newUpdate);
            }
            _applicationObserverTopic->waitForSyncedSubscribers(serial); // Wait for subscriber to be updated.
            for_each(entries.begin(), entries.end(), IceUtil::voidMemFun(&ServerEntry::waitForSyncNoThrow));
            finishUpdating(newDesc.name);
            throw ex;
        }
    }

    if(_traceLevels->application > 0)
    {
        Ice::Trace out(_traceLevels->logger, _traceLevels->applicationCat);
        out << "updated application `" << update.descriptor.name << "' (serial = `" << dbSerial << "')";
    }
    finishUpdating(update.descriptor.name);
}

void
Database::waitForUpdate(const string& name)
{
    while(find(_updating.begin(), _updating.end(), name) != _updating.end())
    {
        wait();
    }
}

void
Database::startUpdating(const string& name, const string& uuid, int revision)
{
    // Must be called within the synchronization.
    assert(find(_updating.begin(), _updating.end(), name) == _updating.end());
    _updating.push_back(UpdateInfo(name, uuid, revision));
}

void
Database::finishUpdating(const string& name)
{
    Lock sync(*this);

    vector<UpdateInfo>::iterator p = find(_updating.begin(), _updating.end(), name);
    assert(p != _updating.end());
    p->markUpdated();
    _updating.erase(p);
    notifyAll();
}
