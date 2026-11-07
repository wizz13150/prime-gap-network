# One reproducible guard for the shared CPU/GPU/Total presentation contract.
# These existing tests need neither a device nor a running wallet/node.
set(_pgn_mining_ui_regression
    gauge-layout
    device_card_bands
    rate_gauge_scale
    cpu-card-metrics
    cpu-card-render
    metric-caption
    throttle-badge
    window-screen-fit
    gap-rate-window
    mining-status
    position_formats
    graph-content-fit
    difficulty-graph-border)

set(_pgn_mining_ui_regression_targets)
foreach(_check IN LISTS _pgn_mining_ui_regression)
    if(NOT TEST "${_check}" OR NOT TARGET "pgn-${_check}-test")
        message(FATAL_ERROR "Mining UI regression guard is missing: ${_check}")
    endif()
    set_property(TEST "${_check}" APPEND PROPERTY LABELS horizon-mining-ui-regression)
    list(APPEND _pgn_mining_ui_regression_targets "pgn-${_check}-test")
endforeach()

add_custom_target(check-horizon-mining-ui
    COMMAND "${CMAKE_COMMAND}" -E env "QT_QPA_PLATFORM=offscreen"
        "${CMAKE_CTEST_COMMAND}" --output-on-failure --timeout 40
        -L "^horizon-mining-ui-regression$"
    DEPENDS ${_pgn_mining_ui_regression_targets}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Checking shared CPU/GPU/Total layout, gauges, smoothing and graph bounds"
    USES_TERMINAL
    VERBATIM)

unset(_pgn_mining_ui_regression)
unset(_pgn_mining_ui_regression_targets)
unset(_check)
