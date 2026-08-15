# Assemble SOURCE with ZASM and require the result to match REFERENCE exactly.
#
# Used as a CTest command:
#   cmake -DZASM=... -DSOURCE=... -DOUTPUT=... -DREFERENCE=... -P rom_compare.cmake

foreach(var ZASM SOURCE OUTPUT REFERENCE)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "${var} is not set")
    endif()
endforeach()

execute_process(COMMAND ${ZASM} -s ${SOURCE} -o ${OUTPUT} -x bin
                RESULT_VARIABLE assembly_result
                OUTPUT_VARIABLE assembly_output
                ERROR_VARIABLE assembly_output)

if(NOT assembly_result EQUAL 0)
    message(FATAL_ERROR "assembling ${SOURCE} failed (${assembly_result}):\n${assembly_output}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${OUTPUT} ${REFERENCE}
                RESULT_VARIABLE compare_result)

if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR "${OUTPUT} does not match the reference image ${REFERENCE}")
endif()
