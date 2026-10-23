# The sanitizers need their runtime at link time too (PlatformIO passes -fsanitize only to the compiler).
Import("env")  # noqa: F821
env.Append(LINKFLAGS=["-fsanitize=address,undefined,leak", "-fno-sanitize-recover=all"])  # noqa: F821
