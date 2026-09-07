#ifndef RW_ENGINE_MODELS_FORMATS_FBX_PARSER_HPP
#define RW_ENGINE_MODELS_FORMATS_FBX_PARSER_HPP

#include "Models/Formats/FbxDocument.hpp"

// Transitional source-compatibility alias for the unchanged semantic importer.
// Parsing is implemented by FbxBinary/FbxAscii and dispatched by FbxDocument.
namespace Models {
namespace FbxParser = FbxDocument;
}

#endif
