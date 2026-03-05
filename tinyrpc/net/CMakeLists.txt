set(HEADERS
    include/abstract_codec.h
    include/byte.h
    include/coroutine_mutex.h
    include/abstract_data.h   
    include/fd_event.h
    include/net_address.h
    include/timer.h
    include/abstract_dispatcher.h
    include/reactor.h
)

install(FILES ${HEADERS} DESTINATION include/tinyrpc/net)