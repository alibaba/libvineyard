# - Config file for the vineyard package
#
# It defines the following variables
#
#  VINEYARD_INCLUDE_DIR         - include directory for vineyard
#  VINEYARD_INCLUDE_DIRS        - include directories for vineyard
#  VINEYARD_LIBRARIES           - libraries to link against
#  VINEYARDD_EXECUTABLE         - the vineyardd executable
#  VINEYARD_CODEGEN_EXECUTABLE  - the vineyard codegen executable

set(VINEYARD_HOME "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${CMAKE_CURRENT_LIST_DIR}/vineyard-targets.cmake")

set(VINEYARD_LIBRARIES @VINEYARD_INSTALL_LIBS@)
set(VINEYARD_INCLUDE_DIR "${VINEYARD_HOME}/include"
                         "${VINEYARD_HOME}/include/vineyard")
set(VINEYARD_INCLUDE_DIRS "${VINEYARD_INCLUDE_DIR}")

set(VINEYARDD_EXECUTABLE "${VINEYARD_HOME}/bin/vineyardd")

set(VINEYARD_CODEGEN_EXECUTABLE "${VINEYARD_HOME}/bin/vineyard-codegen")
