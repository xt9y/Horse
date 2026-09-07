#include <cassert>
#include <filesystem>

int main()
{
    namespace fs = std::filesystem;

    assert(fs::exists("Sources/Models/Formats/FBX/Fbx.cpp"));
    assert(fs::exists("Sources/Models/Formats/FBX/Ascii.cpp"));
    assert(fs::exists("Sources/Models/Formats/FBX/Binary.cpp"));
    assert(fs::exists("Sources/Models/Formats/FBX/Document.cpp"));
    assert(fs::exists("Sources/Models/Formats/FBX/Sanitize.cpp"));
    assert(fs::exists("Sources/Models/Formats/OBJ/Obj.cpp"));

    assert(!fs::exists("Sources/Models/Formats/Fbx.cpp"));
    assert(!fs::exists("Sources/Models/Formats/FbxParser.hpp"));
    assert(!fs::exists("Sources/Models/Formats/Obj.cpp"));

    return 0;
}
