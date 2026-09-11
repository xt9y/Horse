#include "Draco.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Models::Compression {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

class Reader {
public:
    Reader(const std::uint8_t *data, std::size_t size) : data_(data), size_(size) {}
    std::size_t remaining() const { return size_ - pos_; }
    const std::uint8_t *current() const { return data_ + pos_; }
    bool skip(std::size_t n) { if (n > remaining()) return false; pos_ += n; return true; }
    bool bytes(std::size_t n, const std::uint8_t **out) {
        if (!out || n > remaining()) return false;
        *out = data_ + pos_; pos_ += n; return true;
    }
    bool u8(std::uint8_t *out) { if (!out || remaining() < 1u) return false; *out = data_[pos_++]; return true; }
    bool i8(std::int8_t *out) { std::uint8_t v=0; if (!out || !u8(&v)) return false; *out=static_cast<std::int8_t>(v); return true; }
    bool u16(std::uint16_t *out) {
        if (!out || remaining() < 2u) return false;
        *out = static_cast<std::uint16_t>(data_[pos_]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(data_[pos_ + 1u]) << 8u);
        pos_ += 2u; return true;
    }
    bool u32(std::uint32_t *out) {
        if (!out || remaining() < 4u) return false;
        *out = static_cast<std::uint32_t>(data_[pos_]) |
            (static_cast<std::uint32_t>(data_[pos_+1u]) << 8u) |
            (static_cast<std::uint32_t>(data_[pos_+2u]) << 16u) |
            (static_cast<std::uint32_t>(data_[pos_+3u]) << 24u);
        pos_ += 4u; return true;
    }
    bool f32(float *out) {
        std::uint32_t bits=0; if (!out || !u32(&bits)) return false;
        std::memcpy(out,&bits,sizeof(bits)); return true;
    }
    bool f64(double *out) {
        if (!out || remaining() < 8u) return false;
        std::uint64_t bits=0;
        for (unsigned i=0;i<8u;++i) bits |= static_cast<std::uint64_t>(data_[pos_+i]) << (8u*i);
        pos_+=8u; std::memcpy(out,&bits,sizeof(bits)); return true;
    }
    bool var64(std::uint64_t *out) {
        if (!out) return false;
        std::uint64_t value=0; unsigned shift=0;
        for (unsigned i=0;i<10u;++i) {
            std::uint8_t byte=0; if (!u8(&byte)) return false;
            if (shift >= 64u && (byte & 0x7fu)) return false;
            value |= static_cast<std::uint64_t>(byte & 0x7fu) << shift;
            if ((byte & 0x80u)==0u) { *out=value; return true; }
            shift += 7u;
        }
        return false;
    }
    bool var32(std::uint32_t *out) {
        std::uint64_t value=0; if (!var64(&value) || value > UINT32_MAX) return false;
        *out=static_cast<std::uint32_t>(value); return true;
    }
private:
    const std::uint8_t *data_ = nullptr;
    std::size_t size_ = 0u;
    std::size_t pos_ = 0u;
};

bool skipMetadataElement(Reader& r, std::string *error)
{
    std::uint32_t entries=0;
    if (!r.var32(&entries)) return fail(error,"truncated Draco metadata entries");
    for (std::uint32_t i=0;i<entries;++i) {
        std::uint8_t key=0,value=0;
        if (!r.u8(&key) || !r.skip(key) || !r.u8(&value) || !r.skip(value))
            return fail(error,"truncated Draco metadata entry");
    }
    std::uint32_t children=0;
    if (!r.var32(&children)) return fail(error,"truncated Draco metadata children");
    for (std::uint32_t i=0;i<children;++i) {
        std::uint8_t key=0;
        if (!r.u8(&key) || !r.skip(key) || !skipMetadataElement(r,error))
            return fail(error,"truncated Draco submetadata");
    }
    return true;
}

bool skipMetadata(Reader& r, std::string *error)
{
    std::uint32_t count=0;
    if (!r.var32(&count)) return fail(error,"truncated Draco metadata count");
    for (std::uint32_t i=0;i<count;++i) {
        std::uint32_t id=0;
        if (!r.var32(&id) || !skipMetadataElement(r,error)) return false;
        (void)id;
    }
    return skipMetadataElement(r,error);
}

