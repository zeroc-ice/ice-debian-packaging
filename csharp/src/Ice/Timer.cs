// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

//
// NOTE: We don't use C# timers, the API is quite a bit different from
// the C++ & Java timers and it's not clear what is the cost of
// scheduling and cancelling timers.
//
    
namespace IceInternal
{
    using System;
    using System.Diagnostics;
    using System.Threading;
    using System.Collections;
    using System.Collections.Generic;

    public interface TimerTask
    {
        void runTimerTask();
    }

    public sealed class Timer
    {
        public void destroy()
        {
            lock(this)
            {
                if(_instance == null)
                {
                    return;
                }

                _instance = null;
                System.Threading.Monitor.Pulse(this);
            
                _tokens.Clear();
                _tasks.Clear();
            }

            _thread.Join();
        }

        public void schedule(TimerTask task, long delay)
        {
            lock(this)
            {
                if(_instance == null)
                {
                    throw new Ice.CommunicatorDestroyedException();
                }

                Token token = new Token(Time.currentMonotonicTimeMillis() + delay, ++_tokenId, 0, task);

                try
                {
                    _tasks.Add(task, token);
#if SILVERLIGHT
                    int index = _tokens.BinarySearch(token);
                    Debug.Assert(index < 0);
                    if(index < 0)
                    {
                        _tokens.Insert(~index, token);
                    }
#else
                    _tokens.Add(token, null);
#endif
                }
                catch(System.ArgumentException)
                {
                    Debug.Assert(false);
                }
            
                if(token.scheduledTime < _wakeUpTime)
                {
                    System.Threading.Monitor.Pulse(this);
                }
            }
        }

        public void scheduleRepeated(TimerTask task, long period)
        {
            lock(this)
            {
                if(_instance == null)
                {
                    throw new Ice.CommunicatorDestroyedException();
                }

                Token token = new Token(Time.currentMonotonicTimeMillis() + period, ++_tokenId, period, task);

                try
                {
                    _tasks.Add(task, token);
#if SILVERLIGHT
                    int index = _tokens.BinarySearch(token);
                    Debug.Assert(index < 0);
                    if(index < 0)
                    {
                        _tokens.Insert(~index, token);
                    }
#else
                    _tokens.Add(token, null);
#endif
                }
                catch(System.ArgumentException)
                {
                    Debug.Assert(false);
                }

                if(token.scheduledTime < _wakeUpTime)
                {
                    System.Threading.Monitor.Pulse(this);
                }
            }
        } 
        
        public bool cancel(TimerTask task)
        {
            lock(this)
            {
                if(_instance == null)
                {
                    return false;
                }

                Token token;
                if(!_tasks.TryGetValue(task, out token))
                {
                    return false;
                }
                _tasks.Remove(task);
                _tokens.Remove(token);
                return true;
            }
        }

        //
        // Only for use by Instance.
        //
#if !SILVERLIGHT
        internal Timer(IceInternal.Instance instance, ThreadPriority priority)
        {
            init(instance, priority, true);
        }
#endif
        
        internal Timer(IceInternal.Instance instance)
        {
#if !SILVERLIGHT
            init(instance, ThreadPriority.Normal, false);
#else
            init(instance);
#endif
        }

#if !SILVERLIGHT
        internal void init(IceInternal.Instance instance, ThreadPriority priority,  bool hasPriority)
#else
        internal void init(IceInternal.Instance instance)
#endif
        {
            _instance = instance;

            string threadName = _instance.initializationData().properties.getProperty("Ice.ProgramName");
            if(threadName.Length > 0)
            {
                threadName += "-";
            }

            _thread = new Thread(new ThreadStart(Run));
            _thread.IsBackground = true;
            _thread.Name = threadName + "Ice.Timer";
#if !SILVERLIGHT
            if(hasPriority)
            {
                _thread.Priority = priority;
            }
#endif
            _thread.Start();
        }

        internal void updateObserver(Ice.Instrumentation.CommunicatorObserver obsv)
        {
            lock(this)
            {
                Debug.Assert(obsv != null);
                _observer = obsv.getThreadObserver("Communicator", 
                                                   _thread.Name,
                                                   Ice.Instrumentation.ThreadState.ThreadStateIdle,
                                                   _observer);
                if(_observer != null)
                {
                    _observer.attach();
                }
            }
        }

