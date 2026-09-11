#include "DracoSequentialComplete.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Models::Compression {
namespace {

#include "DracoSequentialCore.inl"
#include "DracoSequentialTopology.inl"
#include "DracoSequentialPrediction.inl"
#include "DracoSequentialDecode.inl"

bool decodeDracoSequentialComplete(const std::uint8_t *data,std::size_t size,DracoMesh *mesh,std::string *error){
    if (error) error->clear();
    if (!data || !mesh || size < 11u) return fail(error, "truncated Draco stream");
    *mesh = {};
    Reader r(data, size);
    if (r.remaining() < 5u || std::memcmp(r.current(), "DRACO", 5u) != 0) return fail(error, "invalid Draco magic");
    r.skip(5u);
    std::uint8_t major=0,minor=0,type=0,method=0;std::uint16_t flags=0;if(!r.u8(&major)||!r.u8(&minor)||!r.u8(&type)||!r.u8(&method)||!r.u16(&flags))return fail(error,"truncated Draco header");if(major!=2u||minor>2u)return fail(error,"unsupported Draco bitstream version");if(type!=1u||method!=0u)return fail(error,"Draco stream is not a sequential triangular mesh");if((flags&0x8000u)!=0u&&!skipMetadata(r,error))return false;if(!sequentialConnectivity(r,mesh,error))return false;
    Topology topology;if(!buildTopology(*mesh,&topology))return fail(error,"invalid Draco topology");std::uint8_t decoder_count=0;if(!r.u8(&decoder_count))return fail(error,"truncated Draco attribute decoder count");std::vector<std::vector<Record>> groups(decoder_count);
    for(auto& group:groups){std::uint32_t count=0;if(!r.var32(&count)||count==0u)return fail(error,"invalid Draco attribute count");group.resize(count);for(Record& rec:group){std::uint8_t normalized=0;if(!r.u8(&rec.output.attribute_type)||!r.u8(&rec.output.data_type)||!r.u8(&rec.output.components)||!r.u8(&normalized)||!r.var32(&rec.output.unique_id))return fail(error,"truncated Draco attribute descriptor");rec.output.normalized=normalized!=0u;if(rec.output.components==0u||dataTypeSize(rec.output.data_type)==0u)return fail(error,"invalid Draco attribute layout");}for(Record& rec:group)if(!r.u8(&rec.decoder_type)||rec.decoder_type>3u)return fail(error,"invalid Draco sequential attribute decoder type");}
    std::vector<Record> decoded;for(auto& group:groups){if(!decodeGroupPortable(r,topology,decoded,&group,error)||!applyTransforms(r,&group,error))return false;for(auto& rec:group){decoded.push_back(rec);mesh->attributes.push_back(std::move(rec.output));}}
    return true;
}

} // namespace Models::Compression
