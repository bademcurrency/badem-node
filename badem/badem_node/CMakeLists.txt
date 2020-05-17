add_executable (badem_node
	daemon.cpp
	daemon.hpp
	entry.cpp)

target_link_libraries (badem_node
	node
	rpc
	secure
	argon2
	Boost::boost
	${PLATFORM_LIBS}
)

if (BADEM_STACKTRACE_BACKTRACE)
	target_link_libraries(badem_node backtrace)
endif ()

target_compile_definitions(badem_node
	PRIVATE
		-DTAG_VERSION_STRING=${TAG_VERSION_STRING}
		-DGIT_COMMIT_HASH=${GIT_COMMIT_HASH}
		-DBOOST_PROCESS_SUPPORTED=${BOOST_PROCESS_SUPPORTED})

set_target_properties (badem_node
	PROPERTIES
		COMPILE_FLAGS
			"-DQT_NO_KEYWORDS -DBOOST_ASIO_HAS_STD_ARRAY=1")

add_custom_command(TARGET badem_node
				   POST_BUILD
				   COMMAND badem_node --generate_config node > ${PROJECT_BINARY_DIR}/config-node.toml.sample
				   COMMAND badem_node --generate_config rpc > ${PROJECT_BINARY_DIR}/config-rpc.toml.sample)

if ((BADEM_GUI OR BDM_GUI) AND NOT APPLE)
	install(TARGETS badem_node
			RUNTIME DESTINATION .
	)
endif()
