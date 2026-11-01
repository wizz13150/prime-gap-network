# Candidate PGN native components. Building these does not activate consensus.
add_library(pgn-exact-amount-native exact_amount.cpp)
target_compile_features(pgn-exact-amount-native PUBLIC cxx_std_20)
target_include_directories(pgn-exact-amount-native PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
find_package(Boost CONFIG REQUIRED)
add_library(pgn-range-commitment range_commitment.cpp sparse_range_pool.cpp)
target_compile_features(pgn-range-commitment PUBLIC cxx_std_20)
target_include_directories(pgn-range-commitment PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${Boost_INCLUDE_DIRS})
add_library(pgn-merit-arithmetic merit_arithmetic.cpp)
target_link_libraries(pgn-merit-arithmetic PUBLIC pgn-range-commitment)
add_library(pgn-gap-verification gap_verification.cpp)
target_link_libraries(pgn-gap-verification PUBLIC pgn-range-commitment)
add_library(pgn-prime-certificate prime_certificate.cpp)
target_link_libraries(pgn-prime-certificate PUBLIC pgn-gap-verification)
add_library(pgn-prime-certificate-builder prime_certificate_builder.cpp)
target_link_libraries(pgn-prime-certificate-builder PUBLIC pgn-prime-certificate)
add_library(pgn-certified-gap certified_gap.cpp)
target_link_libraries(pgn-certified-gap PUBLIC pgn-prime-certificate)
add_library(pgn-certified-gap-builder certified_gap_builder.cpp)
target_link_libraries(pgn-certified-gap-builder PUBLIC pgn-certified-gap)
add_library(pgn-committed-gap committed_gap.cpp)
target_link_libraries(pgn-committed-gap PUBLIC pgn-certified-gap pgn-merit-arithmetic)
add_library(pgn-certified-gap-encoding certified_gap_encoding.cpp)
target_link_libraries(pgn-certified-gap-encoding PUBLIC pgn-certified-gap)
target_link_libraries(pgn-committed-gap PUBLIC pgn-certified-gap-encoding)
# These pure native libraries may be included from a Qt project. Do not inherit
# generated Qt translation units, resource compilers or UI processors.
set_target_properties(pgn-exact-amount-native pgn-range-commitment
  pgn-merit-arithmetic pgn-gap-verification pgn-prime-certificate pgn-prime-certificate-builder
  pgn-certified-gap pgn-certified-gap-builder pgn-committed-gap pgn-certified-gap-encoding PROPERTIES
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
