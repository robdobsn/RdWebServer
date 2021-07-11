/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RdWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "lwip/api.h"

class RdClientConnBase
{
public:

    // Virtual destructor
    virtual ~RdClientConnBase()
    {
    }

    // Connection is active
    virtual bool isActive()
    {
        return true;
    }

    // Current state
    virtual const char* getStateStr()
    {
        return "none";
    }

    // Client ID
    virtual uint32_t getClientId()
    {
        return 0;
    }

    // Write
    virtual bool write(const uint8_t* pBuf, uint32_t bufLen);

    // Setup
    virtual void setup(bool blocking);

    // Data access
    virtual uint8_t* getDataStart(uint32_t& dataLen, bool& closeRequired);
    virtual void getDataEnd();
};
