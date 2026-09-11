#include "DracoEdgeBreakerValence.hpp"
#include "DracoEdgeBreakerAttributes.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace Models::Compression {
namespace {

#include "DracoSequentialCore.inl"

[[maybe_unused]] void retainSequentialCoreHelpers()
{
    (void)&readPredictionBits;
    (void)&zigzag;
    (void)&littleUnsigned;
    (void)&dataTypeSize;
    (void)&rawNumber;
}

#include "DracoEdgeBreakerValenceCore.inl"
#include "DracoEdgeBreakerValenceTopology.inl"

bool decodeDracoEdgeBreakerValence(const std::uint8_t* data,std::size_t size,DracoMesh* mesh,std::string* error){
    if(error) error->clear();
    if(!data||!mesh||size<12u) return fail(error,"truncated Draco stream");
    *mesh={};
    Reader r(data,size);
    if(r.remaining()<5u||std::memcmp(r.current(),"DRACO",5u)!=0) return fail(error,"invalid Draco magic");
    r.skip(5u);
    std::uint8_t major=0,minor=0,type=0,method=0;std::uint16_t flags=0;if(!r.u8(&major)||!r.u8(&minor)||!r.u8(&type)||!r.u8(&method)||!r.u16(&flags))return fail(error,"truncated Draco header");if(major!=2u||minor!=2u)return fail(error,"valence decoder requires Draco 2.2");if(type!=1u||method!=1u)return fail(error,"not an EdgeBreaker triangular mesh");if((flags&0x8000u)!=0u&&!skipMetadata(r,error))return false;
    std::uint8_t traversal=0;if(!r.u8(&traversal)||traversal!=2u)return fail(error,"not a Draco valence traversal");std::uint32_t vertices=0,faces=0,symbols=0,splits_count=0;std::uint8_t attribute_data=0;if(!r.var32(&vertices)||!r.var32(&faces)||!r.u8(&attribute_data)||!r.var32(&symbols)||!r.var32(&splits_count))return fail(error,"truncated Draco EdgeBreaker counts");if(splits_count>symbols||symbols>faces||vertices>faces*3u)return fail(error,"invalid Draco EdgeBreaker counts");std::vector<SplitEvent> splits;if(!parseSplits(r,&splits,error))return false;
    ValenceTraversal tr;if(!tr.start_faces.start(r))return fail(error,"invalid Draco start-face stream");tr.seams.resize(attribute_data);for(auto& seam:tr.seams)if(!seam.start(r))return fail(error,"invalid Draco seam stream");for(std::size_t i=0;i<6u;++i){std::uint32_t n=0;if(!r.var32(&n)||n>faces)return fail(error,"invalid Draco valence context count");if(n&&!decodeSymbols(r,n,1u,&tr.contexts[i]))return fail(error,"invalid Draco valence context symbols");tr.counters[i]=tr.contexts[i].size();}
    CornerTable table;if(!decodeTopology(symbols,faces,vertices,splits_count,splits,tr,&table,error))return false;DracoEdgeBreakerTopology topology;topology.position_vertex_count=static_cast<std::uint32_t>(table.vertices);topology.corner_vertices.resize(table.corner_vertex.size());topology.opposite.resize(table.opposite.size());for(std::size_t i=0;i<table.corner_vertex.size();++i){if(table.corner_vertex[i]<0||static_cast<std::uint32_t>(table.corner_vertex[i])>=topology.position_vertex_count)return fail(error,"invalid Draco reconstructed vertex");topology.corner_vertices[i]=static_cast<std::uint32_t>(table.corner_vertex[i]);topology.opposite[i]=static_cast<std::int32_t>(table.opposite[i]);}if(!decodeSeams(table,tr,&topology,error))return false;return decodeDracoEdgeBreakerAttributes(r.current(),r.remaining(),topology,mesh,error);
}

} // namespace Models::Compression
