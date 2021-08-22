/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RdWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RdWebConnection.h"
#include "RdWebInterface.h"
#include "RdWebHandler.h"
#include "RdWebConnManager.h"
#include "RdWebResponder.h"
#include <Logger.h>
#include <Utils.h>
#include <ArduinoTime.h>
#include <RdJson.h>
#include <functional>

static const char *MODULE_PREFIX = "RdWebConn";

// Warn
#define WARN_WEB_CONN_ERROR_CLOSE

// Debug
// #define DEBUG_WEB_REQUEST_HEADERS
// #define DEBUG_WEB_REQUEST_HEADER_DETAIL
// #define DEBUG_WEB_REQUEST_READ
// #define DEBUG_WEB_REQUEST_RESP
// #define DEBUG_WEB_REQUEST_READ_START_END
// #define DEBUG_RESPONDER_PROGRESS
// #define DEBUG_RESPONDER_PROGRESS_DETAIL
// #define DEBUG_RESPONDER_HEADER_DETAIL
// #define DEBUG_RESPONDER_CONTENT_DETAIL
// #define DEBUG_RESPONDER_CREATE_DELETE
// #define DEBUG_WEB_SOCKET_SEND
// #define DEBUG_WEB_SSEVENT_SEND
// #define DEBUG_WEB_CONNECTION_DATA_PACKETS
// #define DEBUG_WEB_CONNECTION_DATA_PACKETS_CONTENTS
// #define DEBUG_WEB_CONN_OPEN_CLOSE
// #define DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS 50
// #define DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS 50

