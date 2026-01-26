#这里选择你需要编译的项目
if(CONFIG_EXAMPLE_BUILD_LAN8671)
    set(project_path "examples/lan8671/")
elseif(CONFIG_EXAMPLE_BUILD_WIFI)
    set(project_path "examples/wifi/")
endif()
        
idf_component_register(SRC_DIRS ${project_path} 
                        INCLUDE_DIRS ${project_path})

# target_compile_options(
#     ${COMPONENT_LIB}
#     PRIVATE -Wno-error=unused-const-variable -Wno-error=delete-non-virtual-dtor
#             -Wno-error=implicit-function-declaration -fpermissive)