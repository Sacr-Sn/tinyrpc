set(HEADERS
    include/http_codec.h   
    include/http_dispatcher.h 
    include/http_response.h
    include/http_define.h  
    include/http_request.h     
    include/http_servlet.h
)
 
install(FILES ${HEADERS} DESTINATION include/tinyrpc/net/http)