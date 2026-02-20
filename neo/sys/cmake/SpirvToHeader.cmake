# SpirvToHeader.cmake — Convert a SPIR-V binary file to a C header with
# a static const uint32_t array.
#
# Usage (from add_custom_command):
#   cmake -DINPUT_FILE=foo.vert.spv -DOUTPUT_FILE=foo_vert_spv.h
#         -DARRAY_NAME=foo_vert_spv -P SpirvToHeader.cmake

file(READ "${INPUT_FILE}" SPV_HEX HEX)
string(LENGTH "${SPV_HEX}" SPV_HEX_LEN)
math(EXPR SPV_BYTE_LEN "${SPV_HEX_LEN} / 2")

# Convert hex pairs to 0xNN bytes
set(BYTE_LIST "")
set(LINE "")
set(COUNT 0)
math(EXPR LAST "${SPV_HEX_LEN} - 1")

# Process 4 bytes (8 hex chars) at a time for uint32_t
# SPIR-V is always a multiple of 4 bytes
math(EXPR WORD_COUNT "${SPV_BYTE_LEN} / 4")
set(WORD_LIST "")
set(LINE "")
set(WORDS_ON_LINE 0)
set(POS 0)

while(POS LESS SPV_HEX_LEN)
	# Read 8 hex chars (4 bytes) as little-endian uint32_t
	string(SUBSTRING "${SPV_HEX}" ${POS} 2 B0)
	math(EXPR P1 "${POS} + 2")
	string(SUBSTRING "${SPV_HEX}" ${P1} 2 B1)
	math(EXPR P2 "${POS} + 4")
	string(SUBSTRING "${SPV_HEX}" ${P2} 2 B2)
	math(EXPR P3 "${POS} + 6")
	string(SUBSTRING "${SPV_HEX}" ${P3} 2 B3)

	set(WORD "0x${B3}${B2}${B1}${B0}")

	if(WORDS_ON_LINE EQUAL 0)
		set(LINE "\t${WORD}")
	else()
		set(LINE "${LINE}, ${WORD}")
	endif()
	math(EXPR WORDS_ON_LINE "${WORDS_ON_LINE} + 1")

	if(WORDS_ON_LINE EQUAL 6)
		if(WORD_LIST STREQUAL "")
			set(WORD_LIST "${LINE}")
		else()
			set(WORD_LIST "${WORD_LIST},\n${LINE}")
		endif()
		set(LINE "")
		set(WORDS_ON_LINE 0)
	endif()

	math(EXPR POS "${POS} + 8")
endwhile()

# Flush remaining
if(WORDS_ON_LINE GREATER 0)
	if(WORD_LIST STREQUAL "")
		set(WORD_LIST "${LINE}")
	else()
		set(WORD_LIST "${WORD_LIST},\n${LINE}")
	endif()
endif()

file(WRITE "${OUTPUT_FILE}"
"// Auto-generated from ${INPUT_FILE} — do not edit\n"
"#pragma once\n"
"#include <stdint.h>\n"
"\n"
"static const uint32_t ${ARRAY_NAME}[] = {\n"
"${WORD_LIST}\n"
"};\n"
"static const uint32_t ${ARRAY_NAME}_size = sizeof(${ARRAY_NAME});\n"
)
