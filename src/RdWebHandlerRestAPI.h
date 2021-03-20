/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RdWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RdWebHandler.h"
#include <Logger.h>
#include <RestAPIEndpointManager.h>
#include <RdWebRequestHeader.h>
#include <RdWebResponderRestAPI.h>

// #define DEBUG_WEB_HANDLER_REST_API

class RdWebRequest;

class RdWebHandlerRestAPI : public RdWebHandler
{
public:
    RdWebHandlerRestAPI(RestAPIEndpointManager* pEndpointManager, const String& restAPIPrefix)
    {
        _pEndpointManager = pEndpointManager;
        _restAPIPrefix = restAPIPrefix;
        if (!_restAPIPrefix.startsWith("/"))
            _restAPIPrefix = "/" + _restAPIPrefix;
    }
    virtual ~RdWebHandlerRestAPI()
    {
    }
    virtual const char* getName() override
    {
        return "HandlerRESTAPI";
    }
    virtual RdWebResponder* getNewResponder(const RdWebRequestHeader& requestHeader, 
            const RdWebRequestParams& params, const RdWebServerSettings& webServerSettings) override final
    {
        if (!_pEndpointManager)
        {
#ifdef DEBUG_WEB_HANDLER_REST_API
            LOG_W("WebHandlerRestAPI", "getNewResponder no EndpointManager %s", requestHeader.URL.c_str());
#endif
            return NULL;
        }

        // Check for API prefix
        if (!requestHeader.URL.startsWith(_restAPIPrefix))
        {
#ifdef DEBUG_WEB_HANDLER_REST_API
            LOG_W("WebHandlerRestAPI", "getNewResponder no match with %s for %s", 
                        _restAPIPrefix.c_str(), requestHeader.URL.c_str());
#endif
            return NULL;
        }

        // Remove prefix on test string
        String reqStr = requestHeader.URL.substring(_restAPIPrefix.length());
        RestAPIEndpointDef* pEndpointDef = 
                _pEndpointManager->getMatchingEndpointDef(reqStr.c_str(), 
                                convToRESTAPIMethod(requestHeader.extract.method));
        if (!pEndpointDef)
        {
#ifdef DEBUG_WEB_HANDLER_REST_API
            LOG_W("WebHandlerRestAPI", "getNewResponder no matching endpoint found %s", requestHeader.URL.c_str());
#endif
            return NULL;
        }
        // Looks like we can handle this so create a new responder object
        RdWebResponder* pResponder = new RdWebResponderRestAPI(pEndpointDef, this, params, reqStr, requestHeader.extract);

        // Debug
#ifdef DEBUG_WEB_HANDLER_REST_API
        LOG_I("WebHandlerRestAPI", "getNewResponder constructed new responder %lx uri %s", (unsigned long)pResponder, requestHeader.URL.c_str());
#endif

        // Return new responder - caller must clean up by deleting object when no longer needed
        return pResponder;
    }

private:
    RestAPIEndpointManager* _pEndpointManager;
    String _restAPIPrefix;

    // Mapping from web-server method to RESTAPI method enums
    RestAPIEndpointDef::EndpointMethod convToRESTAPIMethod(RdWebServerMethod method)
    {
        switch(method)
        {
            case WEB_METHOD_POST: return RestAPIEndpointDef::ENDPOINT_POST;
            case WEB_METHOD_PUT: return RestAPIEndpointDef::ENDPOINT_PUT;
            case WEB_METHOD_DELETE: return RestAPIEndpointDef::ENDPOINT_DELETE;
            default: return RestAPIEndpointDef::ENDPOINT_GET;
        }
    }
};