struct Probability { std::uint32_t probability=0u, cumulative=0u; };

class RansDecoder {
public:
    bool init(const std::uint8_t *data, std::size_t size, std::uint32_t base) {
        if (!data || size==0u) return false;
        data_=data;
        const std::uint8_t tag=data[size-1u]>>6u;
        const std::size_t bytes=static_cast<std::size_t>(tag)+1u;
        if (bytes>size) return false;
        offset_=size-bytes;
        std::uint32_t value=0;
        for (std::size_t i=0;i<bytes;++i) value |= static_cast<std::uint32_t>(data[offset_+i]) << (8u*i);
        static constexpr std::uint32_t masks[4] = {0x3fu,0x3fffu,0x3fffffu,0x3fffffffu};
        state_=(value & masks[tag])+base;
        return true;
    }
    bool read(std::uint32_t base, std::uint32_t precision, const std::vector<std::uint32_t>& lookup,
              const std::vector<Probability>& table, std::uint32_t *out) {
        if (!out || precision==0u || lookup.size()<precision) return false;
        while (state_<base && offset_>0u) state_=state_*256u+data_[--offset_];
        const std::uint32_t quotient=state_/precision;
        const std::uint32_t remainder=state_%precision;
        if (remainder>=lookup.size()) return false;
        const std::uint32_t symbol=lookup[remainder];
        if (symbol>=table.size() || table[symbol].probability==0u) return false;
        state_=quotient*table[symbol].probability+remainder-table[symbol].cumulative;
        *out=symbol;
        return true;
    }
private:
    const std::uint8_t *data_=nullptr;
    std::size_t offset_=0u;
    std::uint32_t state_=0u;
};

bool symbolTables(Reader& r, std::uint32_t count, std::uint32_t precision,
                  std::vector<std::uint32_t> *lookup, std::vector<Probability> *table)
{
    if (!lookup || !table || count==0u || precision==0u || count>precision) return false;
    std::vector<std::uint32_t> probs(count,0u);
    for (std::uint32_t i=0;i<count;++i) {
        std::uint8_t first=0; if (!r.u8(&first)) return false;
        const unsigned token=first&3u;
        if (token==3u) {
            const std::uint32_t run=(first>>2u)+1u;
            if (run>count-i) return false;
            i += run-1u;
            continue;
        }
        std::uint32_t probability=first>>2u;
        for (unsigned j=0;j<token;++j) {
            std::uint8_t extra=0; if (!r.u8(&extra)) return false;
            probability |= static_cast<std::uint32_t>(extra) << (8u*(j+1u)-2u);
        }
        probs[i]=probability;
    }
    table->resize(count);
    lookup->assign(precision,0u);
    std::uint64_t cumulative=0u;
    for (std::uint32_t i=0;i<count;++i) {
        (*table)[i]={probs[i],static_cast<std::uint32_t>(cumulative)};
        if (cumulative+probs[i]>precision) return false;
        for (std::uint64_t j=cumulative;j<cumulative+probs[i];++j) (*lookup)[static_cast<std::size_t>(j)]=i;
        cumulative+=probs[i];
    }
    return cumulative==precision;
}

class LsbBits {
public:
    explicit LsbBits(Reader *reader):reader_(reader){}
    bool read(unsigned bits,std::uint32_t *out) {
        if (!reader_ || !out || bits>32u) return false;
        std::uint32_t value=0;
        for (unsigned i=0;i<bits;++i) {
            if (bit_==0u && !reader_->u8(&byte_)) return false;
            value |= static_cast<std::uint32_t>((byte_>>bit_)&1u)<<i;
            bit_=(bit_+1u)&7u;
        }
        *out=value; return true;
    }
private:
    Reader *reader_=nullptr;
    std::uint8_t byte_=0u;
    unsigned bit_=0u;
};

