# ThreadSanitizer needs its runtime at link time too (PlatformIO passes -fsanitize only to the compiler).
Import("env")  # noqa: F821
env.Append(LINKFLAGS=["-fsanitize=thread"])  # noqa: F821
