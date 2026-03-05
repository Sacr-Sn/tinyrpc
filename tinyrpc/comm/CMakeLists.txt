set(HEADERS
    include/config.h
    include/error_code.h
    include/log.h
    include/msg_req.h
    include/mysql_instase.h
    include/run_time.h
    include/start.h
    include/string_util.h
    include/thread_pool.h
)
 
install(FILES ${HEADERS} DESTINATION include/tinyrpc/comm)