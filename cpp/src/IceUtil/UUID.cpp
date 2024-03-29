//
// Copyright (c) ZeroC, Inc. All rights reserved.
//

#include <IceUtil/UUID.h>

// On Windows, we use Windows's RPC UUID generator.
// On other platforms, we use a high quality random number generator
// (/dev/random) to generate "version 4" UUIDs, as described in
// http://www.ietf.org/internet-drafts/draft-mealling-uuid-urn-00.txt

#include <IceUtil/Random.h>

#ifdef _WIN32
#   include <rpc.h>
#else
#   include <sys/types.h>
#   include <unistd.h>
#endif

using namespace std;

#ifndef _WIN32

namespace
{

char myPid[2];

}

namespace IceUtilInternal
{

//
// Initialize the pid.
//
class PidInitializer
{
public:

    PidInitializer()
    {
        pid_t p = getpid();
        myPid[0] = (p >> 8) & 0x7F;
        myPid[1] = static_cast<char>(p & 0xFF);
    }
};

PidInitializer pidInitializer;

};
#endif

namespace
{

// Helper char to hex functions
//
inline void halfByteToHex(unsigned char hb, char*& hexBuffer)
{
    if(hb < 10)
    {
        *hexBuffer++ = '0' + static_cast<char>(hb);
    }
    else
    {
        *hexBuffer++ = 'A' + static_cast<char>(hb - 10);
    }
}

inline void bytesToHex(unsigned char* bytes, size_t len, char*& hexBuffer)
{
    for(size_t i = 0; i < len; i++)
    {
        halfByteToHex((bytes[i] & 0xF0) >> 4, hexBuffer);
        halfByteToHex((bytes[i] & 0x0F), hexBuffer);
    }
}

}

string
IceUtil::generateUUID()
{
#if defined(_WIN32)

    UUID uuid;
    RPC_STATUS ret = UuidCreate(&uuid);
    if(ret != RPC_S_OK && ret != RPC_S_UUID_LOCAL_ONLY && ret != RPC_S_UUID_NO_ADDRESS)
    {
        throw SyscallException(__FILE__, __LINE__, GetLastError());
    }

    unsigned char* str;

    ret = UuidToString(&uuid, &str);
    if(ret != RPC_S_OK)
    {
        throw SyscallException(__FILE__, __LINE__, GetLastError());
    }
    string result = reinterpret_cast<char*>(str);

    RpcStringFree(&str);
    return result;

#else
    struct UUID
    {
        unsigned char timeLow[4];
        unsigned char timeMid[2];
        unsigned char timeHighAndVersion[2];
        unsigned char clockSeqHiAndReserved;
        unsigned char clockSeqLow;
        unsigned char node[6];
    };
    UUID uuid;

    assert(sizeof(UUID) == 16);

    //
    // Get a random sequence of bytes. Instead of using 122 random
    // bits that could be duplicated (because of a bug with some Linux
    // kernels and potentially other Unix platforms -- see comment in
    // Random.cpp), we replace the last 15 bits of all "random"
    // Randoms by the last 15 bits of the process id.
    //
    char* buffer = reinterpret_cast<char*>(&uuid);
    IceUtilInternal::generateRandom(buffer, sizeof(UUID));

    //
    // Adjust the bits that say "version 4" UUID
    //
    uuid.timeHighAndVersion[0] &= 0x0F;
    uuid.timeHighAndVersion[0] |= (4 << 4);
    uuid.clockSeqHiAndReserved &= 0x3F;
    uuid.clockSeqHiAndReserved |= 0x80;

    //
    // Replace the end of the node by myPid (15 bits)
    //
    uuid.node[4] = (uuid.node[4] & 0x80) | static_cast<unsigned char>(myPid[0]);
    uuid.node[5] = static_cast<unsigned char>(myPid[1]);

    //
    // Convert to a UUID string
    //
    char uuidString[16 * 2 + 4 + 1]; // 16 bytes, 4 '-' and a final '\0'
    char* uuidIndex = uuidString;
    bytesToHex(uuid.timeLow, sizeof(uuid.timeLow), uuidIndex);
    *uuidIndex++ = '-';
    bytesToHex(uuid.timeMid, sizeof(uuid.timeMid), uuidIndex);
    *uuidIndex++ = '-';
    bytesToHex(uuid.timeHighAndVersion, sizeof(uuid.timeHighAndVersion), uuidIndex);
    *uuidIndex++ = '-';
    bytesToHex(&uuid.clockSeqHiAndReserved, sizeof(uuid.clockSeqHiAndReserved), uuidIndex);
    bytesToHex(&uuid.clockSeqLow, sizeof(uuid.clockSeqLow), uuidIndex);
    *uuidIndex++ = '-';
    bytesToHex(uuid.node, sizeof(uuid.node), uuidIndex);
    *uuidIndex = '\0';

    return uuidString;

#endif
}
