project_root = "../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-patcher")
  uuid("e1c75f76-9e7b-48f6-b17e-dbd20f7a1592")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base"
  })
  defines({
  })
  recursive_platform_files()
