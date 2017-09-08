// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

using System.Diagnostics;
using System.Globalization;
using System.Runtime.Serialization;

namespace IceInternal
{
    public class Ex
    {
        public static void throwUOE(string expectedType, string actualType)
        {
            throw new Ice.UnexpectedObjectException(
                        "expected element of type `" + expectedType + "' but received '" + actualType,
                        actualType, expectedType);
        }

        public static void throwMemoryLimitException(int requested, int maximum)
        {
            throw new Ice.MemoryLimitException("requested " + requested + " bytes, maximum allowed is " + maximum +
                                               " bytes (see Ice.MessageSizeMax)");
        }
    }
}

namespace Ice
{
    /// <summary>
    /// Base class for Ice exceptions.
    /// </summary>
#if !SILVERLIGHT
    [System.Serializable]
#endif
    public abstract class Exception : System.Exception, System.ICloneable
    {
        /// <summary>
        /// Creates and returns a copy of this exception.
        /// </summary>
        /// <returns>A copy of this exception.</returns>
        public object Clone()
        {
            return MemberwiseClone();
        }

        /// <summary>
        /// Creates a default-initialized exception.
        /// </summary>
        public Exception() {}

        /// <summary>
        /// Creates a default-initialized exception and sets the InnerException
        /// property to the passed exception.
        /// </summary>
        /// <param name="ex">The inner exception.</param>
        public Exception(System.Exception ex) : base("", ex) {}

#if !SILVERLIGHT
        /// <summary>
        /// Initializes a new instance of the exception with serialized data.
        /// </summary>
        /// <param name="info">Holds the serialized object data about the exception being thrown.</param>
        /// <param name="context">Contains contextual information about the source or destination.</param>
        protected Exception(SerializationInfo info, StreamingContext context) : base(info, context) {}
#endif

        /// <summary>
        /// Returns the name of this exception.
        /// </summary>
        /// <returns>The name of this exception.</returns>
        public abstract string ice_name();

        /// <summary>
        /// Returns a string representation of this exception, including
        /// any inner exceptions.
        /// </summary>
        /// <returns>The string representation of this exception.</returns>
        public override string ToString()
        {
            //
            // This prints the exception Java style. That is, the outermost
            // exception, "Caused by:" to the innermost exception. The
            // stack trace is not nicely indented as with Java, but
            // without string parsing (perhaps tokenize on "\n"), it
            // doesn't appear to be possible to reformat it.
            //
            System.IO.StringWriter sw = new System.IO.StringWriter(CultureInfo.CurrentCulture);
            IceUtilInternal.OutputBase op = new IceUtilInternal.OutputBase(sw);
            op.setUseTab(false);
            op.print(GetType().FullName);
            op.inc();
            IceInternal.ValueWriter.write(this, op);
            sw.Write("\n");
            sw.Write(StackTrace);

            System.Exception curr = InnerException;
            while(curr != null)
            {
                sw.Write("\nCaused by: ");
                sw.Write(curr.GetType().FullName);
                if(!(curr is Ice.Exception))
                {
                    sw.Write(": ");
                    sw.Write(curr.Message);
                }
                sw.Write("\n");
                sw.Write(curr.StackTrace);
                curr = curr.InnerException;
            }

            return sw.ToString();
        }
    }

    /// <summary>
    /// Base class for local exceptions.
    /// </summary>
#if !SILVERLIGHT
    [System.Serializable]
#endif
    public abstract class LocalException : Exception
    {
        /// <summary>
        /// Creates a default-initialized local exception.
        /// </summary>
        public LocalException() {}

        /// <summary>
        /// Creates a default-initialized local exception and sets the InnerException
        /// property to the passed exception.
        /// </summary>
        /// <param name="ex">The inner exception.</param>
        public LocalException(System.Exception ex) : base(ex) {}

#if !SILVERLIGHT
        /// <summary>
        /// Initializes a new instance of the exception with serialized data.
        /// </summary>
        /// <param name="info">Holds the serialized object data about the exception being thrown.</param>
        /// <param name="context">Contains contextual information about the source or destination.</param>
        protected LocalException(SerializationInfo info, StreamingContext context) : base(info, context) {}
#endif
    }

    /// <summary>
    /// Base class for Ice run-time exceptions.
    /// </summary>
#if !SILVERLIGHT
    [System.Serializable]
#endif
    public abstract class SystemException : Exception
    {
        /// <summary>
        /// Creates a default-initialized run-time exception.
        /// </summary>
        public SystemException() {}

        /// <summary>
        /// Creates a default-initialized run-time exception and sets the InnerException
        /// property to the passed exception.
        /// </summary>
        /// <param name="ex">The inner exception.</param>
        public SystemException(System.Exception ex) : base(ex) {}

#if !SILVERLIGHT
        /// <summary>
        /// Initializes a new instance of the exception with serialized data.
        /// </summary>
        /// <param name="info">Holds the serialized object data about the exception being thrown.</param>
        /// <param name="context">Contains contextual information about the source or destination.</param>
        protected SystemException(SerializationInfo info, StreamingContext context) : base(info, context) {}
#endif
    }

    /// <summary>
    /// Base class for Slice user exceptions.
    /// </summary>
#if !SILVERLIGHT
    [System.Serializable]
#endif
    public abstract class UserException : Exception
    {
        /// <summary>
        /// Creates a default-initialized user exception.
        /// </summary>
        public UserException() {}

        /// <summary>
        /// Creates a default-initialized user exception and sets the InnerException
        /// property to the passed exception.
        /// </summary>
        /// <param name="ex">The inner exception.</param>
        public UserException(System.Exception ex) : base(ex) {}

#if !SILVERLIGHT
        /// <summary>
        /// Initializes a new instance of the exception with serialized data.
        /// </summary>
        /// <param name="info">Holds the serialized object data about the exception being thrown.</param>
        /// <param name="context">Contains contextual information about the source or destination.</param>
        protected UserException(SerializationInfo info, StreamingContext context) : base(info, context) {}
#endif

        public virtual void write__(IceInternal.BasicStream os__)
        {
            os__.startWriteException(null);
            writeImpl__(os__);
            os__.endWriteException();
        }

        public virtual void read__(IceInternal.BasicStream is__)
        {
            is__.startReadException();
            readImpl__(is__);
            is__.endReadException(false);
        }

        public virtual void write__(OutputStream os__)
        {
            os__.startException(null);
            writeImpl__(os__);
            os__.endException();
        }

        public virtual void read__(InputStream is__)
        {
            is__.startException();
            readImpl__(is__);
            is__.endException(false);
        }

        public virtual bool usesClasses__()
        {
            return false;
        }

        protected abstract void writeImpl__(IceInternal.BasicStream os__);
        protected abstract void readImpl__(IceInternal.BasicStream is__);

        protected virtual void writeImpl__(OutputStream os__)
        {
            throw new MarshalException("exception was not generated with stream support");
        }

        protected virtual void readImpl__(InputStream is__)
        {
            throw new MarshalException("exception was not generated with stream support");
        }
    }
}

namespace IceInternal
{
    public class RetryException : System.Exception
    {
        public RetryException(Ice.LocalException ex)
        {
            _ex = ex;
        }

        public Ice.LocalException get()
        {
            return _ex;
        }

        private Ice.LocalException _ex;
    }
}
