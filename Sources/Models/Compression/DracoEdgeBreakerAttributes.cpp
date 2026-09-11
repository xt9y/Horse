#include "DracoEdgeBreakerAttributes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <queue>
#include <unordered_map>
#include <utility>

namespace Models::Compression {
namespace {
#include "DracoEdgeBreakerCore.inl"
#include "DracoEdgeBreakerTraversal.inl"
#include "DracoEdgeBreakerPredictionA.inl"
#include "DracoEdgeBreakerPredictionB.inl"
#include "DracoEdgeBreakerPublic.inl"
} // namespace Models::Compression
