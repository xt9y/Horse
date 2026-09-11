struct AttributeRecord {
    DracoAttribute output;
    std::uint8_t decoder_type=0u;
    std::vector<std::int32_t> portable;
};

struct DecoderGroup {
    std::int8_t data_id=-1;
    std::uint8_t decoder_type=0u;
    std::uint8_t traversal_method=0u;
    std::vector<AttributeRecord> attributes;
    std::vector<std::uint32_t> corner_vertex;
    std::uint32_t vertex_count=0u;
    std::vector<std::uint8_t> boundary;
    std::vector<int> sequence_corners;
    std::vector<std::uint32_t> domain_to_entry;
};

bool groupUsesSeams(const DecoderGroup& group) { return group.decoder_type==1u; }

bool seamEdge(const DracoEdgeBreakerTopology& topology,const DecoderGroup& group,int corner)
{
    if(!groupUsesSeams(group) || group.data_id<0 || static_cast<std::size_t>(group.data_id)>=topology.seams.size()) return false;
    if(corner<0 || static_cast<std::size_t>(corner)>=topology.seams[static_cast<std::size_t>(group.data_id)].size()) return true;
    return topology.seams[static_cast<std::size_t>(group.data_id)][static_cast<std::size_t>(corner)]!=0u;
}

int groupOpposite(const DracoEdgeBreakerTopology& topology,const DecoderGroup& group,int corner)
{
    if(corner<0 || seamEdge(topology,group,corner)) return -1;
    return oppositeCorner(topology,corner);
}
int groupRight(const DracoEdgeBreakerTopology& topology,const DecoderGroup& group,int corner)
{
    return groupOpposite(topology,group,nextCorner(corner));
}
int groupLeft(const DracoEdgeBreakerTopology& topology,const DecoderGroup& group,int corner)
{
    return groupOpposite(topology,group,previousCorner(corner));
}

bool buildGroupTopology(const DracoEdgeBreakerTopology& topology,DecoderGroup *group,std::string *error)
{
    if(!group) return false;
    if(group->decoder_type==0u) {
        group->corner_vertex=topology.corner_vertices;
        group->vertex_count=topology.position_vertex_count;
    } else if(group->decoder_type==1u) {
        if(group->data_id<0 || static_cast<std::size_t>(group->data_id)>=topology.seams.size())
            return fail(error,"Draco corner attribute decoder references invalid connectivity data");
        if(!dracoEdgeBreakerCornerDomains(topology,static_cast<std::size_t>(group->data_id),&group->corner_vertex,&group->vertex_count))
            return fail(error,"failed to split Draco attribute seams");
    } else {
        return fail(error,"unsupported Draco mesh attribute decoder type");
    }
    group->boundary.assign(group->vertex_count,0u);
    for(std::size_t c=0u;c<group->corner_vertex.size();++c) {
        const std::uint32_t v=group->corner_vertex[c];
        if(v>=group->boundary.size()) return fail(error,"Draco attribute corner references invalid vertex");
        const int corner=static_cast<int>(c);
        if(groupRight(topology,*group,corner)<0 || groupLeft(topology,*group,corner)<0) group->boundary[v]=1u;
    }
    return true;
}

bool generateDepthFirstSequence(const DracoEdgeBreakerTopology& topology,DecoderGroup *group,std::string *error)
{
    if(!group) return false;
    const std::size_t faces=topology.corner_vertices.size()/3u;
    std::vector<std::uint8_t> face_visited(faces,0u), vertex_visited(group->vertex_count,0u);
    group->sequence_corners.clear();
    group->domain_to_entry.assign(group->vertex_count,UINT32_MAX);
    auto visitVertex=[&](std::uint32_t v,int corner)->bool {
        if(v>=vertex_visited.size()) return false;
        if(vertex_visited[v]) return true;
        vertex_visited[v]=1u;
        group->domain_to_entry[v]=static_cast<std::uint32_t>(group->sequence_corners.size());
        group->sequence_corners.push_back(corner);
        return true;
    };
    auto faceVisited=[&](int corner)->bool {
        if(corner<0) return true;
        const std::size_t face=static_cast<std::size_t>(corner/3);
        return face>=face_visited.size() || face_visited[face]!=0u;
    };
    for(std::size_t start_face=0u;start_face<faces;++start_face) {
        const int start_corner=static_cast<int>(start_face*3u);
        if(face_visited[start_face]) continue;
        std::vector<int> stack{start_corner};
        const std::uint32_t next_v=group->corner_vertex[static_cast<std::size_t>(nextCorner(start_corner))];
        const std::uint32_t prev_v=group->corner_vertex[static_cast<std::size_t>(previousCorner(start_corner))];
        if(!visitVertex(next_v,nextCorner(start_corner)) || !visitVertex(prev_v,previousCorner(start_corner)))
            return fail(error,"invalid Draco traversal vertex");
        while(!stack.empty()) {
            int corner=stack.back();
            if(corner<0 || faceVisited(corner)) { stack.pop_back(); continue; }
            while(true) {
                const std::size_t face=static_cast<std::size_t>(corner/3);
                if(face>=face_visited.size()) return fail(error,"Draco traversal left face range");
                face_visited[face]=1u;
                const std::uint32_t vertex=group->corner_vertex[static_cast<std::size_t>(corner)];
                if(vertex>=vertex_visited.size()) return fail(error,"invalid Draco traversal vertex id");
                if(!vertex_visited[vertex]) {
                    const bool on_boundary=group->boundary[vertex]!=0u;
                    if(!visitVertex(vertex,corner)) return fail(error,"invalid Draco traversal vertex");
                    if(!on_boundary) {
                        corner=groupRight(topology,*group,corner);
                        if(corner<0) return fail(error,"closed Draco vertex unexpectedly hit a boundary");
                        continue;
                    }
                }
                const int right=groupRight(topology,*group,corner);
                const int left=groupLeft(topology,*group,corner);
                const bool right_visited=faceVisited(right);
                const bool left_visited=faceVisited(left);
                if(right_visited) {
                    if(left_visited) { stack.pop_back(); break; }
                    corner=left;
                } else if(left_visited) {
                    corner=right;
                } else {
                    stack.back()=left;
                    stack.push_back(right);
                    break;
                }
            }
        }
    }
    if(group->sequence_corners.size()!=group->vertex_count)
        return fail(error,"Draco depth-first traversal did not visit every attribute vertex");
    return true;
}

