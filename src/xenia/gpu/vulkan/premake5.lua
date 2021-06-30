project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-gpu-vulkan")
  uuid("717590b4-f579-4162-8f23-0624e87d6cca")
  kind("StaticLib")
  language("C++")
  links({
    "fmt",
    "volk",
    "xenia-base",
    "xenia-gpu",
    "xenia-ui",
    "xenia-ui-spirv",
    "xenia-ui-vulkan",
    "xxhash",
  })
  defines({
  })
  local_platform_files()
  files({
    "shaders/bin/*.h",
  })

-- TODO(benvanik): kill this and move to the debugger UI.
group("src")
project("xenia-gpu-vulkan-trace-viewer")
  uuid("86a1dddc-a26a-4885-8c55-cf745225d93e")
  kind("WindowedApp")
  language("C++")
  links({
    "xenia-apu",
    "xenia-apu-nop",
    "xenia-base",
    "xenia-core",
    "xenia-cpu",
    "xenia-cpu-backend-x64",
    "xenia-gpu",
    "xenia-gpu-vulkan",
    "xenia-hid",
    "xenia-hid-nop",
    "xenia-kernel",
    "xenia-ui",
    "xenia-ui-spirv",
    "xenia-ui-vulkan",
    "xenia-vfs",
  })
  links({
    "aes_128",
    "capstone",
    "fmt",
    "glslang-spirv",
    "imgui",
    "libavcodec",
    "libavutil",
    "mspack",
    "snappy",
    "spirv-tools",
    "volk",
    "xxhash",
  })
  defines({
  })
  files({
    "vulkan_trace_viewer_main.cc",
    "../../base/main_"..platform_suffix..".cc",
  })

  filter("platforms:Linux")
    links({
      "X11",
      "xcb",
      "X11-xcb",
    })

  filter("platforms:Windows")
    links({
      "xenia-apu-xaudio2",
      "xenia-hid-winkey",
      "xenia-hid-xinput",
    })

    -- Only create the .user file if it doesn't already exist.
    local user_file = project_root.."/build/xenia-gpu-vulkan-trace-viewer.vcxproj.user"
    if not os.isfile(user_file) then
      debugdir(project_root)
      debugargs({
        "2>&1",
        "1>scratch/stdout-trace-viewer.txt",
      })
    end

group("src")
project("xenia-gpu-vulkan-trace-dump")
  uuid("0dd0dd1c-b321-494d-ab9a-6c062f0c65cc")
  kind("ConsoleApp")
  language("C++")
  links({
    "xenia-apu",
    "xenia-apu-nop",
    "xenia-base",
    "xenia-core",
    "xenia-cpu",
    "xenia-cpu-backend-x64",
    "xenia-gpu",
    "xenia-gpu-vulkan",
    "xenia-hid",
    "xenia-hid-nop",
    "xenia-kernel",
    "xenia-ui",
    "xenia-ui-spirv",
    "xenia-ui-vulkan",
    "xenia-vfs",
  })
  links({
    "aes_128",
    "capstone",
    "fmt",
    "glslang-spirv",
    "imgui",
    "libavcodec",
    "libavutil",
    "mspack",
    "snappy",
    "spirv-tools",
    "volk",
    "xxhash",
  })
  defines({
  })
  files({
    "vulkan_trace_dump_main.cc",
    "../../base/main_"..platform_suffix..".cc",
  })

  filter("platforms:Linux")
    links({
      "X11",
      "xcb",
      "X11-xcb",
    })

  filter("platforms:Windows")
    -- Only create the .user file if it doesn't already exist.
    local user_file = project_root.."/build/xenia-gpu-vulkan-trace-dump.vcxproj.user"
    if not os.isfile(user_file) then
      debugdir(project_root)
      debugargs({
        "2>&1",
        "1>scratch/stdout-trace-dump.txt",
      })
    end
