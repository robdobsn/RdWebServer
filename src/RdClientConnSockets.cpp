/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RdWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef ESP8266

#include "RdClientConnSockets.h"
#include "RdWebInterface.h"
#include <Logger.h>
#include "lwip/api.h"
#include "lwip/sockets.h"

static const char *MODULE_PREFIX = "RdClientConn8266";

RdClientConnSockets::RdClientConnSockets(int client)
{
    _client = client;
    _pDataBuf = nullptr;
}

RdClientConnSockets::~RdClientConnSockets()
{
    // shutdown(_client, 0);
    close(_client);
    delete _pDataBuf;
}

void RdClientConnSockets::setup(bool blocking)
{
    // Check for non blocking
    if (!blocking)
    {
        // Set non-blocking socket
        int flags = fcntl(_client, F_GETFL, 0);
        if (flags != -1)
        flags = flags | O_NONBLOCK;
        fcntl(_client, F_SETFL, flags);
    }
}

bool RdClientConnSockets::write(const uint8_t* pBuf, uint32_t bufLen)
{
    // Check active
    if (!isActive())
    {
        LOG_W(MODULE_PREFIX, "write conn %d isActive FALSE", getClientId());
        return false;
    }

    // Write using socket
    int rslt = send(_client, pBuf, bufLen, 0);
    if (rslt < 0)
    {
        LOG_W(MODULE_PREFIX, "write failed errno %d conn %d", errno, getClientId());
        return false;
    }
    return true;
}

uint8_t* RdClientConnSockets::getDataStart(uint32_t& dataLen, bool& closeRequired)
{
    // End any current data operation
    getDataEnd();

    // Create data buffer
    _pDataBuf = new uint8_t[WEB_CONN_MAX_RX_BUFFER];
    if (!_pDataBuf)
    {
        LOG_E(MODULE_PREFIX, "service failed to alloc %d", getClientId());
        return nullptr;
    }

    // Check for data
    int32_t bufLen = recv(_client, _pDataBuf, WEB_CONN_MAX_RX_BUFFER, 0);
    if (bufLen < 0)
    {
        switch(errno)
        {
            case EWOULDBLOCK:
                bufLen = 0;
                break;
            default:
                LOG_W(MODULE_PREFIX, "service read error %d", errno);
                closeRequired = true;
                break;
        }
        getDataEnd();
        return nullptr;
    }
    else if (bufLen == 0)
    {
        LOG_W(MODULE_PREFIX, "service read conn closed %d", errno);
        closeRequired = true;
        getDataEnd();
        return nullptr;
    }

    // Return received data
    closeRequired = false;
    dataLen = bufLen;
    return _pDataBuf;
}

void RdClientConnSockets::getDataEnd()
{
    // Delete buffer
    if (_pDataBuf)
        delete _pDataBuf;
    _pDataBuf = nullptr;
}

#endif
