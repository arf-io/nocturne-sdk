#include <nocturne/abi.h>

extern "C" int nocturne_abi_conformance(void);
static_assert(NT_ABI_VERSION == 1, "C++ must read the same ABI version as C");

int main()
{
    return nocturne_abi_conformance();
}
