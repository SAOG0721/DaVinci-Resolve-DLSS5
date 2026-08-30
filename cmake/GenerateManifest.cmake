if(NOT EXISTS "${PLUGIN_FILE}")
    message(FATAL_ERROR "Plugin file not found: ${PLUGIN_FILE}")
endif()
if(NOT EXISTS "${RUNTIME_DLL}")
    message(FATAL_ERROR "Runtime DLL not found: ${RUNTIME_DLL}")
endif()

file(SHA256 "${PLUGIN_FILE}" plugin_hash)
file(SHA256 "${RUNTIME_DLL}" runtime_hash)
file(WRITE "${OUTPUT_FILE}"
    "${plugin_hash} *ResolveDlss5.ofx\n"
    "${runtime_hash} *runtime/nvngx_dlssnr.dll\n")