        public void Run()
        {
            Token token = null;
            while(true)
            {
                lock(this)
                {
                    if(_instance != null)
                    {
                        //
                        // If the task we just ran is a repeated task, schedule it
                        // again for execution if it wasn't canceled.
                        //
                        if(token != null && token.delay > 0)
                        {
                            if(_tasks.ContainsKey(token.task))
                            {
                                token.scheduledTime = Time.currentMonotonicTimeMillis() + token.delay;
#if SILVERLIGHT
                                int index = _tokens.BinarySearch(token);
                                Debug.Assert(index < 0);
                                if(index < 0)
                                {
                                    _tokens.Insert(~index, token);
                                }
#else
                                _tokens.Add(token, null);
#endif
                            }
                        }
                    }
                    token = null;

                    if(_instance == null)
                    {
                        break;
                    }

                    if(_tokens.Count == 0)
                    {
                        _wakeUpTime = System.Int64.MaxValue;
                        System.Threading.Monitor.Wait(this);
                    }
            
                    if(_instance == null)
                    {
                        break;
                    }
                
                    while(_tokens.Count > 0 && _instance != null)
                    {
                        long now = Time.currentMonotonicTimeMillis();

                        Token first = null;
#if SILVERLIGHT
                        foreach(Token t in _tokens)
#else
                        foreach(Token t in _tokens.Keys)
#endif
                        {
                            first = t;
                            break;
                        }
                        Debug.Assert(first != null);

                        if(first.scheduledTime <= now)
                        {
                            _tokens.Remove(first);
                            token = first;
                            if(token.delay == 0)
                            {
                                _tasks.Remove(token.task);
                            }
                            break;
                        }
                    
                        _wakeUpTime = first.scheduledTime;
                        System.Threading.Monitor.Wait(this, (int)(first.scheduledTime - now));
                    }
                
                    if(_instance == null)
                    {
                        break;
                    }
                }

                if(token != null)
                {
                    try
                    {
                        Ice.Instrumentation.ThreadObserver threadObserver = _observer;
                        if(threadObserver != null)
                        {
                            threadObserver.stateChanged(Ice.Instrumentation.ThreadState.ThreadStateIdle,
                                                        Ice.Instrumentation.ThreadState.ThreadStateInUseForOther);
                            try
                            {
                                token.task.runTimerTask();
                            }
                            finally
                            {
                                threadObserver.stateChanged(Ice.Instrumentation.ThreadState.ThreadStateInUseForOther,
                                                            Ice.Instrumentation.ThreadState.ThreadStateIdle);
                            }
                        }
                        else
                        {
                            token.task.runTimerTask();
                        }
                    }
                    catch(System.Exception ex)
                    {
                        lock(this)
                        {
                            if(_instance != null)
                            {
                                string s = "unexpected exception from task run method in timer thread:\n" + ex;
                                _instance.initializationData().logger.error(s);
                            }
                        }
                    } 
                }
            }
        }

        private class Token : System.IComparable
        {
            public
            Token(long scheduledTime, int id, long delay, TimerTask task)
            {
                this.scheduledTime = scheduledTime;
                this.id = id;
                this.delay = delay;
                this.task = task;
            }

            public int CompareTo(object o)
            {
                //
                // Token are sorted by scheduled time and token id.
                //
                Token r = (Token)o;
                if(scheduledTime < r.scheduledTime)
                {
                    return -1;
                }
                else if(scheduledTime > r.scheduledTime)
                {
                    return 1;
                }

                if(id < r.id)
                {
                    return -1;
                }
                else if(id > r.id)
                {
                    return 1;
                }
            
                return 0;
            }

            public override bool Equals(object o)
            {
                if(object.ReferenceEquals(this, o))
                {
                    return true;
                }
                Token t = o as Token;
                return t == null ? false : CompareTo(t) == 0;
            }

            public override int GetHashCode()
            {
                int h = 5381;
                IceInternal.HashUtil.hashAdd(ref h, id);
                IceInternal.HashUtil.hashAdd(ref h, scheduledTime);
                return h;
            }

            public long scheduledTime;
            public int id; // Since we can't compare references, we need to use another id.
            public long delay;
            public TimerTask task;
        }

#if COMPACT
        private IDictionary<Token, object> _tokens = new SortedList<Token, object>();
#elif SILVERLIGHT
        private List<Token> _tokens = new List<Token>();
#else
        private IDictionary<Token, object> _tokens = new SortedDictionary<Token, object>();
#endif
        private IDictionary<TimerTask, Token> _tasks = new Dictionary<TimerTask, Token>();
        private Instance _instance;
        private long _wakeUpTime = System.Int64.MaxValue;
        private int _tokenId = 0;
        private Thread _thread;

        //
        // We use a volatile to avoid synchronization when reading
        // _observer. Reference assignement is atomic in Java so it
        // also doesn't need to be synchronized.
        // 
        private volatile Ice.Instrumentation.ThreadObserver _observer;
}

}