bool decodeSymbols(Reader& r,std::size_t num_values,unsigned components,std::vector<std::uint32_t> *out)
{
    if (!out || components==0u) return false;
    out->assign(num_values,0u);
    if (num_values==0u) return true;
    std::uint8_t scheme=0; if (!r.u8(&scheme)) return false;
    if (scheme==0u) {
        std::uint32_t symbols=0; if (!r.var32(&symbols)) return false;
        std::vector<std::uint32_t> lookup; std::vector<Probability> table;
        if (!symbolTables(r,symbols,4096u,&lookup,&table)) return false;
        std::uint64_t encoded_size=0; if (!r.var64(&encoded_size) || encoded_size>r.remaining()) return false;
        const std::uint8_t *encoded=nullptr; if (!r.bytes(static_cast<std::size_t>(encoded_size),&encoded)) return false;
        RansDecoder decoder; if (!decoder.init(encoded,static_cast<std::size_t>(encoded_size),16384u)) return false;
        LsbBits bits(&r);
        for (std::size_t i=0;i<num_values;i+=components) {
            std::uint32_t bit_length=0;
            if (!decoder.read(16384u,4096u,lookup,table,&bit_length) || bit_length>32u) return false;
            for (unsigned j=0;j<components && i+j<num_values;++j)
                if (!bits.read(bit_length,&(*out)[i+j])) return false;
        }
        return true;
    }
    if (scheme==1u) {
        std::uint8_t max_bits=0; std::uint32_t symbols=0;
        if (!r.u8(&max_bits)||max_bits==0u||max_bits>18u||!r.var32(&symbols)) return false;
        const unsigned precision_bits=std::clamp((3u*static_cast<unsigned>(max_bits))/2u,12u,20u);
        const std::uint32_t precision=1u<<precision_bits;
        std::vector<std::uint32_t> lookup; std::vector<Probability> table;
        if (!symbolTables(r,symbols,precision,&lookup,&table)) return false;
        std::uint64_t encoded_size=0; if (!r.var64(&encoded_size)||encoded_size>r.remaining()) return false;
        const std::uint8_t *encoded=nullptr; if (!r.bytes(static_cast<std::size_t>(encoded_size),&encoded)) return false;
        RansDecoder decoder; if (!decoder.init(encoded,static_cast<std::size_t>(encoded_size),precision*4u)) return false;
        for (std::uint32_t& value:*out) if (!decoder.read(precision*4u,precision,lookup,table,&value)) return false;
        return true;
    }
    return false;
}

bool sequentialConnectivity(Reader& r, DracoMesh *mesh, std::string *error)
{
    std::uint32_t faces=0, points=0;
    std::uint8_t method=0;
    if (!r.var32(&faces) || !r.var32(&points) || !r.u8(&method))
        return fail(error,"truncated Draco sequential connectivity header");
    if (faces > std::numeric_limits<std::size_t>::max()/3u)
        return fail(error,"Draco face count overflows");
    mesh->point_count=points;
    mesh->indices.resize(static_cast<std::size_t>(faces)*3u);
    if (method==0u) {
        std::vector<std::uint32_t> encoded;
        if (!decodeSymbols(r,mesh->indices.size(),1u,&encoded)) return fail(error,"invalid Draco compressed indices");
        std::int64_t last=0;
        for (std::size_t i=0;i<encoded.size();++i) {
            std::int64_t diff=static_cast<std::int64_t>(encoded[i]>>1u);
            if ((encoded[i]&1u)!=0u) diff=-diff;
            const std::int64_t value=last+diff;
            if (value<0 || value>=points) return fail(error,"Draco compressed index exceeds point count");
            mesh->indices[i]=static_cast<std::uint32_t>(value);
            last=value;
        }
        return true;
    }
    if (method==1u) {
        for (std::uint32_t& index: mesh->indices) {
            std::uint32_t value=0;
            if (points < 256u) { std::uint8_t v=0; if(!r.u8(&v)) return fail(error,"truncated Draco UI8 index"); value=v; }
            else if (points < 65536u) { std::uint16_t v=0; if(!r.u16(&v)) return fail(error,"truncated Draco UI16 index"); value=v; }
            else if (points < (1u<<21u)) { if(!r.var32(&value)) return fail(error,"truncated Draco varuint index"); }
            else { if(!r.u32(&value)) return fail(error,"truncated Draco UI32 index"); }
            if (value >= points) return fail(error,"Draco face index exceeds point count");
            index=value;
        }
        return true;
    }
    return fail(error,"unknown Draco sequential connectivity method");
}

