set(source_files
  bfd/config.h
  bfd/bfd_utils.cpp
  bfd/bfd_utils.h
  utils/defer.h
  utils/util.h
  utils/color_printf.h
  common/log.cpp
  common/log.h
  common/config.cpp
  common/config.h
  common/error.h
  lib/macro_utils.h
  lib/signal.cpp
  lib/signal.h
  llvmtool/llvm-dwarfdump.cpp
  llvmtool/llvm-dwarfdump.h
  obstack.cpp
  obstack.h
  main.cpp
  )

add_executable(obstack ${source_files})
EXECUTE_PROCESS(COMMAND git log -1 --pretty=format:"%H" OUTPUT_VARIABLE REVISION ERROR_QUIET)
target_compile_definitions(obstack INTERFACE
    _GLIBCXX_USE_CXX11_ABI=0
    LLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING=1
    PUBLIC
    REVISION=${REVISION})
target_compile_options(obstack PRIVATE -fstack-protector-all -fPIC -fPIE -I${CMAKE_CURRENT_SOURCE_DIR} -I${DEP_DIR}/usr/include -I${DEVEL_DIR}/include)

set(LLVM_DIR "${DEVTOOLS_DIR}/lib/cmake/llvm")
find_package(LLVM REQUIRED CONFIG)
EXECUTE_PROCESS(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE)
if(${ARCHITECTURE} STREQUAL "x86_64" )
    LLVM_MAP_COMPONENTS_TO_LIBNAMES(llvm_libs debuginfodwarf x86desc x86info mc object support bitreader mcparser mcdisassembler core binaryformat mc support demangle)
elseif(${ARCHITECTURE} STREQUAL "aarch64" )
    LLVM_MAP_COMPONENTS_TO_LIBNAMES(llvm_libs debuginfodwarf mc object support bitreader mcparser mcdisassembler core binaryformat mc support demangle aarch64info)
else()
    message(FATAL_ERROR "architecture not supported")
endif()
target_link_libraries(obstack
  PRIVATE
  -Wl,--start-group
  -Wl,--end-group
  -Wl,-z,relro,-z,now
  -static-libgcc
  -static-libstdc++
  -pthread
  -ldl -lrt -lz
  -pie
  ${llvm_libs}
  ${DEVTOOLS_DIR}/lib/libbfd.a
  ${DEVTOOLS_DIR}/lib/libiberty.a
  ${DEVEL_DIR}/lib/libelf_pic.a
  ${DEVEL_DIR}/lib/libunwind-ptrace.a
  ${DEVEL_DIR}/lib/libunwind-${ARCHITECTURE}.a
  ${DEVEL_DIR}/lib/libunwind.a
  )
