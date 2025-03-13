add_rules("mode.debug", "mode.release")
set_languages("c++20")

add_requires("glfw", "glm", "assimp")


target("Detex")
    set_kind("static")
    add_includedirs("3rd/Detex/", {public = true})
    add_files("3rd/Detex/*.c")

target("D3D12Ma")
    set_kind("static")
    add_includedirs("3rd/d3d12ma/include", {public = true})
    add_files("3rd/d3d12ma/src/D3D12MemAlloc.cpp")

target("ImGUI")
    set_kind("static")
    add_includedirs("3rd/imgui/", {public = true})
    add_files("3rd/imgui/*.cpp")


target("NRI")
    set_kind("static")
    add_deps("D3D12Ma")
    add_defines("NOMINMAX", "NRI_ENABLE_D3D12_SUPPORT")
    if is_mode("debug") then
        add_defines("NRI_ENABLE_DEBUG_NAMES_AND_ANNOTATIONS")
    end
    add_includedirs("3rd/NRI/Include", {public = true})
    add_includedirs("3rd/NRI/Source/Shared/", {public = true})
    add_includedirs("3rd/d3d12ma/include/", {public = true})
    add_includedirs("3rd/WinPixEventRuntime/Include/WinPixEventRuntime/")
    add_files("3rd/NRI/Source/Creation/*.cpp")
    add_files("3rd/NRI/Source/D3D12/*.cpp")
    add_files("3rd/NRI/Source/Shared/*.cpp")
    add_files("3rd/NRI/Source/NONE/*.cpp")
    add_files("3rd/NRI/Source/Validation/*.cpp")
    add_links("3rd/WinPixEventRuntime/bin/x64/WinPixEventRuntime.lib")
    add_syslinks("dxgi", "d3d12", "dxguid")

target("NRIFramework")
    set_kind("static")
    add_deps("NRI", "ImGUI")
    add_includedirs("3rd/NRI_Framework/Include", {public = true})
    add_includedirs("3rd/NRI/Include", {public = true})
    add_includedirs("3rd/imgui/", {public = true})
    add_includedirs("3rd/", {public = true})
    add_files("3rd/NRI_Framework/Source/*.cpp")
    add_packages("glfw", "glm")

target("DemoApp")
    set_kind("binary")
    add_deps("NRIFramework", "Detex", "NRI", "ImGUI")
    add_includedirs("3rd/NRI_Framework/Include", {public = true})
    add_includedirs("3rd/Detex", {public = true})
    add_packages("glfw", "glm", "assimp")
    add_files("main.cpp")

target("ShaderCompiler")
    set_kind("phony") -- 这里可以是 phony，避免 xmake 生成实际的二进制文件
    set_default(false) -- 让它不在默认 `xmake build` 触发
    add_files("shaders/**.hlsl")

    on_build(function (target)
        import("lib.detect.find_tool")
        local dxc = assert(find_tool("dxc"), "dxc not found!")
        local shaderMakePath = path.join(os.scriptdir(), "3rd/ShaderMake/bin/Release/ShaderMake.exe")
        local shader_output_path = path.join(target:targetdir(), "_Shaders1")
        os.mkdir(shader_output_path)
        local args = {
            "--useAPI", "--binary", "--flatten", "--stripReflection", "--WX",
            "--sRegShift", "100", "--tRegShift", "200", "--bRegShift", "300", "--uRegShift", "400",
            "--sourceDir", path.join(os.scriptdir(), "Shaders"),
            "-c", path.join(os.scriptdir(), "Shaders.cfg"),
            "-o", shader_output_path,
            "-I", path.join(os.scriptdir(), "3rd/NRI/Include"),
            "-p", "DXIL", "--compiler", dxc.program
        }
        os.execv(shaderMakePath, args)
    end)

