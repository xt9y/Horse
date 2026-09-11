struct Topology {
    std::vector<std::uint32_t> corner_point;
    std::vector<int> opposite;
    std::vector<int> point_corner;
    static int next(int c){return (c/3)*3+(c+1)%3;} static int prev(int c){return (c/3)*3+(c+2)%3;}
};

std::uint64_t edgeKey(std::uint32_t a,std::uint32_t b){if(a>b)std::swap(a,b);return(static_cast<std::uint64_t>(a)<<32u)|b;}
bool buildTopology(const DracoMesh& mesh,Topology *topology) {
    if (!topology || mesh.indices.size() % 3u != 0u) return false;
    topology->corner_point = mesh.indices;
    topology->opposite.assign(mesh.indices.size(), -1);
    topology->point_corner.assign(mesh.point_count, -1);
    std::unordered_map<std::uint64_t,int> edges; edges.reserve(mesh.indices.size());
    for(std::size_t c=0;c<mesh.indices.size();++c){const auto p=mesh.indices[c];if(p>=mesh.point_count)return false;if(topology->point_corner[p]<0)topology->point_corner[p]=static_cast<int>(c);const int ci=static_cast<int>(c);const auto a=mesh.indices[static_cast<std::size_t>(Topology::next(ci))],b=mesh.indices[static_cast<std::size_t>(Topology::prev(ci))];const auto key=edgeKey(a,b);auto it=edges.find(key);if(it==edges.end())edges.emplace(key,ci);else if(topology->opposite[static_cast<std::size_t>(it->second)]<0){topology->opposite[c]=it->second;topology->opposite[static_cast<std::size_t>(it->second)]=ci;edges.erase(it);}}
    return true;
}

struct Record {
    DracoAttribute output;
    std::uint8_t decoder_type=0u;
    std::vector<std::int32_t> portable;
    std::int8_t prediction=-2;
};

bool wrapValue(const std::vector<std::int32_t>& pred,const std::vector<std::int32_t>& corr,std::int32_t minv,std::int32_t maxv,std::vector<std::int32_t> *out){
    if (!out || pred.size() != corr.size() || minv > maxv) return false;
    const std::int64_t range = 1ll + static_cast<std::int64_t>(maxv) - minv;
    out->resize(pred.size());
    for (std::size_t i = 0u; i < pred.size(); ++i) { const std::int64_t p = std::clamp<std::int64_t>(pred[i], minv, maxv); std::int64_t v = p + corr[i]; if (v > maxv) v -= range; else if (v < minv) v += range; (*out)[i] = static_cast<std::int32_t>(v); }
    return true;
}

bool parallelogram(const Topology& t,std::size_t entry,const std::vector<std::int32_t>& values,unsigned comps,std::vector<std::int32_t> *out) {
    if (!out || entry >= t.point_corner.size()) return false;
    const int c = t.point_corner[entry];
    if (c < 0) return false;
    const int o = t.opposite[static_cast<std::size_t>(c)];
    if (o < 0) return false;
    const std::uint32_t opp=t.corner_point[static_cast<std::size_t>(o)],n=t.corner_point[static_cast<std::size_t>(Topology::next(o))],p=t.corner_point[static_cast<std::size_t>(Topology::prev(o))];
    if (opp >= entry || n >= entry || p >= entry) return false;
    out->assign(comps, 0);
    for (unsigned k = 0u; k < comps; ++k) (*out)[k] = values[static_cast<std::size_t>(n) * comps + k] + values[static_cast<std::size_t>(p) * comps + k] - values[static_cast<std::size_t>(opp) * comps + k];
    return true;
}

std::vector<int> incidentCorners(const Topology& t,std::uint32_t point){std::vector<int> result;for(std::size_t c=0;c<t.corner_point.size();++c)if(t.corner_point[c]==point)result.push_back(static_cast<int>(c));return result;}

std::array<std::int32_t,3> portablePosition(const std::vector<Record>& decoded,std::uint32_t point) {
    for(const Record& r:decoded) if(r.output.attribute_type==0u && r.portable.size()>=static_cast<std::size_t>(point+1u)*3u) return {r.portable[static_cast<std::size_t>(point)*3u],r.portable[static_cast<std::size_t>(point)*3u+1u],r.portable[static_cast<std::size_t>(point)*3u+2u]};
    return {0,0,0};
}

void octahedralToUnit(std::int32_t qs,std::int32_t qt,unsigned bits,double *x,double *y,double *z){const std::int64_t maxq=(std::int64_t{1}<<bits)-1,maxv=maxq-1;if(maxv<=0){*x=*y=*z=0;return;}double s=static_cast<double>(qs)/maxv,t=static_cast<double>(qt)/maxv,sum=s+t,diff=s-t,sign=1.0;if(!(sum>=0.5&&sum<=1.5&&diff>=-0.5&&diff<=0.5)){sign=-1.0;if(sum<=0.5){const double ns=0.5-t,nt=0.5-s;s=ns;t=nt;}else if(sum>=1.5){const double ns=1.5-t,nt=1.5-s;s=ns;t=nt;}else if(diff<=-0.5){const double ns=t-0.5,nt=s+0.5;s=ns;t=nt;}else{const double ns=t+0.5,nt=s-0.5;s=ns;t=nt;}sum=s+t;diff=s-t;}const double yy=2*s-1,zz=2*t-1,xx=std::min(std::min(2*sum-1,3-2*sum),std::min(2*diff+1,1-2*diff))*sign;double n=xx*xx+yy*yy+zz*zz;if(n<1e-20){*x=*y=*z=0;return;}n=1/std::sqrt(n);*x=xx*n;*y=yy*n;*z=zz*n;}

void canonicalizeIntegerVector(std::array<std::int64_t,3> *v,std::int64_t center){const std::int64_t sum=std::llabs((*v)[0])+std::llabs((*v)[1])+std::llabs((*v)[2]);if(sum==0){(*v)[0]=center;return;}(*v)[0]=((*v)[0]*center)/sum;(*v)[1]=((*v)[1]*center)/sum;(*v)[2]=(*v)[2]>=0?center-std::llabs((*v)[0])-std::llabs((*v)[1]):-(center-std::llabs((*v)[0])-std::llabs((*v)[1]));}
std::array<std::int64_t,2> integerVectorToOct(const std::array<std::int64_t,3>& value,std::int64_t center,std::int64_t max_value){std::int64_t s=0,t=0;if(value[0]>=0){s=value[1]+center;t=value[2]+center;}else{s=value[1]<0?std::llabs(value[2]):max_value-std::llabs(value[2]);t=value[2]<0?std::llabs(value[1]):max_value-std::llabs(value[1]);}return{s,t};}
