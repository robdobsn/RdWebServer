idf_component_register(
                SRCS 
                  "src/RdWebServer.cpp"
                  "src/RdWebConnManager.cpp"
                  "src/RdWebConnDefs.cpp"
                  "src/RdWebHandlerStaticFiles.cpp"
                  "src/RdWebConnection.cpp"
                  "src/RdWebResponderFile.cpp"
                  "src/RdWebResponderRestAPI.cpp"
                  "src/RdWebResponderWS.cpp"
                  "src/RdWebSocketLink.cpp"
                  "src/RdWebMultipart.cpp"
                INCLUDE_DIRS
                  "src"
                REQUIRES
                )

