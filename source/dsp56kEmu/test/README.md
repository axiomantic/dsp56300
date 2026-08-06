# `dsp56kEmu` per-task tests

This directory holds the tests of the Nord Modular G2 emulator work on this
fork. Upstream keeps its tests flat in `source/dsp56kEmu/` and drives all of
them from one registration, `add_test(NAME dsp56300_unitTests COMMAND
dsp56kTestRunner)` in `source/dsp56kTestRunner/CMakeLists.txt`. This directory
is a divergence from that layout. It can be removed if upstream adopts the
directory.

## The rules this directory obeys

1. **One source file, one executable, one registered name.** `CMakeLists.txt`
   gives the helper `dsp56k_add_test(<name>)`, which builds `<name>.cpp` into
   the target `<name>` and registers `add_test(NAME <name> COMMAND <name>)`.
   Two names must never point at one program.
2. **Every registered name starts with `dsp56k_`.** That prefix belongs to this
   directory. Nothing else in the build tree uses it.
3. **A test must be able to fail, and the failure must survive `NDEBUG`.** Use
   the `verify(...)` macro from `dsp56kEmu/unittests.h`. It throws a
   `std::string`, which a `catch` in `main` turns into a non-zero exit status.
   A bare `assert()` compiles to nothing under `NDEBUG`; the test then prints
   "passed" and returns 0. Do not use one.
4. **A compile-time property is asserted with `static_assert`.** The failure
   mode is then a build error, which is a real failure. State the property in
   the message.
5. **No `SKIP_RETURN_CODE`.** Every test here is T0: it needs no firmware and
   no recorded audio, so it has nothing to skip for.

## The shape of a test

```cpp
#include "dsp56kEmu/unittests.h"

#include <iostream>

namespace
{
    void run()
    {
        verify(2 + 2 == 4);
    }
}

int main()
{
    try
    {
        run();
    }
    catch(const std::string& _err)
    {
        std::cout << "FAILED: " << _err << std::endl;
        return -1;
    }
    std::cout << "passed" << std::endl;
    return 0;
}
```
