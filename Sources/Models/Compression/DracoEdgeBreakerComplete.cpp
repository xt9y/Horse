#include "DracoEdgeBreakerComplete.hpp"
#include "DracoEdgeBreakerAttributes.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace Models::Compression {
namespace {

bool fail(std::string *error,const std::string& message){if(error)*error=message;return false;}

class Reader {
public:
    Reader(const std::uint8_t *data,std::size_t size):data_(data),size_(size){}
    std::size_t remaining()const{return size_-position_;}
    const std::uint8_t *current()const{return data_+position_;}
    bool skip(std::size_t count){if(count>remaining())return false;position_+=count;return true;}
    bool u8(std::uint8_t *out){if(!out||remaining()<1u)return false;*out=data_[position_++];return true;}
    bool u16(std::uint16_t *out){if(!out||remaining()<2u)return false;*out=static_cast<std::uint16_t>(data_[position_])|static_cast<std::uint16_t>(static_cast<std::uint16_t>(data_[position_+1u])<<8u);position_+=2u;return true;}
    bool var64(std::uint64_t *out){if(!out)return false;std::uint64_t value=0u;unsigned shift=0u;for(unsigned i=0u;i<10u;++i){std::uint8_t byte=0u;if(!u8(&byte))return false;if(shift>=64u&&(byte&0x7fu)!=0u)return false;value|=static_cast<std::uint64_t>(byte&0x7fu)<<shift;if((byte&0x80u)==0u){*out=value;return true;}shift+=7u;}return false;}
    bool var32(std::uint32_t *out){std::uint64_t value=0u;if(!var64(&value)||value>UINT32_MAX)return false;*out=static_cast<std::uint32_t>(value);return true;}
    bool bytes(std::size_t count,const std::uint8_t **out){if(!out||count>remaining())return false;*out=data_+position_;position_+=count;return true;}
private:
    const std::uint8_t *data_=nullptr;std::size_t size_=0u,position_=0u;
};

class Bits {
public:
    Bits()=default;Bits(const std::uint8_t *data,std::size_t size):data_(data),size_(size){}
    bool read(unsigned count,std::uint32_t *out){if(!out||count>32u||count>available())return false;std::uint32_t value=0u;for(unsigned i=0u;i<count;++i){value|=static_cast<std::uint32_t>((data_[bit_>>3u]>>(bit_&7u))&1u)<<i;++bit_;}*out=value;return true;}
    std::size_t bytesUsed()const{return(bit_+7u)/8u;}std::size_t available()const{return size_*8u-bit_;}
private:const std::uint8_t *data_=nullptr;std::size_t size_=0u,bit_=0u;
};

class RansBit {
public:
    bool start(Reader& reader){if(!reader.u8(&probability_zero_))return false;std::uint32_t size=0u;if(!reader.var32(&size)||size==0u||size>reader.remaining())return false;const std::uint8_t *data=nullptr;if(!reader.bytes(size,&data))return false;data_=data;const std::uint8_t tag=data[size-1u]>>6u;const std::size_t bytes=static_cast<std::size_t>(tag)+1u;if(tag==3u||bytes>size)return false;offset_=size-bytes;std::uint32_t value=0u;for(std::size_t i=0u;i<bytes;++i)value|=static_cast<std::uint32_t>(data[offset_+i])<<(8u*i);static constexpr std::uint32_t masks[3]={0x3fu,0x3fffu,0x3fffffu};state_=(value&masks[tag])+4096u;return state_<4096u*256u;}
    bool bit(bool *out){if(!out)return false;if(state_<4096u){if(offset_==0u)return false;state_=state_*256u+data_[--offset_];}const std::uint32_t probability_one=256u-probability_zero_;const std::uint32_t quotient=state_/256u,remainder=state_%256u,one_state=quotient*probability_one;const bool value=remainder<probability_one;state_=value?one_state+remainder:state_-one_state-probability_one;*out=value;return true;}
private:const std::uint8_t *data_=nullptr;std::size_t offset_=0u;std::uint32_t state_=0u;std::uint8_t probability_zero_=0u;
};

struct SplitEvent{int source=0,split=0;bool right=false;};

struct CornerTable{
    int faces=0,maximum_vertices=0,vertices=0;std::vector<int> corner_vertex,opposite,leftmost;
    bool reset(int face_count,int max_vertices){if(face_count<0||max_vertices<0||face_count>INT_MAX/3)return false;faces=face_count;maximum_vertices=max_vertices;vertices=0;corner_vertex.assign(static_cast<std::size_t>(face_count)*3u,-1);opposite.assign(corner_vertex.size(),-1);leftmost.assign(static_cast<std::size_t>(max_vertices),-1);return true;}
    int next(int c)const{return c<0?-1:(c/3)*3+(c+1)%3;}int previous(int c)const{return c<0?-1:(c/3)*3+(c+2)%3;}
    int oppositeCorner(int c)const{return c<0||static_cast<std::size_t>(c)>=opposite.size()?-1:opposite[static_cast<std::size_t>(c)];}
    int vertex(int c)const{return c<0||static_cast<std::size_t>(c)>=corner_vertex.size()?-1:corner_vertex[static_cast<std::size_t>(c)];}
    bool map(int c,int v){if(c<0||static_cast<std::size_t>(c)>=corner_vertex.size()||v<0||v>=maximum_vertices)return false;corner_vertex[static_cast<std::size_t>(c)]=v;return true;}
    int addVertex(){return vertices>=maximum_vertices?-1:vertices++;}
    bool setOpposite(int a,int b){if(a<0||b<0||a==b||static_cast<std::size_t>(a)>=opposite.size()||static_cast<std::size_t>(b)>=opposite.size()||opposite[a]>=0||opposite[b]>=0)return false;opposite[a]=b;opposite[b]=a;return true;}
    int swingLeft(int c)const{const int e=next(c),o=oppositeCorner(e);return o<0?-1:next(o);}int swingRight(int c)const{const int e=previous(c),o=oppositeCorner(e);return o<0?-1:previous(o);}
    void setLeftmost(int v,int c){if(v>=0&&static_cast<std::size_t>(v)<leftmost.size())leftmost[static_cast<std::size_t>(v)]=c;}int leftMost(int v)const{return v>=0&&static_cast<std::size_t>(v)<leftmost.size()?leftmost[static_cast<std::size_t>(v)]:-1;}void isolate(int v){if(v>=0&&static_cast<std::size_t>(v)<leftmost.size())leftmost[static_cast<std::size_t>(v)]=-1;}
};

struct Traversal{Bits symbols;RansBit start_faces;std::vector<RansBit> seams;bool symbol(std::uint32_t *out){std::uint32_t first=0u;if(!symbols.read(1u,&first))return false;if(first==0u){*out=0u;return true;}std::uint32_t suffix=0u;if(!symbols.read(2u,&suffix))return false;*out=1u|(suffix<<1u);return true;}};

bool skipMetadataElement(Reader& r,std::string *error){std::uint32_t entries=0u;if(!r.var32(&entries))return fail(error,"truncated Draco metadata entries");for(std::uint32_t i=0u;i<entries;++i){std::uint8_t key=0u,value=0u;if(!r.u8(&key)||!r.skip(key)||!r.u8(&value)||!r.skip(value))return fail(error,"truncated Draco metadata entry");}std::uint32_t children=0u;if(!r.var32(&children))return fail(error,"truncated Draco metadata children");for(std::uint32_t i=0u;i<children;++i){std::uint8_t key=0u;if(!r.u8(&key)||!r.skip(key)||!skipMetadataElement(r,error))return fail(error,"truncated Draco submetadata");}return true;}
bool skipMetadata(Reader& r,std::string *error){std::uint32_t count=0u;if(!r.var32(&count))return fail(error,"truncated Draco metadata count");for(std::uint32_t i=0u;i<count;++i){std::uint32_t id=0u;if(!r.var32(&id)||!skipMetadataElement(r,error))return false;}return skipMetadataElement(r,error);}

bool parseSplitEvents(Reader& reader,std::vector<SplitEvent> *splits,std::string *error){std::uint32_t count=0u;if(!reader.var32(&count))return fail(error,"truncated Draco topology split count");splits->clear();splits->reserve(count);int last_source=0;for(std::uint32_t i=0u;i<count;++i){std::uint32_t source_delta=0u,split_delta=0u;if(!reader.var32(&source_delta)||!reader.var32(&split_delta))return fail(error,"truncated Draco topology split event");if(source_delta>static_cast<std::uint32_t>(INT_MAX-last_source))return fail(error,"Draco topology source id overflows");const int source=last_source+static_cast<int>(source_delta);if(split_delta>static_cast<std::uint32_t>(source))return fail(error,"invalid Draco split delta");splits->push_back({source,source-static_cast<int>(split_delta),false});last_source=source;}if(!splits->empty()){Bits bits(reader.current(),reader.remaining());for(SplitEvent& split:*splits){std::uint32_t edge=0u;if(!bits.read(1u,&edge))return fail(error,"truncated Draco split edge data");split.right=edge!=0u;}if(!reader.skip(bits.bytesUsed()))return fail(error,"truncated Draco split edge data");}return true;}

bool findSplit(const std::vector<SplitEvent>& splits,int source,std::size_t *cursor,SplitEvent *out){if(!cursor||!out)return false;while(*cursor<splits.size()&&splits[*cursor].source<source)++*cursor;if(*cursor>=splits.size()||splits[*cursor].source!=source)return false;*out=splits[(*cursor)++];return true;}

int compactVertices(CornerTable& table,std::vector<int>& isolated){int count=table.vertices;std::sort(isolated.begin(),isolated.end());for(const int invalid:isolated){if(invalid>=count)continue;int source=count-1;while(source>=0&&table.leftMost(source)<0)--source;if(source<invalid)continue;if(source!=invalid){for(int& vertex:table.corner_vertex)if(vertex==source)vertex=invalid;table.setLeftmost(invalid,table.leftMost(source));table.setLeftmost(source,-1);}--count;}table.vertices=count;return count;}

bool decodeTopology(std::uint32_t symbol_count,std::uint32_t face_count,std::uint32_t encoded_vertices,std::uint32_t split_count,const std::vector<SplitEvent>& splits,Traversal& traversal,CornerTable *table,std::string *error){
    if(!table||!table->reset(static_cast<int>(face_count),static_cast<int>(encoded_vertices+split_count)))return fail(error,"invalid Draco EdgeBreaker table size");
    std::vector<int> active,isolated;std::unordered_map<int,int> split_active;std::size_t split_cursor=0u;int decoded_faces=0;
    for(std::uint32_t symbol_id=0u;symbol_id<symbol_count;++symbol_id){std::uint32_t symbol=0u;if(!traversal.symbol(&symbol))return fail(error,"truncated Draco EdgeBreaker symbol stream");const int corner=decoded_faces++*3;bool check_split=false;
        if(symbol==0u){if(active.empty())return fail(error,"Draco C symbol has no active edge");const int a=active.back();const int vx=table->vertex(table->next(a));const int b=table->next(table->leftMost(vx));if(a==b||a<0||b<0||table->oppositeCorner(a)>=0||table->oppositeCorner(b)>=0)return fail(error,"invalid Draco C topology");const int va=table->vertex(table->previous(a)),vb=table->vertex(table->next(b));if(vx==va||vx==vb||!table->setOpposite(a,corner+1)||!table->setOpposite(b,corner+2)||!table->map(corner,vx)||!table->map(corner+1,vb)||!table->map(corner+2,va))return fail(error,"invalid Draco C mapping");table->setLeftmost(va,corner+2);active.back()=corner;}
        else if(symbol==3u||symbol==5u){if(active.empty())return fail(error,"Draco L/R symbol has no active edge");const int a=active.back();if(table->oppositeCorner(a)>=0)return fail(error,"occupied Draco active edge");int opposite=0,left=0,right=0;if(symbol==5u){opposite=corner+2;left=corner+1;right=corner;}else{opposite=corner+1;left=corner;right=corner+2;}if(!table->setOpposite(opposite,a))return fail(error,"invalid Draco L/R opposite edge");const int nv=table->addVertex();if(nv<0||!table->map(opposite,nv))return fail(error,"too many Draco vertices");table->setLeftmost(nv,opposite);const int rv=table->vertex(table->previous(a)),lv=table->vertex(table->next(a));if(!table->map(right,rv)||!table->map(left,lv))return fail(error,"invalid Draco L/R vertex mapping");table->setLeftmost(rv,right);active.back()=corner;check_split=true;}
        else if(symbol==1u){if(active.empty())return fail(error,"Draco S symbol has no active edge");const int b=active.back();active.pop_back();const auto split=split_active.find(static_cast<int>(symbol_id));if(split!=split_active.end())active.push_back(split->second);if(active.empty())return fail(error,"Draco S symbol is missing its second active edge");const int a=active.back();if(a==b||table->oppositeCorner(a)>=0||table->oppositeCorner(b)>=0||!table->setOpposite(a,corner+2)||!table->setOpposite(b,corner+1))return fail(error,"invalid Draco S topology");const int vp=table->vertex(table->previous(a)),van=table->vertex(table->next(a)),vbp=table->vertex(table->previous(b));if(!table->map(corner,vp)||!table->map(corner+1,van)||!table->map(corner+2,vbp))return fail(error,"invalid Draco S vertex mapping");table->setLeftmost(vbp,corner+2);int merge=table->next(b);const int merged=table->vertex(merge);if(vp<0||merged<0)return fail(error,"invalid Draco S merge");table->setLeftmost(vp,table->leftMost(merged));const int first=merge;while(merge>=0){if(!table->map(merge,vp))return fail(error,"invalid Draco S merge mapping");merge=table->swingLeft(merge);if(merge==first)return fail(error,"closed Draco S merge ring");}table->isolate(merged);isolated.push_back(merged);active.back()=corner;}
        else if(symbol==7u){const int a=table->addVertex(),b=table->addVertex(),c=table->addVertex();if(a<0||b<0||c<0||!table->map(corner,a)||!table->map(corner+1,b)||!table->map(corner+2,c))return fail(error,"too many Draco E vertices");table->setLeftmost(a,corner);table->setLeftmost(b,corner+1);table->setLeftmost(c,corner+2);active.push_back(corner);check_split=true;}
        else return fail(error,"unknown Draco EdgeBreaker topology symbol");
        if(check_split){const int encoder_symbol=static_cast<int>(symbol_count)-static_cast<int>(symbol_id)-1;SplitEvent split;while(findSplit(splits,encoder_symbol,&split_cursor,&split)){if(split.split<0||active.empty())return fail(error,"invalid Draco topology split");const int top=active.back();const int new_active=split.right?table->next(top):table->previous(top);const int decoder_split=static_cast<int>(symbol_count)-split.split-1;split_active[decoder_split]=new_active;}}
    }
    while(!active.empty()){const int a=active.back();active.pop_back();bool interior=false;if(!traversal.start_faces.bit(&interior))return fail(error,"truncated Draco start-face stream");if(!interior)continue;if(decoded_faces>=static_cast<int>(face_count))return fail(error,"too many Draco start faces");const int vn=table->vertex(table->next(a));const int b=table->next(table->leftMost(vn));const int vx=table->vertex(table->next(b));const int c=table->next(table->leftMost(vx));if(a==b||a==c||b==c||a<0||b<0||c<0||table->oppositeCorner(a)>=0||table->oppositeCorner(b)>=0||table->oppositeCorner(c)>=0)return fail(error,"invalid Draco interior start face");const int vp=table->vertex(table->next(c));const int nc=decoded_faces++*3;if(!table->setOpposite(nc,a)||!table->setOpposite(nc+1,b)||!table->setOpposite(nc+2,c)||!table->map(nc,vx)||!table->map(nc+1,vp)||!table->map(nc+2,vn))return fail(error,"invalid Draco start-face mapping");}
    if (decoded_faces != static_cast<int>(face_count))
        return fail(error, "Draco EdgeBreaker face count mismatch");
    const int vertex_count = compactVertices(*table, isolated);
    if (vertex_count < 0 || static_cast<std::uint32_t>(vertex_count) > encoded_vertices)
        return fail(error, "Draco EdgeBreaker vertex count mismatch");
    return true;
}

bool startTraversal(Reader& reader,std::uint8_t attribute_data,Traversal *traversal,std::string *error){if(!traversal)return false;std::uint64_t symbol_bytes=0u;if(!reader.var64(&symbol_bytes)||symbol_bytes>reader.remaining())return fail(error,"invalid Draco traversal bitstream size");const std::uint8_t *symbols=nullptr;if(!reader.bytes(static_cast<std::size_t>(symbol_bytes),&symbols))return false;traversal->symbols=Bits(symbols,static_cast<std::size_t>(symbol_bytes));if(!traversal->start_faces.start(reader))return fail(error,"invalid Draco start-face rANS stream");traversal->seams.resize(attribute_data);for(RansBit& seam:traversal->seams)if(!seam.start(reader))return fail(error,"invalid Draco seam rANS stream");return true;}

bool decodeSeams(const CornerTable& table,Traversal *traversal,DracoEdgeBreakerTopology *topology,std::string *error){if(!traversal||!topology)return false;topology->seams.assign(traversal->seams.size(),std::vector<std::uint8_t>(table.corner_vertex.size(),0u));for(std::size_t corner=0u;corner<table.corner_vertex.size();++corner){const int opposite=table.oppositeCorner(static_cast<int>(corner));if(opposite<0){for(auto& seam:topology->seams)seam[corner]=1u;continue;}if(static_cast<std::size_t>(opposite)<corner)continue;for(std::size_t i=0u;i<traversal->seams.size();++i){bool value=false;if(!traversal->seams[i].bit(&value))return fail(error,"truncated Draco attribute seam stream");if(value){topology->seams[i][corner]=1u;topology->seams[i][static_cast<std::size_t>(opposite)]=1u;}}}return true;}

} // namespace

