struct SplitEvent { int source=0, split=0; bool right=false; };
struct CornerTable {
    int maximum_vertices=0, vertices=0;
    std::vector<int> corner_vertex, opposite, leftmost;
    bool reset(std::uint32_t faces,std::uint32_t max_vertices) {
        if(faces>static_cast<std::uint32_t>(INT_MAX/3)||max_vertices>static_cast<std::uint32_t>(INT_MAX)) return false;
        maximum_vertices=static_cast<int>(max_vertices);vertices=0;
        corner_vertex.assign(static_cast<std::size_t>(faces)*3u,-1);opposite.assign(corner_vertex.size(),-1);leftmost.assign(max_vertices,-1);return true;
    }
    int next(int c)const{return c<0?-1:(c/3)*3+(c+1)%3;} int previous(int c)const{return c<0?-1:(c/3)*3+(c+2)%3;}
    int oppositeCorner(int c)const{return c<0||static_cast<std::size_t>(c)>=opposite.size()?-1:opposite[static_cast<std::size_t>(c)];}
    int vertex(int c)const{return c<0||static_cast<std::size_t>(c)>=corner_vertex.size()?-1:corner_vertex[static_cast<std::size_t>(c)];}
    bool map(int c,int v){if(c<0||static_cast<std::size_t>(c)>=corner_vertex.size()||v<0||v>=maximum_vertices)return false;corner_vertex[static_cast<std::size_t>(c)]=v;return true;}
    int addVertex(){return vertices>=maximum_vertices?-1:vertices++;}
    bool setOpposite(int a,int b){if(a<0||b<0||a==b||static_cast<std::size_t>(a)>=opposite.size()||static_cast<std::size_t>(b)>=opposite.size()||opposite[a]>=0||opposite[b]>=0)return false;opposite[a]=b;opposite[b]=a;return true;}
    int swingLeft(int c)const{const int o=oppositeCorner(next(c));return o<0?-1:next(o);} int swingRight(int c)const{const int o=oppositeCorner(previous(c));return o<0?-1:previous(o);}
    void setLeftmost(int v,int c){if(v>=0&&static_cast<std::size_t>(v)<leftmost.size())leftmost[static_cast<std::size_t>(v)]=c;}
    int leftMost(int v)const{return v>=0&&static_cast<std::size_t>(v)<leftmost.size()?leftmost[static_cast<std::size_t>(v)]:-1;}
    void isolate(int v){if(v>=0&&static_cast<std::size_t>(v)<leftmost.size())leftmost[static_cast<std::size_t>(v)]=-1;}
};

bool parseSplits(Reader& r,std::vector<SplitEvent>* out,std::string* error){
    std::uint32_t count=0;if(!r.var32(&count))return fail(error,"truncated Draco topology split count");out->clear();out->reserve(count);int last=0;
    for(std::uint32_t i=0;i<count;++i){std::uint32_t sd=0,dd=0;if(!r.var32(&sd)||!r.var32(&dd)||sd>static_cast<std::uint32_t>(INT_MAX-last))return fail(error,"truncated Draco topology split");const int source=last+static_cast<int>(sd);if(dd>static_cast<std::uint32_t>(source))return fail(error,"invalid Draco split delta");out->push_back({source,source-static_cast<int>(dd),false});last=source;}
    if(!out->empty()){
        std::uint8_t byte=0;unsigned bit=8;
        for(auto& s:*out){if(bit==8){if(!r.u8(&byte))return fail(error,"truncated Draco split edge data");bit=0;}s.right=((byte>>bit)&1u)!=0u;++bit;}
    }
    return true;
}

bool findSplit(const std::vector<SplitEvent>& s,int source,std::size_t* cursor,SplitEvent* out){while(*cursor<s.size()&&s[*cursor].source<source)++*cursor;if(*cursor>=s.size()||s[*cursor].source!=source)return false;*out=s[(*cursor)++];return true;}
int compact(CornerTable& t,std::vector<int>& isolated){int count=t.vertices;std::sort(isolated.begin(),isolated.end());for(int invalid:isolated){if(invalid>=count)continue;int source=count-1;while(source>=0&&t.leftMost(source)<0)--source;if(source<invalid)continue;if(source!=invalid){for(int& v:t.corner_vertex)if(v==source)v=invalid;t.setLeftmost(invalid,t.leftMost(source));t.setLeftmost(source,-1);}--count;}t.vertices=count;return count;}

struct ValenceTraversal {
    RansBit start_faces;
    std::vector<RansBit> seams;
    std::vector<std::vector<std::uint32_t>> contexts = std::vector<std::vector<std::uint32_t>>(6);
    std::vector<std::size_t> counters = std::vector<std::size_t>(6,0u);
    std::vector<int> valence;
    int active_context=-1;
    std::uint32_t last_symbol=7u;
    bool next(std::uint32_t* out){
        if(!out)return false;
        if(active_context<0){last_symbol=7u;*out=7u;return true;}
        const std::size_t c=static_cast<std::size_t>(active_context);if(c>=contexts.size()||counters[c]==0u)return false;
        const std::uint32_t id=contexts[c][--counters[c]];static constexpr std::uint32_t map[5]={0u,1u,3u,5u,7u};if(id>4u)return false;last_symbol=map[id];*out=last_symbol;return true;
    }
    bool merge(int dest,int source){if(dest<0||source<0||static_cast<std::size_t>(dest)>=valence.size()||static_cast<std::size_t>(source)>=valence.size())return false;valence[dest]+=valence[source];return true;}
    bool reached(const CornerTable& t,int corner){
        const int n=t.vertex(t.next(corner)),p=t.vertex(t.previous(corner)),v=t.vertex(corner);if(n<0||p<0||v<0)return false;
        if(static_cast<std::size_t>(std::max({n,p,v}))>=valence.size())return false;
        switch(last_symbol){case 0u:case 1u:valence[n]+=1;valence[p]+=1;break;case 5u:valence[v]+=1;valence[n]+=1;valence[p]+=2;break;case 3u:valence[v]+=1;valence[n]+=2;valence[p]+=1;break;case 7u:valence[v]+=2;valence[n]+=2;valence[p]+=2;break;default:return false;}
        const int active=std::clamp(valence[n],2,7);active_context=active-2;return true;
    }
};