#ifdef DEBUG_TRACE_HEAP_USAGE_WEB_CONN
#include "esp_heap_trace.h"
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RdWebConnection::RdWebConnection()
{
    // Responder
    _pResponder = nullptr;
    _pClientConn = nullptr;
    
    // Clear
    clear();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RdWebConnection::~RdWebConnection()
{
    // Check if there is a responder to clean up
    if (_pResponder)
    {
#ifdef DEBUG_RESPONDER_CREATE_DELETE
        LOG_W(MODULE_PREFIX, "destructor deleting _pResponder %d", (uint32_t)_pResponder);
#endif
        delete _pResponder;
    }

    // Check if there is a client to clean up
    if (_pClientConn)
    {
#ifdef DEBUG_RESPONDER_CREATE_DELETE
        LOG_W(MODULE_PREFIX, "destructor deleting _pClientConn %d", _pClientConn->getClientId());
#endif
        delete _pClientConn;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set new connection
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RdWebConnection::setNewConn(RdClientConnBase* pClientConn, RdWebConnManager* pConnManager,
                uint32_t maxSendBufferBytes)
{
    // Error check - there should not be a current client otherwise there's been a mistake!
    if (_pClientConn != nullptr)
    {
        // Caller should only use this method if connection is inactive
        LOG_E(MODULE_PREFIX, "setNewConn existing connection active %d", _pClientConn->getClientId());
        return false;
    }

    // Clear first
    clear();

    // New connection
    _pClientConn = pClientConn;
    _pConnManager = pConnManager;
    _timeoutStartMs = millis();
    _timeoutLastActivityMs = millis();
    _timeoutActive = true;
    _timeoutDurationMs = MAX_STD_CONN_DURATION_MS;
    _timeoutOnIdleDurationMs = MAX_CONN_IDLE_DURATION_MS;
    _maxSendBufferBytes = maxSendBufferBytes;

    // Set non-blocking connection
    _pClientConn->setup(false);

    // Debug
#ifdef DEBUG_WEB_CONN_OPEN_CLOSE
    LOG_I(MODULE_PREFIX, "setNewConn connId %d", _pClientConn->getClientId());
#endif

    // Connection set
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clear
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RdWebConnection::clear()
{
    // Delete responder if there is one
    if (_pResponder)
    {
#ifdef DEBUG_RESPONDER_CREATE_DELETE
        LOG_W(MODULE_PREFIX, "clear deleting _pResponder %d", (uint32_t)_pResponder);
#endif
        delete _pResponder;
        _pResponder = nullptr;
    }

    // Delete any client
#ifdef DEBUG_WEB_CONN_OPEN_CLOSE
    if (_pClientConn)
    {
        LOG_I(MODULE_PREFIX, "clear deleting clientConn %d", _pClientConn->getClientId());
    }
#endif
    delete _pClientConn;
    _pClientConn = nullptr;

    // Clear all fields
    _pConnManager = nullptr;
    _isStdHeaderRequired = true;
    _sendSpecificHeaders = true;
    _httpResponseStatus = HTTP_STATUS_OK;
    _timeoutStartMs = 0;
    _timeoutLastActivityMs = 0;
    _timeoutDurationMs = MAX_STD_CONN_DURATION_MS;
    _timeoutOnIdleDurationMs = MAX_CONN_IDLE_DURATION_MS;
    _timeoutActive = false;
    _parseHeaderStr = "";
    _debugDataRxCount = 0;
    _maxSendBufferBytes = 0;
    _header.clear();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check Active
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RdWebConnection::isActive()
{
    return _pClientConn && _pClientConn->isActive();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send on websocket
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RdWebConnection::sendOnWebSocket(const uint8_t* pBuf, uint32_t bufLen)
{
#ifdef DEBUG_WEB_SOCKET_SEND
    LOG_I(MODULE_PREFIX, "sendOnWebSocket len %d responder %d connId %d", bufLen, (uint32_t)_pResponder, 
                    _pClientConn ? _pClientConn->getClientId() : 0);
#endif

    // Send to responder
    if (_pResponder)
        return _pResponder->sendFrame(pBuf, bufLen);

    // Failure
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send server-side-event
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RdWebConnection::sendOnSSEvents(const char* eventContent, const char* eventGroup)
{
#ifdef DEBUG_WEB_SSEVENT_SEND
    LOG_I(MODULE_PREFIX, "sendOnSSEvents eventGroup %s eventContent %s responder %d connId %d", 
                eventGroup, eventContent,
                (uint32_t)_pResponder, 
                _pClientConn ? _pClientConn->getClientId() : 0);
#endif

    // Send to responder
    if (_pResponder)
    {
        _pResponder->sendEvent(eventContent, eventGroup);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service - called frequently
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RdWebConnection::service()
{
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
    uint32_t debugServiceStartMs = millis();
    uint32_t debugServiceRespTimeMs = 0;
    uint32_t debugServiceGetDataMs = 0;
    uint32_t debugServiceDataLen = 0;
    uint32_t debugDataResponderElapMs = 0;
    uint32_t debugDataEndElapMs = 0;
    uint32_t debugServiceConnHeaderMs = 0;
    uint32_t debugTimeOutHandlerElapMs = 0;
#endif

    // Check active
    if (!_pClientConn)
        return;

    // Check timeout
    if (_timeoutActive && (Utils::isTimeout(millis(), _timeoutStartMs, _timeoutDurationMs) ||
                    Utils::isTimeout(millis(), _timeoutLastActivityMs, _timeoutOnIdleDurationMs)))
    {
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        uint32_t debugTimeOutHandlerStartMs = millis();
#endif
        LOG_W(MODULE_PREFIX, "service timeout on connection connId %d", _pClientConn->getClientId());
        clear();

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        debugTimeOutHandlerElapMs = millis() - debugTimeOutHandlerStartMs;
#endif
        return;
    }

    // Service responder and check if ready for data, if there is no responder
    // then always ready as we're building the header, etc
    bool checkForNewData = true;
    if (_pResponder)
    {
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        uint32_t debugRespHdlDataStartMs = millis();
#endif

        _pResponder->service();
        checkForNewData = _pResponder->readyForData();

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        debugServiceRespTimeMs = millis() - debugRespHdlDataStartMs;
#endif
    }

    // Check for new data if required
    uint32_t dataLen = 0;
    bool closeRequired = false;
    uint8_t* pData = nullptr;
    bool dataAvailable = false;
    if (checkForNewData)
    {
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        uint32_t getDataStartMs = millis();
#endif

        pData = _pClientConn->getDataStart(dataLen, closeRequired);
        dataAvailable = (pData != nullptr) && (dataLen != 0);

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        debugServiceGetDataMs = millis() - getDataStartMs;
        debugServiceDataLen = dataAvailable ? dataLen : 0;
#endif
    }

    // Check if data available
    if (dataAvailable)
    {
        // Update for timeout
        _timeoutLastActivityMs = millis();

        // Update stats
        _debugDataRxCount += dataLen;
#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS
        LOG_I(MODULE_PREFIX, "service got new data len %d rxTotal %d", dataLen, _debugDataRxCount);
#endif
#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS_CONTENTS
        String debugStr;
        Utils::getHexStrFromBytes(pData, dataLen, debugStr);
        LOG_I(MODULE_PREFIX, "connId %d RX: %s", _pClientConn->getClientId(), debugStr.c_str());
#endif
    }

    // See if we are forming the header
    uint32_t bufPos = 0;
    bool errorOccurred = false;
    if (dataAvailable && !_header.isComplete)
    {
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        uint32_t debugConnHeaderStartMs = millis();
#endif

        if (!serviceConnHeader(pData, dataLen, bufPos))
        {
            LOG_W(MODULE_PREFIX, "service connHeader error closing connId %d", _pClientConn->getClientId());
            errorOccurred = true;
        }

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        debugServiceConnHeaderMs = millis() - debugConnHeaderStartMs;
#endif
    }

    // Service response - may remain in this state for multiple service loops
    // (e.g. for file-transfer / web-sockets)
    if (_header.isComplete)
    {
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        uint32_t debugResponderStartMs = millis();
#endif

        if (!responderHandleData(pData, dataLen, bufPos))
        {
#ifdef DEBUG_RESPONDER_PROGRESS
            LOG_I(MODULE_PREFIX, "service no longer sending so close connId %d", _pClientConn->getClientId());
#endif
            closeRequired = true;
        }

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        debugDataResponderElapMs = millis() - debugResponderStartMs;
#endif
    }

    // Data all handled if acquired
    if (checkForNewData)
    {
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        uint32_t debugDataEndStartMs = millis();
#endif
        _pClientConn->getDataEnd();
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        debugDataEndElapMs = millis() - debugDataEndStartMs;
#endif
    }

    // Check for close
    if (errorOccurred || closeRequired)
    {
#ifdef DEBUG_WEB_CONN_OPEN_CLOSE
        LOG_I(MODULE_PREFIX, "service conn closing cause %s connId %d", 
                errorOccurred ? "ErrorOccurred" : "CloseRequired", 
                _pClientConn->getClientId());
#endif

        // This closes any connection and clears status ready for a new one
        clear();

#ifdef DEBUG_TRACE_HEAP_USAGE_WEB_CONN
        heap_trace_stop();
        heap_trace_dump();
#endif
    }

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
    if (millis() - debugServiceStartMs > DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS)
    {
        LOG_I(MODULE_PREFIX, "service SLOW total %ldms srvResp %dms dataStart %dms dataLen %d connHdr %dms datResp %dms dataEnd %dms timoHdlr %dms", 
                millis() - debugServiceStartMs,
                debugServiceRespTimeMs,
                debugServiceGetDataMs,
                debugServiceDataLen,
                debugServiceConnHeaderMs,
                debugDataResponderElapMs,
                debugDataEndElapMs,
                debugTimeOutHandlerElapMs);
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service connection header
// Returns false on header error
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RdWebConnection::serviceConnHeader(const uint8_t* pRxData, uint32_t dataLen, uint32_t& curBufPos)
{
    if (!_pConnManager)
        return false;

    // Check for data
    if (dataLen == 0)
        return true;

    // Debug
#ifdef DEBUG_WEB_REQUEST_HEADER_DETAIL
    {
        String rxStr;
        Utils::strFromBuffer(pRxData, dataLen, rxStr);
        LOG_I(MODULE_PREFIX, "req data:\n%s", rxStr.c_str());
    }
#endif

    // Handle data for header
    bool headerOk = handleHeaderData(pRxData, dataLen, curBufPos);
    if (!headerOk)
    {
        setHTTPResponseStatus(HTTP_STATUS_BADREQUEST);
        return false;
    }

    // Check if header if now complete
    if (!_header.isComplete)
        return true;

    // Debug
#ifdef DEBUG_WEB_REQUEST_HEADERS
    LOG_I(MODULE_PREFIX, "onRxData headersOK method %s fullURI %s contentType %s contentLength %d host %s isContinue %d isMutilpart %d multipartBoundary %s", 
            RdWebInterface::getHTTPMethodStr(_header.extract.method), _header.URIAndParams.c_str(),
            _header.extract.contentType.c_str(), _header.extract.contentLength, _header.extract.host.c_str(), 
            _header.isContinue, _header.extract.isMultipart, _header.extract.multipartBoundary.c_str());
    LOG_I(MODULE_PREFIX, "onRxData headerExt auth %s isComplete %d isDigest %d reqConnType %d wsKey %s wsVers %s", 
            _header.extract.authorization.c_str(), _header.isComplete, _header.extract.isDigest, 
            _header.reqConnType, _header.webSocketKey.c_str(), _header.webSocketVersion.c_str());
#endif

    // Now find a responder
    RdHttpStatusCode statusCode = HTTP_STATUS_NOTFOUND;
    // Delete any existing responder - there shouldn't be one
    if (_pResponder)
    {
        LOG_W(MODULE_PREFIX, "onRxData unexpectedly deleting _pResponder %d", (uint32_t)_pResponder);
        delete _pResponder;
        _pResponder = nullptr;
    }

    // Get a responder (we are responsible for deletion)
    RdWebRequestParams params(_maxSendBufferBytes, _pConnManager->getStdResponseHeaders(), 
                std::bind(&RdWebConnection::rawSendOnConn, this, std::placeholders::_1, std::placeholders::_2));
    _pResponder = _pConnManager->getNewResponder(_header, params, statusCode);
#ifdef DEBUG_RESPONDER_CREATE_DELETE
    if (_pResponder) 
    {
        uint32_t channelID = 0;
        bool chanIdOk = _pResponder->getProtocolChannelID(channelID);
        LOG_I(MODULE_PREFIX, "New Responder created type %s chanID %d%s responder %d", _pResponder->getResponderType(), 
                            channelID, chanIdOk ? "" : " (INVALID)", (uint32_t)_pResponder);
    } 
    else 
    {
        LOG_W(MODULE_PREFIX, "Failed to create responder URI %s HTTP resp %d", _header.URIAndParams.c_str(), statusCode);
    }
#endif

    // Check we got a responder
    if (!_pResponder)
    {
        setHTTPResponseStatus(statusCode);
    }
    else
    {
        // Remove timeouts on long-running responders
        if (_pResponder->leaveConnOpen())
            _timeoutActive = false;

        // Start responder
        _pResponder->startResponding(*this);
    }

    // Ok
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send data to responder
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RdWebConnection::responderHandleData(const uint8_t* pRxData, uint32_t dataLen, uint32_t& curBufPos)
{
#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
    uint32_t debugRespHdlDataStartMs = millis();
    uint32_t debugRespHdlDataHandleDataMs = 0;
    uint32_t debugResponderServiceElapMs = 0;
    uint32_t debugHandleRespElapMs = 0;
    uint32_t debugSendStdHdrElapMs = 0;
#endif

    // Hand any data (if there is any) to responder (if there is one)
    bool errorOccurred = false;
    if (_pResponder && (curBufPos < dataLen) && pRxData)
    {
        _pResponder->handleData(pRxData+curBufPos, dataLen-curBufPos);
#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
        debugRespHdlDataHandleDataMs = millis() - debugRespHdlDataStartMs;
#endif
    }

    // Service the responder (if there is one)
    if (_pResponder)
    {
#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
        uint32_t debugResponderServiceStartMs = millis();
#endif
        _pResponder->service();
#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
        debugResponderServiceElapMs = millis() - debugResponderServiceStartMs;
#endif
    }

    // Handle active responder responses
    if (_pResponder && _pResponder->isActive())
    {
#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
        uint32_t debugHandleRespStartMs = millis();
#endif
        // Get next chunk of response
        if (_maxSendBufferBytes > MAX_BUFFER_ALLOCATED_ON_STACK)
        {
            uint8_t* pSendBuffer = new uint8_t[_maxSendBufferBytes];
            errorOccurred = !handleResponseWithBuffer(pSendBuffer);
            delete [] pSendBuffer;
        }
        else
        {
            uint8_t pSendBuffer[_maxSendBufferBytes];
            errorOccurred = !handleResponseWithBuffer(pSendBuffer);
        }

        // Record time of activity for timeouts
        _timeoutLastActivityMs = millis();

#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
        debugHandleRespElapMs = millis() - debugHandleRespStartMs;
#endif
    }

    // Send the standard response and headers if required    
    else if (_isStdHeaderRequired && (!_pResponder || _pResponder->isStdHeaderRequired()))
    {
        // Send standard headers
#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
        uint32_t debugSendStdHdrStartMs = millis();
#endif

        errorOccurred = !sendStandardHeaders();

#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
        debugSendStdHdrElapMs = millis() - debugSendStdHdrStartMs;
#endif

        // Done headers
        _isStdHeaderRequired = false;
    }

    // Debug
#ifdef DEBUG_RESPONDER_PROGRESS_DETAIL
    LOG_I(MODULE_PREFIX, "serviceResponse responder %s isActive %s connId %d", 
                _pResponder ? "YES" : "NO", 
                (_pResponder && _pResponder->isActive()) ? "YES" : "NO", 
                _pClientConn->getClientId());
#endif

#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
    if (millis() - debugRespHdlDataStartMs > DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS)
    {
        LOG_I(MODULE_PREFIX, "responderHandleData total %ldms handleData %dms servResp %dms hdlResp %dms stdHdr %dms", 
            millis() - debugRespHdlDataStartMs,
            debugRespHdlDataHandleDataMs, 
            debugResponderServiceElapMs,
            debugHandleRespElapMs,
            debugSendStdHdrElapMs);
    }
#endif

    // If no responder then that's it
    if (!_pResponder || errorOccurred)
        return false;

    // Return indication of more to come
    return _pResponder->isActive();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle header data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RdWebConnection::handleHeaderData(const uint8_t* pRxData, uint32_t dataLen, uint32_t& curBufPos)
{
    // Go through received data extracting headers
    uint32_t pos = 0;
    while (true)
    {
        // Find eol if there is one
        uint32_t lfFoundPos = 0;
        for (lfFoundPos = pos; lfFoundPos < dataLen; lfFoundPos++)
            if (pRxData[lfFoundPos] == '\n')
                break;

        // Extract string
        String newStr;
        Utils::strFromBuffer(pRxData + pos, lfFoundPos - pos, newStr);
        newStr.trim();

        // Add to parse header string
        _parseHeaderStr += newStr;

        // Move on
        pos = lfFoundPos + 1;

        // Check if we have found a full line
        if (lfFoundPos != dataLen)
        {
            // Parse header line
            if (!parseHeaderLine(_parseHeaderStr))
                return false;
            _parseHeaderStr = "";
        }

        // Check all done
        if ((pos >= dataLen) || (_header.isComplete))
            break;
    }
    curBufPos = pos;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Parse header line
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RdWebConnection::parseHeaderLine(const String& line)
{
    // Headers
#ifdef DEBUG_WEB_REQUEST_HEADER_DETAIL
    LOG_I(MODULE_PREFIX, "header line len %d = %s", line.length(), line.c_str());
#endif

    // Check if we're looking at the request line
    if (!_header.gotFirstLine)
    {
        // Check blank request line
        if (line.length() == 0)
            return false;

        // Parse method, etc
        if (!parseRequestLine(line))
            return false;

        // Debug
#ifdef DEBUG_WEB_REQUEST_HEADERS
        LOG_I(MODULE_PREFIX, "parseHeaderLine method %s URL %s params %s fullURI %s", 
                    RdWebInterface::getHTTPMethodStr(_header.extract.method), _header.URL.c_str(), _header.params.c_str(),
                    _header.URIAndParams.c_str());
#endif

        // Next parsing headers
        _header.gotFirstLine = true;
        return true;
    }

    // Check if we've finished all lines
    if (line.length() == 0)
    {
        // Debug
#ifdef DEBUG_WEB_REQUEST_HEADERS
        LOG_I(MODULE_PREFIX, "End of headers");
#endif

        // Check if continue required
        if (_header.isContinue)
        {
            const char *response = "HTTP/1.1 100 Continue\r\n\r\n";
            if (!rawSendOnConn((const uint8_t*) response, strlen(response)))
                return false;
        }

        // Header now complete
        _header.isComplete = true;
    }
    else
    {
        // Handle each line of header
        parseNameValueLine(line);
    }

    // Ok
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Parse first line of HTTP header
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RdWebConnection::parseRequestLine(const String& reqLine)
{
    // Methods
    static const char* WEB_REQ_METHODS [] = { "GET", "POST", "DELETE", "PUT", "PATCH", "HEAD", "OPTIONS" };
    static const RdWebServerMethod WEB_REQ_METHODS_ENUM [] = { WEB_METHOD_GET, WEB_METHOD_POST, WEB_METHOD_DELETE, 
                        WEB_METHOD_PUT, WEB_METHOD_PATCH, WEB_METHOD_HEAD, WEB_METHOD_OPTIONS };
    static const uint32_t WEB_REQ_METHODS_NUM = sizeof(WEB_REQ_METHODS) / sizeof(WEB_REQ_METHODS[0]);

    // Method
    int sepPos = reqLine.indexOf(' ');
    String method = reqLine.substring(0, sepPos);
    _header.extract.method = WEB_METHOD_NONE;
    for (uint32_t i = 0; i < WEB_REQ_METHODS_NUM; i++)
    {
        if (method.equalsIgnoreCase(WEB_REQ_METHODS[i]))
        {
            _header.extract.method = WEB_REQ_METHODS_ENUM[i];
            break;
        }
    }

    // Check valid
    if (_header.extract.method == WEB_METHOD_NONE)
        return false;

    // URI
    int sep2Pos = reqLine.indexOf(' ', sepPos+1);
    if (sep2Pos < 0)
        return false;
    _header.URIAndParams = decodeURL(reqLine.substring(sepPos+1, sep2Pos));

    // Split out params if present
    _header.URL = _header.URIAndParams;
    int paramPos = _header.URIAndParams.indexOf('?');
    _header.params = "";
    if (paramPos > 0)
    {
        _header.URL = _header.URIAndParams.substring(0, paramPos);
        _header.params = _header.URIAndParams.substring(paramPos+1);
    }

    // Remainder is the version string
    _header.versStr = reqLine.substring(sep2Pos+1);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Parse name/value pairs of HTTP header
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RdWebConnection::parseNameValueLine(const String& reqLine)
{
    // Extract header name/value pairs
    int nameEndPos = reqLine.indexOf(':');
    if (nameEndPos < 0)
        return;

    // Parts
    String name = reqLine.substring(0, nameEndPos);
    String val = reqLine.substring(nameEndPos+2);

    // Store
    if (_header.nameValues.size() >= RdWebRequestHeader::MAX_WEB_HEADERS)
        return;
    _header.nameValues.push_back({name, val});

    // Handle named headers
    // Parsing derived from AsyncWebServer menodev
    if (name.equalsIgnoreCase("Host"))
    {
        _header.extract.host = val;
    }
    else if (name.equalsIgnoreCase("Content-Type"))
    {
        _header.extract.contentType = val.substring(0, val.indexOf(';'));
        if (val.startsWith("multipart/"))
        {
            _header.extract.multipartBoundary = val.substring(val.indexOf('=') + 1);
            _header.extract.multipartBoundary.replace("\"", "");
            _header.extract.isMultipart = true;
        }
    }
    else if (name.equalsIgnoreCase("Content-Length"))
    {
        _header.extract.contentLength = atoi(val.c_str());
    }
    else if (name.equalsIgnoreCase("Expect") && val.equalsIgnoreCase("100-continue"))
    {
        _header.isContinue = true;
    }
    else if (name.equalsIgnoreCase("Authorization"))
    {
        if (val.length() > 5 && val.substring(0, 5).equalsIgnoreCase("Basic"))
        {
            _header.extract.authorization = val.substring(6);
        }
        else if (val.length() > 6 && val.substring(0, 6).equalsIgnoreCase("Digest"))
        {
            _header.extract.isDigest = true;
            _header.extract.authorization = val.substring(7);
        }
    }
    else if (name.equalsIgnoreCase("Upgrade") && val.equalsIgnoreCase("websocket"))
    {
        // WebSocket request can be uniquely identified by header: [Upgrade: websocket]
        _header.reqConnType = REQ_CONN_TYPE_WEBSOCKET;
    }
    else if (name.equalsIgnoreCase("Accept"))
    {
        String acceptLC = val;
        acceptLC.toLowerCase();
        if (acceptLC.indexOf("text/event-stream") >= 0)
        {
            // WebEvent request can be uniquely identified by header:  [Accept: text/event-stream]
            _header.reqConnType = REQ_CONN_TYPE_EVENT;
        }
    }
    else if (name.equalsIgnoreCase("Sec-WebSocket-Key"))
    {
        _header.webSocketKey = val;
    }
    else if (name.equalsIgnoreCase("Sec-WebSocket-Version"))
    {
        _header.webSocketVersion = val;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Decode URL escaped string
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String RdWebConnection::decodeURL(const String &inURL) const
{
    // Go through handling encoding
    const char* pCh = inURL.c_str();
    String outURL;
    outURL.reserve(inURL.length());
    while (*pCh)
    {
        // Check for % escaping
        if ((*pCh == '%') && *(pCh+1) && *(pCh+2))
        {
            char newCh = Utils::getHexFromChar(*(pCh+1)) * 16 + Utils::getHexFromChar(*(pCh+2));
            outURL.concat(newCh);
            pCh += 3;
        }
        else
        {
            outURL.concat(*pCh == '+' ? ' ' : *pCh);
            pCh++;
        }
    }
    return outURL;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set HTTP response status
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RdWebConnection::setHTTPResponseStatus(RdHttpStatusCode responseCode)
{
#ifdef DEBUG_WEB_REQUEST_RESP
    LOG_I(MODULE_PREFIX, "Setting response code %s (%d)", RdWebInterface::getHTTPStatusStr(responseCode), responseCode);
#endif
    _httpResponseStatus = responseCode;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Raw send on connection
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RdWebConnection::rawSendOnConn(const uint8_t* pBuf, uint32_t bufLen)
{
    // Check connection
    if (!_pClientConn)
    {
        LOG_W(MODULE_PREFIX, "rawSendOnConn conn is nullptr");
        return false;
    }

#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS_CONTENTS
    String debugStr;
    Utils::getHexStrFromBytes(pBuf, bufLen, debugStr);
    LOG_I(MODULE_PREFIX, "connId %d TX: %s", _pClientConn->getClientId(), debugStr.c_str());
#endif

    // Send
    return _pClientConn->write(pBuf, bufLen);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send standard headers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RdWebConnection::sendStandardHeaders()
{
    // Send the first line
    char respLine[100];
    snprintf(respLine, sizeof(respLine), "HTTP/1.1 %d %s\r\n", _httpResponseStatus, 
                RdWebInterface::getHTTPStatusStr(_httpResponseStatus));
    if (!rawSendOnConn((const uint8_t*)respLine, strlen(respLine)))
        return false;

#ifdef DEBUG_RESPONDER_HEADER_DETAIL
    // Debug
    LOG_I(MODULE_PREFIX, "sendStandardHeaders sent %s clientId %d", respLine, _pClientConn ? _pClientConn->getClientId() : 0);
#endif

    // Get the content type
    if (_pResponder && _pResponder->getContentType())
    {
        snprintf(respLine, sizeof(respLine), "Content-Type: %s\r\n", _pResponder->getContentType());
        if (!rawSendOnConn((const uint8_t*)respLine, strlen(respLine)))
            return false;

#ifdef DEBUG_RESPONDER_HEADER_DETAIL
        // Debug
        LOG_I(MODULE_PREFIX, "sendStandardHeaders sent %s clientId %d", respLine, _pClientConn ? _pClientConn->getClientId() : 0);
#endif
    }

    // Send other headers
    if (_pConnManager)
    {
        std::list<RdJson::NameValuePair>* pRespHeaders = _pConnManager->getStdResponseHeaders();
        for (RdJson::NameValuePair& nvPair : *pRespHeaders)
        {
            snprintf(respLine, sizeof(respLine), "%s: %s\r\n", nvPair.name.c_str(), nvPair.value.c_str());
            if (!rawSendOnConn((const uint8_t*)respLine, strlen(respLine)))
                return false;

#ifdef DEBUG_RESPONDER_HEADER_DETAIL
            // Debug
            LOG_I(MODULE_PREFIX, "sendStandardHeaders sent %s clientId %d", respLine, _pClientConn ? _pClientConn->getClientId() : 0);
#endif
        }
    }

    // Content length if required
    if (_pResponder)
    {
        int contentLength = _pResponder->getContentLength();
        if (contentLength >= 0)
        {
            snprintf(respLine, sizeof(respLine), "Content-Length: %d\r\n", contentLength);
            if (!rawSendOnConn((const uint8_t*)respLine, strlen(respLine)))
                return false;

#ifdef DEBUG_RESPONDER_HEADER_DETAIL
            // Debug
            LOG_I(MODULE_PREFIX, "sendStandardHeaders sent %s clientId %d", respLine, _pClientConn ? _pClientConn->getClientId() : 0);
#endif
        }
    }

    // Check if connection needs closing
    if (!_pResponder || !_pResponder->leaveConnOpen())
    {
        snprintf(respLine, sizeof(respLine), "Connection: close\r\n");
        if (!rawSendOnConn((const uint8_t*)respLine, strlen(respLine)))
            return false;

#ifdef DEBUG_RESPONDER_HEADER_DETAIL
        // Debug
        LOG_I(MODULE_PREFIX, "sendStandardHeaders sent %s clientId %d", respLine, _pClientConn ? _pClientConn->getClientId() : 0);
#endif
    }

    // Send end of header line
    return rawSendOnConn((const uint8_t*)"\r\n", 2);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle response with buffer provided
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RdWebConnection::handleResponseWithBuffer(uint8_t* pSendBuffer)
{
    uint32_t respSize = _pResponder->getResponseNext(pSendBuffer, _maxSendBufferBytes);

    // Check valid
    if (respSize != 0)
    {
        // Debug
#ifdef DEBUG_RESPONDER_CONTENT_DETAIL
        LOG_I(MODULE_PREFIX, "handleResponseWithBuffer writing %d clientId %d", respSize, _pClientConn ? _pClientConn->getClientId() : 0);
#endif

        // Check if standard reponse to be sent first
        if (_isStdHeaderRequired && _pResponder->isStdHeaderRequired())
        {
            // Send standard headers
            if (!sendStandardHeaders())
                return false;

            // Done headers
            _isStdHeaderRequired = false;
        }

        // Send
        return rawSendOnConn((const uint8_t*)pSendBuffer, respSize);
    }
    return true;
}