std::int32_t zigzag(std::uint32_t value)
{
    return (value&1u)==0u ? static_cast<std::int32_t>(value>>1u) : -static_cast<std::int32_t>((value>>1u)+1u);
}

bool littleUnsigned(Reader& r,unsigned bytes,std::uint32_t *out)
{
    if (!out || bytes==0u || bytes>4u) return false;
    std::uint32_t value=0;
    for (unsigned i=0;i<bytes;++i) {
        std::uint8_t b=0; if (!r.u8(&b)) return false;
        value|=static_cast<std::uint32_t>(b)<<(8u*i);
    }
    *out=value; return true;
}

bool wrapOriginal(std::vector<std::int32_t> *values,unsigned components,std::int32_t minv,std::int32_t maxv)
{
    if (!values || components==0u || minv>maxv) return false;
    const std::int64_t range=1ll+static_cast<std::int64_t>(maxv)-minv;
    std::vector<std::int32_t> result(values->size(),0);
    for (std::size_t i=0;i<values->size();i+=components) {
        for (unsigned c=0;c<components && i+c<values->size();++c) {
            std::int32_t pred=i==0u?0:result[i-components+c];
            pred=std::clamp(pred,minv,maxv);
            std::int64_t original=static_cast<std::int64_t>(pred)+(*values)[i+c];
            if (original>maxv) original-=range; else if (original<minv) original+=range;
            if (original<INT32_MIN||original>INT32_MAX) return false;
            result[i+c]=static_cast<std::int32_t>(original);
        }
    }
    *values=std::move(result); return true;
}

std::size_t dataTypeSize(std::uint8_t type)
{
    switch(type) {
        case 1: case 2: case 11: return 1u;
        case 3: case 4: return 2u;
        case 5: case 6: case 9: return 4u;
        case 7: case 8: case 10: return 8u;
        default: return 0u;
    }
}

bool rawNumber(Reader& r, std::uint8_t type, double *out)
{
    if (!out) return false;
    switch(type) {
        case 1: { std::int8_t v=0; if(!r.i8(&v)) return false; *out=v; return true; }
        case 2: case 11: { std::uint8_t v=0; if(!r.u8(&v)) return false; *out=v; return true; }
        case 3: { std::uint16_t u=0; if(!r.u16(&u)) return false; *out=static_cast<std::int16_t>(u); return true; }
        case 4: { std::uint16_t v=0; if(!r.u16(&v)) return false; *out=v; return true; }
        case 5: { std::uint32_t u=0; if(!r.u32(&u)) return false; *out=static_cast<std::int32_t>(u); return true; }
        case 6: { std::uint32_t v=0; if(!r.u32(&v)) return false; *out=v; return true; }
        case 9: { float v=0; if(!r.f32(&v)) return false; *out=v; return true; }
        case 10: { double v=0; if(!r.f64(&v)) return false; *out=v; return true; }
        default: return false;
    }
}

struct AttributeRecord {
    DracoAttribute output;
    std::uint8_t decoder_type = 0u;
};

void octahedralToUnit(std::int32_t qs,std::int32_t qt,unsigned bits,double *x,double *y,double *z)
{
    const std::int64_t max_quantized=(std::int64_t{1}<<bits)-1;
    const std::int64_t max_value=max_quantized-1;
    if (max_value<=0) { *x=0; *y=0; *z=0; return; }
    double ss=static_cast<double>(qs)/static_cast<double>(max_value);
    double tt=static_cast<double>(qt)/static_cast<double>(max_value);
    double sum=ss+tt,diff=ss-tt,sign=1.0;
    if (!(sum>=0.5&&sum<=1.5&&diff>=-0.5&&diff<=0.5)) {
        sign=-1.0;
        if (sum<=0.5) { const double ns=0.5-tt,nt=0.5-ss;ss=ns;tt=nt; }
        else if (sum>=1.5) { const double ns=1.5-tt,nt=1.5-ss;ss=ns;tt=nt; }
        else if (diff<=-0.5) { const double ns=tt-0.5,nt=ss+0.5;ss=ns;tt=nt; }
        else { const double ns=tt+0.5,nt=ss-0.5;ss=ns;tt=nt; }
        sum=ss+tt; diff=ss-tt;
    }
    const double yy=2.0*ss-1.0, zz=2.0*tt-1.0;
    const double xx=std::min(std::min(2.0*sum-1.0,3.0-2.0*sum),std::min(2.0*diff+1.0,1.0-2.0*diff))*sign;
    double n=xx*xx+yy*yy+zz*zz;
    if (n<1e-12) { *x=0; *y=0; *z=0; return; }
    n=1.0/std::sqrt(n); *x=xx*n;*y=yy*n;*z=zz*n;
}

