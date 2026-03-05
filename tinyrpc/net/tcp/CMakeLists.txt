set(HEADERS
    include/abstract_slot.h  
    include/tcp_client.h                 
    include/tcp_server.h
    include/io_thread.h      
    include/tcp_connection.h
    include/tcp_buffer.h     
    include/tcp_connection_time_wheel.h
)
 
install(FILES ${HEADERS} DESTINATION include/tinyrpc/net/tcp)