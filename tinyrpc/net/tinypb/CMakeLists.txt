set(HEADERS
    include/tinypb_codec.h             
    include/tinypb_rpc_closure.h
    include/tinypb_data.h               
    include/tinypb_rpc_controller.h
    include/tinypb_rpc_async_channel.h 
    include/tinypb_rpc_dispatcher.h
    include/tinypb_rpc_channel.h
)
 
install(FILES ${HEADERS} DESTINATION include/tinyrpc/net/tinypb)