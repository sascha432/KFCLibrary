Import('env')

# NOTE: this script is not working unless KFCGfx is separated from KFCLibrary
# all *.cpp files have an `#if HAVE_KFCGFXLIB` statement

build_src_filter = ["-<*>"]
for item in env.get("CPPDEFINES", []):
    if isinstance(item, tuple) and item[0] == "HAVE_KFCGFXLIB" and item[1] != 0:
        build_src_filter = ["+<src>"]
        break

print("BUILD_SRC_FILTER")
print(build_src_filter)
env.Replace(BUILD_SRC_FILTER=build_src_filter)

