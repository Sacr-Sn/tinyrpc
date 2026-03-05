set(HEADERS
    include/coctx.h
    include/coroutine.h
    include/coroutine_hook.h
    include/coroutine_pool.h
    include/memory.h
)
 
install(FILES ${HEADERS} DESTINATION include/tinyrpc/coroutine)