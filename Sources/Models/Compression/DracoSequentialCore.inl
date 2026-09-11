bool fail(std::string *error, const std::string& message) {
    if (error) *error = message;
    return false;
}

class Reader {
public:
    Reader(const std::uint8_t *data, std::size_t size) : data_(data), size_(size) {}
    std::size_t remaining() const { return size_ - pos_; }
    const std::uint8_t *current() const { return data_ + pos_; }
    bool skip(std::size_t count) { if (count > remaining()) return false; pos_ += count; return true; }
    bool bytes(std::size_t count, const std::uint8_t **out) {
        if (!out || count > remaining()) return false;
        *out = data_ + pos_; pos_ += count; return true;
    }
    bool u8(std::uint8_t *out) { if (!out || remaining() < 1u) return false; *out = data_[pos_++]; return true; }
    bool i8(std::int8_t *out) { std::uint8_t v = 0; if (!out || !u8(&v)) return false; *out = static_cast<std::int8_t>(v); return true; }
    bool u16(std::uint16_t *out) {
        if (!out || remaining() < 2u) return false;
        *out = static_cast<std::uint16_t>(data_[pos_]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(data_[pos_ + 1u]) << 8u);
        pos_ += 2u; return true;
    }
    bool u32(std::uint32_t *out) {
        if (!out || remaining() < 4u) return false;
        *out = static_cast<std::uint32_t>(data_[pos_]) |
            (static_cast<std::uint32_t>(data_[pos_ + 1u]) << 8u) |
            (static_cast<std::uint32_t>(data_[pos_ + 2u]) << 16u) |
            (static_cast<std::uint32_t>(data_[pos_ + 3u]) << 24u);
        pos_ += 4u; return true;
    }
    bool i32(std::int32_t *out) { std::uint32_t v=0; if (!out || !u32(&v)) return false; *out = static_cast<std::int32_t>(v); return true; }
    bool f32(float *out) { std::uint32_t bits=0; if (!out || !u32(&bits)) return false; std::memcpy(out,&bits,sizeof(bits)); return true; }
    bool f64(double *out) {
        if (!out || remaining() < 8u) return false;
        std::uint64_t bits=0u;
        for (unsigned i=0;i<8u;++i) bits |= static_cast<std::uint64_t>(data_[pos_+i]) << (8u*i);
        pos_ += 8u; std::memcpy(out,&bits,sizeof(bits)); return true;
    }
    bool var64(std::uint64_t *out) {
        if (!out) return false;
        std::uint64_t value=0u; unsigned shift=0u;
        for (unsigned i=0;i<10u;++i) {
            std::uint8_t byte=0u; if (!u8(&byte)) return false;
            if (shift >= 64u && (byte & 0x7fu) != 0u) return false;
            value |= static_cast<std::uint64_t>(byte & 0x7fu) << shift;
            if ((byte & 0x80u) == 0u) { *out=value; return true; }
            shift += 7u;
        }
        return false;
    }
    bool var32(std::uint32_t *out) { std::uint64_t v=0u; if (!var64(&v) || v > UINT32_MAX) return false; *out=static_cast<std::uint32_t>(v); return true; }
private:
    const std::uint8_t *data_ = nullptr;
    std::size_t size_ = 0u;
    std::size_t pos_ = 0u;
};

bool skipMetadataElement(Reader& r, std::string *error) {
    std::uint32_t entries=0; if (!r.var32(&entries)) return fail(error,"truncated Draco metadata entries");
    for (std::uint32_t i=0;i<entries;++i) {
        std::uint8_t key=0,value=0;
        if (!r.u8(&key) || !r.skip(key) || !r.u8(&value) || !r.skip(value)) return fail(error,"truncated Draco metadata entry");
    }
    std::uint32_t children=0; if (!r.var32(&children)) return fail(error,"truncated Draco metadata children");
    for (std::uint32_t i=0;i<children;++i) {
        std::uint8_t key=0; if (!r.u8(&key) || !r.skip(key) || !skipMetadataElement(r,error)) return fail(error,"truncated Draco submetadata");
    }
    return true;
}

bool skipMetadata(Reader& r, std::string *error) {
    std::uint32_t count=0; if (!r.var32(&count)) return fail(error,"truncated Draco metadata count");
    for (std::uint32_t i=0;i<count;++i) { std::uint32_t id=0; if (!r.var32(&id) || !skipMetadataElement(r,error)) return false; }
    return skipMetadataElement(r,error);
}