bool generatePredictionDegreeSequence(const DracoEdgeBreakerTopology& topology,DecoderGroup *group,std::string *error)
{
    if(!group) return false;
    if(groupUsesSeams(*group)) return fail(error,"prediction-degree traversal is not valid for Draco corner attributes");
    const std::size_t faces=topology.corner_vertices.size()/3u;
    std::vector<std::uint8_t> face_visited(faces,0u),vertex_visited(group->vertex_count,0u);
    std::vector<int> prediction_degree(group->vertex_count,0);
    group->sequence_corners.clear();
    group->domain_to_entry.assign(group->vertex_count,UINT32_MAX);
    auto visit=[&](std::uint32_t v,int c)->bool{
        if(v>=vertex_visited.size()) return false;
        if(vertex_visited[v]) return true;
        vertex_visited[v]=1u; group->domain_to_entry[v]=static_cast<std::uint32_t>(group->sequence_corners.size()); group->sequence_corners.push_back(c); return true;
    };
    auto faceVisited=[&](int c)->bool{if(c<0)return true;const std::size_t f=static_cast<std::size_t>(c/3);return f>=faces||face_visited[f]!=0u;};
    for(std::size_t start_face=0u;start_face<faces;++start_face) {
        if(face_visited[start_face]) continue;
        const int start=static_cast<int>(start_face*3u);
        std::array<std::vector<int>,3> stacks;
        int best=0; stacks[0].push_back(start);
        const int nc=nextCorner(start),pc=previousCorner(start);
        if(!visit(group->corner_vertex[nc],nc)||!visit(group->corner_vertex[pc],pc)||!visit(group->corner_vertex[start],start))
            return fail(error,"invalid Draco prediction traversal vertex");
        auto priority=[&](int corner)->int{
            if(corner<0)return 0;
            const std::uint32_t v=group->corner_vertex[static_cast<std::size_t>(corner)];
            if(v>=vertex_visited.size()||vertex_visited[v])return 0;
            const int degree=++prediction_degree[v]; return degree>1?1:2;
        };
        auto add=[&](int corner,int p){if(corner<0)return;stacks[static_cast<std::size_t>(p)].push_back(corner);best=std::min(best,p);};
        auto pop=[&]()->int{
            for(int i=best;i<3;++i) if(!stacks[static_cast<std::size_t>(i)].empty()) {const int c=stacks[static_cast<std::size_t>(i)].back();stacks[static_cast<std::size_t>(i)].pop_back();best=i;return c;}
            return -1;
        };
        int corner=-1;
        while((corner=pop())>=0) {
            if(faceVisited(corner)) continue;
            while(true) {
                const std::size_t face=static_cast<std::size_t>(corner/3); face_visited[face]=1u;
                const std::uint32_t vertex=group->corner_vertex[static_cast<std::size_t>(corner)];
                if(!visit(vertex,corner)) return fail(error,"invalid Draco prediction traversal vertex");
                const int right=groupRight(topology,*group,corner),left=groupLeft(topology,*group,corner);
                const bool rv=faceVisited(right),lv=faceVisited(left);
                if(!lv) {
                    const int p=priority(left);
                    if(rv && p<=best) {corner=left;continue;}
                    add(left,p);
                }
                if(!rv) {
                    const int p=priority(right);
                    if(p<=best) {corner=right;continue;}
                    add(right,p);
                }
                break;
            }
        }
    }
    if(group->sequence_corners.size()!=group->vertex_count)
        return fail(error,"Draco prediction-degree traversal did not visit every attribute vertex");
    return true;
}

