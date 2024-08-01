
make_global_var(sources_engine_NetServer
		"${CMAKE_CURRENT_SOURCE_DIR}/AutohostInterface.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/GameServer.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/GameParticipant.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/Protocol/BaseNetProtocol.cpp"
	)
set(sources_engine_NetClient
		"${CMAKE_CURRENT_SOURCE_DIR}/Protocol/NetProtocol.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/NetCommands.cpp"
	)

if(WIN32)
	if(ENABLE_LIVE_GAME)
		set(sources_engine_NetClient
			${sources_engine_NetClient}
			"${CMAKE_CURRENT_SOURCE_DIR}/LocalMemComm/common.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/LocalMemComm/local_mem_adapter.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/LocalMemComm/test_component.cpp"
		)
	endif()
endif()

make_global_var(sources_engine_Net
		${sources_engine_NetServer}
		${sources_engine_NetClient}
	)