struct Probability { std::uint32_t probability=0u, cumulative=0u; };
class RansDecoder {
public:
    bool init(const std::uint8_t *data,std::size_t size,std::uint32_t base) {
        if (!data || size==0u) return false;
        data_=data; const std::uint8_t tag=data[size-1u]>>6u; const std::size_t bytes=static_cast<std::size_t>(tag)+1u;
        if (bytes > size) return false;
        offset_ = size - bytes;
        std::uint32_t value = 0u;
        for (std::size_t i=0;i<bytes;++i) value |= static_cast<std::uint32_t>(data[offset_+i]) << (8u*i);
        static constexpr std::uint32_t masks[4]={0x3fu,0x3fffu,0x3fffffu,0x3fffffffu};
        state_=(value&masks[tag])+base; return true;
    }
    bool read(std::uint32_t base,std::uint32_t precision,const std::vector<std::uint32_t>& lookup,const std::vector<Probability>& table,std::uint32_t *out) {
        if (!out || precision==0u || lookup.size()<precision) return false;
        while (state_<base && offset_>0u) state_=state_*256u+data_[--offset_];
        const std::uint32_t q=state_/precision, rem=state_%precision;
        if (rem >= lookup.size()) return false;
        const std::uint32_t symbol = lookup[rem];
        if (symbol>=table.size() || table[symbol].probability==0u) return false;
        state_=q*table[symbol].probability+rem-table[symbol].cumulative; *out=symbol; return true;
    }
private: const std::uint8_t *data_=nullptr; std::size_t offset_=0u; std::uint32_t state_=0u;
};

bool symbolTables(Reader& r,std::uint32_t count,std::uint32_t precision,std::vector<std::uint32_t> *lookup,std::vector<Probability> *table) {
    if (!lookup||!table||count==0u||precision==0u||count>precision) return false;
    std::vector<std::uint32_t> probabilities(count,0u);
    for (std::uint32_t i=0;i<count;++i) {
        std::uint8_t first=0; if(!r.u8(&first)) return false; const unsigned token=first&3u;
        if (token==3u) { const std::uint32_t run=(first>>2u)+1u; if(run>count-i) return false; i+=run-1u; continue; }
        std::uint32_t probability=first>>2u;
        for (unsigned j=0;j<token;++j) { std::uint8_t extra=0; if(!r.u8(&extra)) return false; probability|=static_cast<std::uint32_t>(extra)<<(8u*(j+1u)-2u); }
        probabilities[i]=probability;
    }
    table->resize(count); lookup->assign(precision,0u); std::uint64_t cumulative=0u;
    for (std::uint32_t i=0;i<count;++i) {
        (*table)[i]={probabilities[i],static_cast<std::uint32_t>(cumulative)};
        if (cumulative+probabilities[i]>precision) return false;
        for (std::uint64_t j=cumulative;j<cumulative+probabilities[i];++j) (*lookup)[static_cast<std::size_t>(j)]=i;
        cumulative += probabilities[i];
    }
    return cumulative==precision;
}

class LsbBits {
public:
    explicit LsbBits(Reader *reader):reader_(reader){}
    bool read(unsigned bits,std::uint32_t *out) {
        if (!reader_ || !out || bits > 32u) return false;
        std::uint32_t value = 0u;
        for(unsigned i=0;i<bits;++i) { if(bit_==0u && !reader_->u8(&byte_)) return false; value|=static_cast<std::uint32_t>((byte_>>bit_)&1u)<<i; bit_=(bit_+1u)&7u; }
        *out=value; return true;
    }
private: Reader *reader_=nullptr; std::uint8_t byte_=0u; unsigned bit_=0u;
};

bool decodeSymbols(Reader& r,std::size_t num_values,unsigned components,std::vector<std::uint32_t> *out) {
    if (!out || components == 0u) return false;
    out->assign(num_values, 0u);
    if (num_values == 0u) return true;
    std::uint8_t scheme=0; if(!r.u8(&scheme)) return false;
    if(scheme==0u) {
        std::uint32_t symbols=0; if(!r.var32(&symbols)) return false; std::vector<std::uint32_t> lookup; std::vector<Probability> table;
        if(!symbolTables(r,symbols,4096u,&lookup,&table)) return false;
        std::uint64_t encoded_size=0; if(!r.var64(&encoded_size)||encoded_size>r.remaining()) return false;
        const std::uint8_t *encoded=nullptr; if(!r.bytes(static_cast<std::size_t>(encoded_size),&encoded)) return false;
        RansDecoder decoder; if(!decoder.init(encoded,static_cast<std::size_t>(encoded_size),16384u)) return false; LsbBits bits(&r);
        for(std::size_t i=0;i<num_values;i+=components) { std::uint32_t bit_length=0; if(!decoder.read(16384u,4096u,lookup,table,&bit_length)||bit_length>32u) return false; for(unsigned j=0;j<components&&i+j<num_values;++j) if(!bits.read(bit_length,&(*out)[i+j])) return false; }
        return true;
    }
    if(scheme==1u) {
        std::uint8_t max_bits=0; std::uint32_t symbols=0; if(!r.u8(&max_bits)||max_bits==0u||max_bits>18u||!r.var32(&symbols)) return false;
        const unsigned precision_bits=std::clamp((3u*static_cast<unsigned>(max_bits))/2u,12u,20u); const std::uint32_t precision=1u<<precision_bits;
        std::vector<std::uint32_t> lookup; std::vector<Probability> table; if(!symbolTables(r,symbols,precision,&lookup,&table)) return false;
        std::uint64_t encoded_size=0; if(!r.var64(&encoded_size)||encoded_size>r.remaining()) return false; const std::uint8_t *encoded=nullptr; if(!r.bytes(static_cast<std::size_t>(encoded_size),&encoded)) return false;
        RansDecoder decoder; if(!decoder.init(encoded,static_cast<std::size_t>(encoded_size),precision*4u)) return false; for(std::uint32_t& value:*out) if(!decoder.read(precision*4u,precision,lookup,table,&value)) return false; return true;
    }
    return false;
}

