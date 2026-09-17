#include <Models/Internal/ModelCacheBinary.hpp>

#include <cassert>
#include <cstddef>

int main()
{
    Models::Internal::ModelCacheBinary::Writer writer;
    assert(writer.count(1024u));

    const auto& bytes = writer.bytes();
    Models::Internal::ModelCacheBinary::Reader reader(bytes);
    std::size_t count = 0u;

    assert(!reader.count(&count));
    assert(!reader.good());
    return 0;
}
