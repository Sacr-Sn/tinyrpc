set(HEADERS
    abstract_codec.h   
    abstract_data.h
    abstract_dispatcher.h
    byte.h  
    fd_event.h     
    net_address.h
    reactor.h
    timer.h
)
 
install(FILES ${HEADERS} DESTINATION include/tinyrpc/net/comm)