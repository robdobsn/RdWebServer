/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RdWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <WString.h>
#include <list>
#include "RdWebInterface.h"

class RdWebRequest;
class RdWebRequestParams;
class RdWebRequestHeader;
class RdWebResponder;
class RdWebServerSettings;


class RdWebHandler
{
public:
    RdWebHandler()
    {
    }
    virtual ~RdWebHandler()
    {        
    }
    virtual const char* getName()
    {
        return "HandlerBase";
    }
    virtual RdWebResponder* getNewResponder(const RdWebRequestHeader& requestHeader, 
                const RdWebRequestParams& params, const RdWebServerSettings& webServerSettings,
                RdHttpStatusCode &statusCode)
    {
        return NULL;
    }
    virtual bool isFileHandler()
    {
        return false;
    }
    virtual bool isWebSocketHandler()
    {
        return false;
    }
    
private:
};

