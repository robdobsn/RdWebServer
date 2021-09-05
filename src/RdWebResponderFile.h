/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RdWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef ESP8266

#include <WString.h>
#include <Logger.h>
#include <ArduinoTime.h>
#include "RdWebResponder.h"
#include "RdWebRequestParams.h"
#include "FileSystemChunker.h"

class RdWebHandler;
class RdWebRequestHeader;

class RdWebResponderFile : public RdWebResponder
{
public:
    RdWebResponderFile(const String& filePath, RdWebHandler* pWebHandler, const RdWebRequestParams& params,
                    const RdWebRequestHeader& requestHeader);
    virtual ~RdWebResponderFile();

    // Handle inbound data
    virtual bool handleData(const uint8_t* pBuf, uint32_t dataLen) override final;

    // Start responding
    virtual bool startResponding(RdWebConnection& request) override final;

    // Get response next
    virtual uint32_t getResponseNext(uint8_t*& pBuf, uint32_t bufMaxLen) override final;

    // Get content type
    virtual const char* getContentType() override final;

    // Get content length (or -1 if not known)
    virtual int getContentLength() override final;

    // Leave connection open
    virtual bool leaveConnOpen() override final;

    // Get responder type
    virtual const char* getResponderType() override final
    {
        return "FILE";
    }

    // Ready for data
    virtual bool readyForData() override final
    {
        return true;
    }

private:
    String _filePath;
    RdWebHandler* _pWebHandler;
    FileSystemChunker _fileChunker;
    RdWebRequestParams _reqParams;
    uint32_t _fileLength;
    uint32_t _fileSendStartMs;
    std::vector<uint8_t> _lastChunkData;
    static const uint32_t SEND_DATA_OVERALL_TIMEOUT_MS = 5 * 60 * 1000;
    bool _isFinalChunk;
};

#endif