bool generateSequence(const DracoEdgeBreakerTopology& topology,DecoderGroup *group,std::string *error)
{
    if(group->traversal_method==0u) return generateDepthFirstSequence(topology,group,error);
    if(group->traversal_method==1u) return generatePredictionDegreeSequence(topology,group,error);
    return fail(error,"unsupported Draco mesh traversal method");
}

void wrapValue(const std::vector<std::int32_t>& prediction,const std::vector<std::int32_t>& correction,
               std::int32_t minimum,std::int32_t maximum,std::vector<std::int32_t> *out)
{
    out->resize(correction.size());
    const std::int64_t range=1ll+static_cast<std::int64_t>(maximum)-minimum;
    for(std::size_t i=0u;i<correction.size();++i) {
        std::int64_t predicted=i<prediction.size()?prediction[i]:0;
        predicted=std::clamp<std::int64_t>(predicted,minimum,maximum);
        std::int64_t value=predicted+correction[i];
        if(value>maximum)value-=range;else if(value<minimum)value+=range;
        (*out)[i]=static_cast<std::int32_t>(value);
    }
}

bool parallelogramPrediction(const DracoEdgeBreakerTopology& topology,const DecoderGroup& group,
                             std::size_t entry,const std::vector<std::int32_t>& values,unsigned int components,
                             std::vector<std::int32_t> *prediction)
{
    if(entry>=group.sequence_corners.size()||!prediction) return false;
    const int corner=group.sequence_corners[entry];
    const int opposite=groupOpposite(topology,group,corner);
    if(opposite<0)return false;
    const std::array<int,3> corners={opposite,nextCorner(opposite),previousCorner(opposite)};
    std::array<std::uint32_t,3> ids{};
    for(std::size_t i=0u;i<3u;++i) {
        const std::uint32_t domain=group.corner_vertex[static_cast<std::size_t>(corners[i])];
        if(domain>=group.domain_to_entry.size())return false;
        ids[i]=group.domain_to_entry[domain];
        if(ids[i]>=entry)return false;
    }
    prediction->assign(components,0);
    for(unsigned int c=0u;c<components;++c) {
        const std::int64_t value=static_cast<std::int64_t>(values[static_cast<std::size_t>(ids[1])*components+c])+
            values[static_cast<std::size_t>(ids[2])*components+c]-values[static_cast<std::size_t>(ids[0])*components+c];
        (*prediction)[c]=static_cast<std::int32_t>(value);
    }
    return true;
}

bool readPredictionBits(Reader& reader,std::size_t count,std::vector<std::uint8_t> *out)
{
    if(!out)return false;
    out->assign(count,0u); if(count==0u)return true;
    RansBit decoder; if(!decoder.start(reader))return false;
    for(std::size_t i=0u;i<count;++i){bool bit=false;if(!decoder.bit(&bit))return false;(*out)[i]=bit?1u:0u;}
    return true;
}

void octahedralToUnit(std::int32_t qs,std::int32_t qt,unsigned int bits,double *x,double *y,double *z)
{
    const std::int64_t max_quantized=(std::int64_t{1}<<bits)-1;
    const std::int64_t max_value=max_quantized-1;
    if(max_value<=0){*x=0;*y=0;*z=0;return;}
    double ss=static_cast<double>(qs)/static_cast<double>(max_value);
    double tt=static_cast<double>(qt)/static_cast<double>(max_value);
    double sum=ss+tt,diff=ss-tt,sign=1.0;
    if(!(sum>=0.5&&sum<=1.5&&diff>=-0.5&&diff<=0.5)){
        sign=-1.0;
        if(sum<=0.5){const double ns=0.5-tt,nt=0.5-ss;ss=ns;tt=nt;}
        else if(sum>=1.5){const double ns=1.5-tt,nt=1.5-ss;ss=ns;tt=nt;}
        else if(diff<=-0.5){const double ns=tt-0.5,nt=ss+0.5;ss=ns;tt=nt;}
        else{const double ns=tt+0.5,nt=ss-0.5;ss=ns;tt=nt;}
        sum=ss+tt;diff=ss-tt;
    }
    const double yy=2.0*ss-1.0,zz=2.0*tt-1.0;
    const double xx=std::min(std::min(2.0*sum-1.0,3.0-2.0*sum),std::min(2.0*diff+1.0,1.0-2.0*diff))*sign;
    double n=xx*xx+yy*yy+zz*zz;if(n<1e-12){*x=0;*y=0;*z=0;return;}n=1.0/std::sqrt(n);*x=xx*n;*y=yy*n;*z=zz*n;
}

struct DecodeState {
    const DracoEdgeBreakerTopology *topology=nullptr;
    std::vector<DecoderGroup> groups;
    std::vector<std::uint32_t> final_corner_points;
    std::vector<int> final_point_corner;
    AttributeRecord *position_record=nullptr;
    DecoderGroup *position_group=nullptr;
};

bool quantizedPositionForCorner(const DecodeState& state,int corner,std::array<std::int32_t,3> *out)
