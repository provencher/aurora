if (NOT PATCH_FILE)
  message(FATAL_ERROR "PATCH_FILE is required")
endif ()
if (NOT SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif ()

# Check if SOURCE_DIR is a git repository
set(_is_git_repo FALSE)
if (EXISTS "${SOURCE_DIR}/.git")
  set(_is_git_repo TRUE)
endif ()

if (_is_git_repo)
  # Use git apply when in a git repository
  if (NOT GIT_EXECUTABLE)
    message(FATAL_ERROR "GIT_EXECUTABLE is required for git-based patch application")
  endif ()

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -c "safe.directory=${SOURCE_DIR}" apply --check --ignore-whitespace "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE _patch_check_result
    OUTPUT_VARIABLE _patch_check_output
    ERROR_VARIABLE _patch_check_error
  )
  if (_patch_check_result EQUAL 0)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" -c "safe.directory=${SOURCE_DIR}" apply --ignore-whitespace "${PATCH_FILE}"
      WORKING_DIRECTORY "${SOURCE_DIR}"
      RESULT_VARIABLE _patch_apply_result
      OUTPUT_VARIABLE _patch_apply_output
      ERROR_VARIABLE _patch_apply_error
    )
    if (NOT _patch_apply_result EQUAL 0)
      message(FATAL_ERROR
        "Failed to apply patch ${PATCH_FILE}\n"
        "${_patch_apply_output}\n${_patch_apply_error}")
    endif ()
    message(STATUS "Applied patch ${PATCH_FILE}")
    return()
  endif ()

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -c "safe.directory=${SOURCE_DIR}" apply --reverse --check --ignore-whitespace "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE _patch_reverse_check_result
    OUTPUT_VARIABLE _patch_reverse_check_output
    ERROR_VARIABLE _patch_reverse_check_error
  )
  if (_patch_reverse_check_result EQUAL 0)
    message(STATUS "Patch already applied: ${PATCH_FILE}")
    return()
  endif ()

  message(FATAL_ERROR
    "Patch ${PATCH_FILE} cannot be applied and does not appear to be already applied.\n"
    "apply --check output:\n${_patch_check_output}\n${_patch_check_error}\n"
    "apply --reverse --check output:\n${_patch_reverse_check_output}\n${_patch_reverse_check_error}")

else ()
  # Not a git repository (e.g., tarball extract) - use patch command
  # First check if already applied by checking if patch would reverse cleanly
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env bash -c "patch --dry-run --reverse --ignore-whitespace --quiet -p1 < '${PATCH_FILE}'"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE _patch_reverse_check_result
    OUTPUT_VARIABLE _patch_reverse_check_output
    ERROR_VARIABLE _patch_reverse_check_error
  )
  if (_patch_reverse_check_result EQUAL 0)
    message(STATUS "Patch already applied: ${PATCH_FILE}")
    return()
  endif ()

  # Check if patch applies cleanly
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env bash -c "patch --dry-run --ignore-whitespace --quiet -p1 < '${PATCH_FILE}'"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE _patch_check_result
    OUTPUT_VARIABLE _patch_check_output
    ERROR_VARIABLE _patch_check_error
  )
  if (_patch_check_result EQUAL 0)
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E env bash -c "patch --ignore-whitespace -p1 < '${PATCH_FILE}'"
      WORKING_DIRECTORY "${SOURCE_DIR}"
      RESULT_VARIABLE _patch_apply_result
      OUTPUT_VARIABLE _patch_apply_output
      ERROR_VARIABLE _patch_apply_error
    )
    if (NOT _patch_apply_result EQUAL 0)
      message(FATAL_ERROR
        "Failed to apply patch ${PATCH_FILE}\n"
        "${_patch_apply_output}\n${_patch_apply_error}")
    endif ()
    message(STATUS "Applied patch ${PATCH_FILE}")
    return()
  endif ()

  message(FATAL_ERROR
    "Patch ${PATCH_FILE} cannot be applied and does not appear to be already applied.\n"
    "patch --dry-run output:\n${_patch_check_output}\n${_patch_check_error}")
endif ()