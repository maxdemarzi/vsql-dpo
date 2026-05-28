# Copyright (c) 2026 VillageSQL Contributors
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.18)
project(vsql_corruptor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Extension name
set(EXTENSION_NAME "vsql_corruptor")

# Configure FetchContent to download hyrise/sql-parser
include(FetchContent)
FetchContent_Declare(
  hyrise_sql_parser
  GIT_REPOSITORY https://github.com/hyrise/sql-parser.git
  GIT_TAG        main
)

# Fetch dependency
FetchContent_GetProperties(hyrise_sql_parser)
if(NOT hyrise_sql_parser_POPULATED)
  FetchContent_Populate(hyrise_sql_parser)
endif()

# Compile hyrise/sql-parser as a static library
add_library(sqlparser STATIC
  ${hyrise_sql_parser_SOURCE_DIR}/src/SQLParser.cpp
  ${hyrise_sql_parser_SOURCE_DIR}/src/SQLParserResult.cpp
  ${hyrise_sql_parser_SOURCE_DIR}/src/parser/bison_parser.cpp
  ${hyrise_sql_parser_SOURCE_DIR}/src/parser/flex_lexer.cpp
  ${hyrise_sql_parser_SOURCE_DIR}/src/sql/CreateStatement.cpp
  ${hyrise_sql_parser_SOURCE_DIR}/src/sql/Expr.cpp
  ${hyrise_sql_parser_SOURCE_DIR}/src/sql/PrepareStatement.cpp
  ${hyrise_sql_parser_SOURCE_DIR}/src/sql/SQLStatement.cpp
  ${hyrise_sql_parser_SOURCE_DIR}/src/sql/statements.cpp
)

target_include_directories(sqlparser PUBLIC
  ${hyrise_sql_parser_SOURCE_DIR}/src
)

# Disable warnings in fetched dependency to keep output clean
if(NOT MSVC)
  target_compile_options(sqlparser PRIVATE -w)
endif()

# Find VillageSQL SDK
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(VillageSQL REQUIRED)

message(STATUS "VillageSQL SDK found at: ${VillageSQL_PREFIX}")
message(STATUS "VillageSQL include dir: ${VillageSQL_INCLUDE_DIR}")

# Create the vsql_corruptor shared library
add_library(vsql_corruptor SHARED
    src/vsql_corruptor.cc
    src/CorruptionEngine.cpp
    src/SQLUnparser.cpp
)

# Include directories
target_include_directories(vsql_corruptor PRIVATE
    ${VillageSQLExtensionFramework_INCLUDE_DIR}
    ${hyrise_sql_parser_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link dependencies
target_link_libraries(vsql_corruptor PRIVATE sqlparser)

# Create the VEB package
VEF_CREATE_VEB(
    NAME ${EXTENSION_NAME}
    LIBRARY_TARGET vsql_corruptor
    MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
)
