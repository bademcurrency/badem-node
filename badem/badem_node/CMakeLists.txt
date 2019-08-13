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

target_compile_definitions(badem_node
	PRIVATE
		-DTAG_VERSION_STRING=${TAG_VERSION_STRING}
		-DGIT_COMMIT_HASH=${GIT_COMMIT_HASH}
		-DBOOST_PROCESS_SUPPORTED=${BOOST_PROCESS_SUPPORTED})

set_target_properties (badem_node
	PROPERTIES
		COMPILE_FLAGS
			"-DQT_NO_KEYWORDS -DBOOST_ASIO_HAS_STD_ARRAY=1")

if ((BADEM_GUI OR BDM_GUI) AND NOT APPLE)
	install(TARGETS badem_node
			RUNTIME DESTINATION .
	)
endif()
