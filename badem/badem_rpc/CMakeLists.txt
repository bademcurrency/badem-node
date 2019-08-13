add_executable (badem_rpc
	entry.cpp)

target_link_libraries (badem_rpc
	rpc
	secure
	Boost::filesystem
	Boost::log
	Boost::log_setup
	Boost::program_options
	Boost::system
	Boost::thread
	Boost::boost)

target_compile_definitions(badem_rpc
	PUBLIC
		-DACTIVE_NETWORK=${ACTIVE_NETWORK}
	PRIVATE
		-DTAG_VERSION_STRING=${TAG_VERSION_STRING}
		-DGIT_COMMIT_HASH=${GIT_COMMIT_HASH})

if ((BADEM_GUI OR BDM_GUI) AND NOT APPLE)
	install(TARGETS badem_rpc
			RUNTIME DESTINATION .
	)
endif()