bool decodeAttributes(Reader& r, DracoMesh *mesh, std::uint8_t encoder_method, std::string *error)
{
    std::uint8_t decoder_count=0;
    if (!r.u8(&decoder_count)) return fail(error,"truncated Draco attribute decoder count");
    std::vector<std::vector<AttributeRecord>> groups(decoder_count);
    if (encoder_method==1u) {
        for (std::size_t i=0;i<groups.size();++i) {
            std::uint8_t data_id=0,type=0,traversal=0;
            if (!r.u8(&data_id)||!r.u8(&type)||!r.u8(&traversal)) return fail(error,"truncated Draco mesh attribute decoder header");
            (void)data_id; (void)type; (void)traversal;
        }
    }
    for (auto& group: groups) {
        std::uint32_t count=0;
        if (!r.var32(&count)) return fail(error,"truncated Draco attribute count");
        group.resize(count);
        for (AttributeRecord& record: group) {
            std::uint8_t normalized=0;
            if (!r.u8(&record.output.attribute_type)||!r.u8(&record.output.data_type)||
                !r.u8(&record.output.components)||!r.u8(&normalized)||!r.var32(&record.output.unique_id))
                return fail(error,"truncated Draco attribute descriptor");
            record.output.normalized = normalized != 0u;
            if (record.output.components==0u || dataTypeSize(record.output.data_type)==0u)
                return fail(error,"invalid Draco attribute layout");
        }
        for (AttributeRecord& record: group)
            if (!r.u8(&record.decoder_type)) return fail(error,"truncated Draco sequential attribute decoder type");
    }

    for (auto& group: groups) {
        for (AttributeRecord& record: group) {
            const unsigned portable_components = record.decoder_type==3u ? 2u : record.output.components;
            if (portable_components==0u || mesh->point_count > std::numeric_limits<std::size_t>::max()/portable_components)
                return fail(error,"Draco portable attribute value count overflows");
            const std::size_t values = static_cast<std::size_t>(mesh->point_count)*portable_components;
            if (record.decoder_type==0u) {
                record.output.values.resize(values);
                for (double& value: record.output.values)
                    if (!rawNumber(r,record.output.data_type,&value)) return fail(error,"truncated Draco generic attribute values");
            } else if (record.decoder_type==1u || record.decoder_type==2u || record.decoder_type==3u) {
                std::int8_t prediction=0;
                if (!r.i8(&prediction)) return fail(error,"truncated Draco prediction scheme");
                std::int8_t transform=0;
                if (prediction!=-2 && !r.i8(&transform)) return fail(error,"truncated Draco prediction transform");
                std::uint8_t compressed=0;
                if (!r.u8(&compressed)) return fail(error,"truncated Draco integer compression flag");
                std::vector<std::uint32_t> encoded(values,0u);
                if (compressed!=0u) {
                    if (!decodeSymbols(r,values,portable_components,&encoded)) return fail(error,"invalid Draco integer symbol stream");
                } else {
                    std::uint8_t bytes=0;
                    if (!r.u8(&bytes)||bytes==0u||bytes>4u) return fail(error,"invalid Draco integer byte width");
                    for (std::uint32_t& value:encoded)
                        if (!littleUnsigned(r,bytes,&value)) return fail(error,"truncated Draco integer data");
                }
                std::vector<std::int32_t> integers(values,0);
                for (std::size_t i=0;i<values;++i) integers[i]=zigzag(encoded[i]);
                if (prediction!=-2) {
                    if (transform!=1) return fail(error,"unsupported Draco integer prediction transform");
                    std::uint32_t min_bits=0,max_bits=0;
                    if (!r.u32(&min_bits)||!r.u32(&max_bits)) return fail(error,"truncated Draco wrap transform");
                    const std::int32_t minv=static_cast<std::int32_t>(min_bits);
                    const std::int32_t maxv=static_cast<std::int32_t>(max_bits);
                    if (prediction==0) {
                        if (!wrapOriginal(&integers,portable_components,minv,maxv)) return fail(error,"invalid Draco difference prediction");
                    } else {
                        return fail(error,"unsupported Draco mesh prediction scheme");
                    }
                }
                if (record.decoder_type==2u) {
                    record.output.values.resize(values);
                    std::vector<float> minimum(record.output.components,0.0f);
                    for (float& v:minimum) if (!r.f32(&v)) return fail(error,"truncated Draco quantization minimum");
                    float range=0.0f; std::uint8_t bits=0;
                    if (!r.f32(&range)||!r.u8(&bits)||bits==0u||bits>31u) return fail(error,"invalid Draco quantization data");
                    const double denominator=static_cast<double>((std::uint64_t{1}<<bits)-1u);
                    for (std::size_t i=0;i<values;++i)
                        record.output.values[i]=minimum[i%record.output.components]+static_cast<double>(integers[i])*static_cast<double>(range)/denominator;
                } else if (record.decoder_type==3u) {
                    if (record.output.components!=3u || record.output.data_type!=9u)
                        return fail(error,"Draco normal decoder requires float32 vec3 output");
                    if (prediction!=-2) return fail(error,"Draco predicted normals are not implemented");
                    std::uint8_t bits=0;
                    if (!r.u8(&bits)||bits<2u||bits>30u) return fail(error,"invalid Draco normal quantization bits");
                    record.output.values.resize(static_cast<std::size_t>(mesh->point_count)*3u);
                    for (std::size_t i=0;i<mesh->point_count;++i) {
                        double x=0,y=0,z=0;
                        octahedralToUnit(integers[i*2u],integers[i*2u+1u],bits,&x,&y,&z);
                        record.output.values[i*3u]=x;record.output.values[i*3u+1u]=y;record.output.values[i*3u+2u]=z;
                    }
                } else {
                    record.output.values.resize(values);
                    for (std::size_t i=0;i<values;++i) record.output.values[i]=integers[i];
                }
            } else {
                return fail(error,"unknown Draco sequential attribute decoder type");
            }
            mesh->attributes.push_back(std::move(record.output));
        }
    }
    return true;
}

} // namespace

