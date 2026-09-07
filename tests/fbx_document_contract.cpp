#include "Models/Formats/FbxDocument.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

int main()
{
    using namespace Models::FbxDocument;

    Property integer{'L', std::int64_t{42}};
    Property real{'D', 3.5};
    Property text{'S', std::string{"hello"}};
    Property array{'d', std::vector<double>{1.0,2.0,3.0}};
    Property bytes{'R', Bytes{1u,2u,3u}};

    assert(integer.asInt64(-1) == 42);
    assert(real.asDouble(-1.0) == 3.5);
    assert(text.asString("x") == "hello");
    assert(text.asInt64(99) == 99);
    assert(array.asDoubleArray() && array.asDoubleArray()->size() == 3u);
    assert(bytes.asBytes() && bytes.asBytes()->at(2) == 3u);

    Node root;
    root.children.push_back(Node{"A", {integer}, {}});
    root.children.push_back(Node{"A", {real}, {}});
    root.children.push_back(Node{"B", {array}, {}});
    assert(root.child("A"));
    assert(root.childrenNamed("A").size() == 2u);
    assert(root.child("B")->numericArray().size() == 3u);
    return 0;
}
