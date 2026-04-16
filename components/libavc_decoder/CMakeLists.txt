# components/libavc_decoder/CMakeLists.txt — libavc H.264 decoder as ESP-IDF component
#
# Builds the Android/Ittiam libavc decoder (generic C, no SIMD) for ESP32-P4.
# Used by test_apps/esp_shootout/ for Sub0h264 vs libavc performance comparison.

set(LIBAVC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../docs/reference/libavc")

if(NOT EXISTS "${LIBAVC_ROOT}/decoder/ih264d_api.c")
    message(WARNING "libavc source not found at ${LIBAVC_ROOT} — component disabled")
    return()
endif()

set(LIBAVC_COMMON_SRCS
    "${LIBAVC_ROOT}/common/ih264_buf_mgr.c"
    "${LIBAVC_ROOT}/common/ih264_cabac_tables.c"
    "${LIBAVC_ROOT}/common/ih264_cavlc_tables.c"
    "${LIBAVC_ROOT}/common/ih264_chroma_intra_pred_filters.c"
    "${LIBAVC_ROOT}/common/ih264_common_tables.c"
    "${LIBAVC_ROOT}/common/ih264_deblk_edge_filters.c"
    "${LIBAVC_ROOT}/common/ih264_deblk_tables.c"
    "${LIBAVC_ROOT}/common/ih264_disp_mgr.c"
    "${LIBAVC_ROOT}/common/ih264_dpb_mgr.c"
    "${LIBAVC_ROOT}/common/ih264_ihadamard_scaling.c"
    "${LIBAVC_ROOT}/common/ih264_inter_pred_filters.c"
    "${LIBAVC_ROOT}/common/ih264_iquant_itrans_recon.c"
    "${LIBAVC_ROOT}/common/ih264_list.c"
    "${LIBAVC_ROOT}/common/ih264_luma_intra_pred_filters.c"
    "${LIBAVC_ROOT}/common/ih264_mem_fns.c"
    "${LIBAVC_ROOT}/common/ih264_padding.c"
    "${LIBAVC_ROOT}/common/ih264_resi_trans_quant.c"
    "${LIBAVC_ROOT}/common/ih264_trans_data.c"
    "${LIBAVC_ROOT}/common/ih264_weighted_pred.c"
    "${LIBAVC_ROOT}/common/ithread.c"
)

set(LIBAVC_DECODER_SRCS
    "${LIBAVC_ROOT}/decoder/ih264d_api.c"
    "${LIBAVC_ROOT}/decoder/ih264d_bitstrm.c"
    "${LIBAVC_ROOT}/decoder/ih264d_cabac.c"
    "${LIBAVC_ROOT}/decoder/ih264d_cabac_init_tables.c"
    "${LIBAVC_ROOT}/decoder/ih264d_compute_bs.c"
    "${LIBAVC_ROOT}/decoder/ih264d_deblocking.c"
    "${LIBAVC_ROOT}/decoder/ih264d_dpb_mgr.c"
    "${LIBAVC_ROOT}/decoder/ih264d_format_conv.c"
    "${LIBAVC_ROOT}/decoder/ih264d_function_selector_generic.c"
    "${LIBAVC_ROOT}/decoder/ih264d_inter_pred.c"
    "${LIBAVC_ROOT}/decoder/ih264d_mb_utils.c"
    "${LIBAVC_ROOT}/decoder/ih264d_mvpred.c"
    "${LIBAVC_ROOT}/decoder/ih264d_nal.c"
    "${LIBAVC_ROOT}/decoder/ih264d_parse_bslice.c"
    "${LIBAVC_ROOT}/decoder/ih264d_parse_cabac.c"
    "${LIBAVC_ROOT}/decoder/ih264d_parse_cavlc.c"
    "${LIBAVC_ROOT}/decoder/ih264d_parse_headers.c"
    "${LIBAVC_ROOT}/decoder/ih264d_parse_islice.c"
    "${LIBAVC_ROOT}/decoder/ih264d_parse_mb_header.c"
    "${LIBAVC_ROOT}/decoder/ih264d_parse_pslice.c"
    "${LIBAVC_ROOT}/decoder/ih264d_parse_slice.c"
    "${LIBAVC_ROOT}/decoder/ih264d_process_bslice.c"
    "${LIBAVC_ROOT}/decoder/ih264d_process_intra_mb.c"
    "${LIBAVC_ROOT}/decoder/ih264d_process_pslice.c"
    "${LIBAVC_ROOT}/decoder/ih264d_quant_scaling.c"
    "${LIBAVC_ROOT}/decoder/ih264d_sei.c"
    "${LIBAVC_ROOT}/decoder/ih264d_tables.c"
    "${LIBAVC_ROOT}/decoder/ih264d_thread_compute_bs.c"
    "${LIBAVC_ROOT}/decoder/ih264d_thread_parse_decode.c"
    "${LIBAVC_ROOT}/decoder/ih264d_utils.c"
    "${LIBAVC_ROOT}/decoder/ih264d_vui.c"
    "${LIBAVC_ROOT}/decoder/riscv/ih264d_function_selector.c"
)

if(DEFINED IDF_TARGET)
    # ESP-IDF component build
    idf_component_register(
        SRCS ${LIBAVC_COMMON_SRCS} ${LIBAVC_DECODER_SRCS}
        INCLUDE_DIRS
            "${LIBAVC_ROOT}/decoder"
            "${LIBAVC_ROOT}/common"
        PRIV_INCLUDE_DIRS
            "${LIBAVC_ROOT}/common/riscv"
    )

    # Suppress warnings in third-party code (libavc uses GCC-isms)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wno-error
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        -Wno-sign-compare
        -Wno-pointer-sign
        -Wno-implicit-function-declaration
    )
else()
    # Desktop build (not used — desktop shootout links libavc's own CMake)
    message(STATUS "libavc_decoder component: ESP-IDF only")
endif()