const DracoAttribute *DracoMesh::attribute(std::uint32_t unique_id) const
{
    for (const DracoAttribute& a: attributes) if (a.unique_id==unique_id) return &a;
    return nullptr;
}

bool decodeDraco(const std::uint8_t *data, std::size_t size, DracoMesh *mesh, std::string *error)
{
    if (error) error->clear();
    if (!data || !mesh || size < 11u) return fail(error,"truncated Draco stream");
    *mesh={};
    Reader r(data,size);
    if (r.remaining()<5u || std::memcmp(r.current(),"DRACO",5u)!=0) return fail(error,"invalid Draco magic");
    r.skip(5u);
    std::uint8_t major=0,minor=0,type=0,method=0; std::uint16_t flags=0;
    if (!r.u8(&major)||!r.u8(&minor)||!r.u8(&type)||!r.u8(&method)||!r.u16(&flags))
        return fail(error,"truncated Draco header");
    if (major!=2u || minor>2u) return fail(error,"unsupported Draco bitstream version");
    if (type!=1u) return fail(error,"Draco stream is not a triangular mesh");
    if ((flags & 0x8000u)!=0u && !skipMetadata(r,error)) return false;
    if (method!=0u) return fail(error,"Draco EdgeBreaker connectivity not implemented");
    if (!sequentialConnectivity(r,mesh,error)) return false;
    return decodeAttributes(r,mesh,method,error);
}

} // namespace Models::Compression