bool decodeDracoEdgeBreakerComplete(const std::uint8_t *data,std::size_t size,DracoMesh *mesh,std::string *error){
    if (error) error->clear();
    if (!data || !mesh || size < 12u) return fail(error, "truncated Draco stream");
    *mesh = {};
    Reader reader(data, size);
    if (reader.remaining() < 5u || std::memcmp(reader.current(), "DRACO", 5u) != 0)
        return fail(error, "invalid Draco magic");
    reader.skip(5u);
    std::uint8_t major=0u,minor=0u,type=0u,method=0u;std::uint16_t flags=0u;if(!reader.u8(&major)||!reader.u8(&minor)||!reader.u8(&type)||!reader.u8(&method)||!reader.u16(&flags))return fail(error,"truncated Draco header");if(major!=2u||minor>2u)return fail(error,"unsupported Draco bitstream version");if(type!=1u||method!=1u)return fail(error,"Draco stream is not an EdgeBreaker triangular mesh");if((flags&0x8000u)!=0u&&!skipMetadata(reader,error))return false;
    std::uint8_t traversal_type=0u;if(!reader.u8(&traversal_type))return fail(error,"truncated Draco EdgeBreaker traversal type");if(traversal_type!=0u)return fail(error,"unsupported Draco EdgeBreaker traversal encoding");
    std::uint32_t encoded_vertices=0u,face_count=0u,symbol_count=0u,split_count=0u;std::uint8_t attribute_data=0u;if(!reader.var32(&encoded_vertices)||!reader.var32(&face_count)||!reader.u8(&attribute_data)||!reader.var32(&symbol_count)||!reader.var32(&split_count))return fail(error,"truncated Draco EdgeBreaker counts");if(split_count>symbol_count||symbol_count>face_count||encoded_vertices>face_count*3u)return fail(error,"invalid Draco EdgeBreaker counts");
    std::vector<SplitEvent> splits;if(!parseSplitEvents(reader,&splits,error))return false;Traversal traversal;if(!startTraversal(reader,attribute_data,&traversal,error))return false;CornerTable table;if(!decodeTopology(symbol_count,face_count,encoded_vertices,split_count,splits,traversal,&table,error))return false;
    DracoEdgeBreakerTopology topology;topology.position_vertex_count=static_cast<std::uint32_t>(table.vertices);topology.corner_vertices.resize(table.corner_vertex.size());topology.opposite.resize(table.opposite.size());for(std::size_t i=0u;i<table.corner_vertex.size();++i){if(table.corner_vertex[i]<0||static_cast<std::uint32_t>(table.corner_vertex[i])>=topology.position_vertex_count)return fail(error,"invalid Draco reconstructed vertex");topology.corner_vertices[i]=static_cast<std::uint32_t>(table.corner_vertex[i]);topology.opposite[i]=static_cast<std::int32_t>(table.opposite[i]);}
    if (!decodeSeams(table, &traversal, &topology, error)) return false;
    return decodeDracoEdgeBreakerAttributes(reader.current(), reader.remaining(), topology, mesh, error);
}

} // namespace Models::Compression