class RansBit {
public:
    bool start(Reader& r) {
        if (!r.u8(&probability_zero_)) return false;
        std::uint32_t size = 0u;
        if (!r.var32(&size) || size == 0u || size > r.remaining()) return false;
        const std::uint8_t *data=nullptr; if(!r.bytes(size,&data)) return false; data_=data; const std::uint8_t tag=data[size-1u]>>6u;
        const std::size_t bytes=static_cast<std::size_t>(tag)+1u; if(tag==3u||bytes>size) return false; offset_=size-bytes; std::uint32_t value=0u;
        for (std::size_t i = 0u; i < bytes; ++i) value |= static_cast<std::uint32_t>(data[offset_ + i]) << (8u * i);
        static constexpr std::uint32_t masks[3] = {0x3fu, 0x3fffu, 0x3fffffu};
        state_=(value&masks[tag])+4096u; return state_<4096u*256u;
    }
    bool bit(bool *out) {
        if (!out) return false;
        if (state_ < 4096u) { if (offset_ == 0u) return false; state_ = state_ * 256u + data_[--offset_]; }
        const std::uint32_t p1=256u-probability_zero_,q=state_/256u,rem=state_%256u,one=q*p1; const bool v=rem<p1; state_=v?one+rem:state_-one-p1; *out=v; return true;
    }
private: const std::uint8_t *data_=nullptr; std::size_t offset_=0u; std::uint32_t state_=0u; std::uint8_t probability_zero_=0u;
};

bool readPredictionBits(Reader& r,std::size_t count,std::vector<std::uint8_t> *out) {
    if (!out) return false;
    out->assign(count, 0u);
    if (count == 0u) return true;
    RansBit decoder;
    if (!decoder.start(r)) return false;
    for(std::size_t i=0;i<count;++i){bool bit=false;if(!decoder.bit(&bit))return false;(*out)[i]=bit?1u:0u;} return true;
}

std::int32_t zigzag(std::uint32_t value) { return (value&1u)==0u ? static_cast<std::int32_t>(value>>1u) : -static_cast<std::int32_t>((value>>1u)+1u); }
bool littleUnsigned(Reader& r,unsigned bytes,std::uint32_t *out) { if(!out||bytes==0u||bytes>4u)return false;std::uint32_t v=0u;for(unsigned i=0;i<bytes;++i){std::uint8_t b=0;if(!r.u8(&b))return false;v|=static_cast<std::uint32_t>(b)<<(8u*i);}*out=v;return true; }

std::size_t dataTypeSize(std::uint8_t type) { switch(type){case 1:case 2:case 11:return 1u;case 3:case 4:return 2u;case 5:case 6:case 9:return 4u;case 7:case 8:case 10:return 8u;default:return 0u;} }
bool rawNumber(Reader& r,std::uint8_t type,double *out) {
    if (!out) return false;
    switch(type){
        case 1:{std::int8_t v=0;if(!r.i8(&v))return false;*out=v;return true;} case 2:case 11:{std::uint8_t v=0;if(!r.u8(&v))return false;*out=v;return true;}
        case 3:{std::uint16_t v=0;if(!r.u16(&v))return false;*out=static_cast<std::int16_t>(v);return true;} case 4:{std::uint16_t v=0;if(!r.u16(&v))return false;*out=v;return true;}
        case 5:{std::uint32_t v=0;if(!r.u32(&v))return false;*out=static_cast<std::int32_t>(v);return true;} case 6:{std::uint32_t v=0;if(!r.u32(&v))return false;*out=v;return true;}
        case 9:{float v=0;if(!r.f32(&v))return false;*out=v;return true;} case 10:{double v=0;if(!r.f64(&v))return false;*out=v;return true;} default:return false;
    }
}
