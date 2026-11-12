# 示例插件：SHARED 库，编译期引用本扩展安装的 ABI 头文件
# 仅测试使用，不安装（测试程序按构建目录路径 dlopen 加载）
add_library(TestPlugin SHARED
    TestPlugin.cpp
)
target_link_libraries(TestPlugin PRIVATE YomkServer::YomkServer)

# 第二个可加载插件：meta 名不同，供跨插件同名实例/限定名用例使用
add_library(SecondPlugin SHARED
    SecondPlugin.cpp
)
target_link_libraries(SecondPlugin PRIVATE YomkServer::YomkServer)
