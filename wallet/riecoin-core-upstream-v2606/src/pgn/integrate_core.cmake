# Native PGN consensus/work dependencies belong to every Core frontend.
# Qt panel tests consume these targets; they do not enable production sources.
include_guard(GLOBAL)
# PGN public common headers expose Boost.MultiIndex types. Declare their
# dependency explicitly so a private SDK works without global compiler headers.
target_link_libraries(riecoin_common PUBLIC Boost::headers)
target_link_libraries(riecoin_node PUBLIC Boost::headers)
target_sources(riecoin_common PRIVATE
  ${PROJECT_SOURCE_DIR}/src/pgn/vanity_public_search.cpp
  ${PROJECT_SOURCE_DIR}/src/pgn/vanity_key_completion.cpp
  ${PROJECT_SOURCE_DIR}/src/pgn/vanity_prefix_plan.cpp
  ${PROJECT_SOURCE_DIR}/src/pgn/vanity_orbit_completion.cpp)
target_sources(riecoin_node PRIVATE ${PROJECT_SOURCE_DIR}/src/pgn/utxo_owner_authorization.cpp)
target_sources(riecoin_node PRIVATE ${PROJECT_SOURCE_DIR}/src/pgn/scientific_fact.cpp)
target_sources(riecoin_node PRIVATE ${PROJECT_SOURCE_DIR}/src/pgn/ticket_request.cpp)

target_sources(riecoin_node PRIVATE ${PROJECT_SOURCE_DIR}/src/pgn/audit_operator_policy.cpp)
target_sources(riecoin_node PRIVATE ${PROJECT_SOURCE_DIR}/src/pgn/ticket_request_confirmation.cpp)
target_sources(riecoin_node PRIVATE ${PROJECT_SOURCE_DIR}/src/pgn/ticket_seed.cpp)
target_sources(riecoin_node PRIVATE ${PROJECT_SOURCE_DIR}/src/pgn/candidate_range_plan.cpp)
target_sources(riecoin_node PRIVATE ${PROJECT_SOURCE_DIR}/src/pgn/chain_range_policy.cpp)
target_sources(riecoin_node PRIVATE ${PROJECT_SOURCE_DIR}/src/pgn/candidate_range_schedule.cpp)
target_sources(riecoin_node PRIVATE ${PROJECT_SOURCE_DIR}/src/pgn/candidate_range_reader.cpp)
target_sources(riecoin_node PRIVATE ${PROJECT_SOURCE_DIR}/src/pgn/candidate_reserved_science.cpp)
target_sources(riecoin_node PRIVATE ${PROJECT_SOURCE_DIR}/src/pgn/candidate_block_science.cpp)
target_sources(riecoin_node PRIVATE ${PROJECT_SOURCE_DIR}/src/pgn/chain_science_policy.cpp)
target_sources(riecoin_node PRIVATE ${PROJECT_SOURCE_DIR}/src/pgn/native_hourly_activation.cpp)
target_sources(riecoin_node PRIVATE ${PROJECT_SOURCE_DIR}/src/pgn/request_registry.cpp)
target_sources(riecoin_node PRIVATE ${PROJECT_SOURCE_DIR}/src/pgn/proposed_ticket_requests.cpp)
target_sources(riecoin_node PRIVATE ${PROJECT_SOURCE_DIR}/src/pgn/request_history_archive.cpp)
target_link_libraries(riecoin_node PRIVATE pgn-committed-gap)
