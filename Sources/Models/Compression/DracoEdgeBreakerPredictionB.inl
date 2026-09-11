            const std::size_t context=predictions.empty()?0u:predictions.size()-1u;
            for(std::size_t i=0u;i<predictions.size();++i){bool crease=false;if(crease_pos[context]<crease_bits[context].size())crease=crease_bits[context][crease_pos[context]++]!=0u;else return fail(error,"Draco constrained prediction flag underflow");if(crease)continue;++used;for(unsigned int c=0u;c<portable_components;++c)pred[c]+=predictions[i][c];}
            if(used==0u)pred=previousValue(entry);else for(std::int32_t& value:pred)value/=static_cast<std::int32_t>(used);
            writeEntry(entry,applyWrap(pred,correctionAt(entry)));
        }
    } else if(prediction==5){
        if(portable_components!=2u)return fail(error,"Draco texcoord prediction requires two components");
        std::size_t orientation_pos=orientations.size();
        for(std::size_t entry=0u;entry<entries;++entry){
            const int corner=group.sequence_corners[entry];
            const std::uint32_t next_domain=group.corner_vertex[static_cast<std::size_t>(nextCorner(corner))];
            const std::uint32_t prev_domain=group.corner_vertex[static_cast<std::size_t>(previousCorner(corner))];
            const std::uint32_t next_entry=next_domain<group.domain_to_entry.size()?group.domain_to_entry[next_domain]:UINT32_MAX;
            const std::uint32_t prev_entry=prev_domain<group.domain_to_entry.size()?group.domain_to_entry[prev_domain]:UINT32_MAX;
            std::vector<std::int32_t> pred(2u,0);
            if(prev_entry<entry&&next_entry<entry){
                const std::array<std::int32_t,2> n_uv={original[static_cast<std::size_t>(next_entry)*2u],original[static_cast<std::size_t>(next_entry)*2u+1u]};
                const std::array<std::int32_t,2> p_uv={original[static_cast<std::size_t>(prev_entry)*2u],original[static_cast<std::size_t>(prev_entry)*2u+1u]};
                if(n_uv==p_uv){pred={p_uv[0],p_uv[1]};}
                else{
                    std::array<std::int32_t,3> tip{},next_pos{},prev_pos{};
                    if(!quantizedPositionForCorner(*state,corner,&tip)||!quantizedPositionForCorner(*state,nextCorner(corner),&next_pos)||!quantizedPositionForCorner(*state,previousCorner(corner),&prev_pos))return fail(error,"Draco texcoord prediction requires decoded positions");
                    const std::int64_t pnx=static_cast<std::int64_t>(prev_pos[0])-next_pos[0],pny=static_cast<std::int64_t>(prev_pos[1])-next_pos[1],pnz=static_cast<std::int64_t>(prev_pos[2])-next_pos[2];
                    const std::uint64_t pn2=static_cast<std::uint64_t>(pnx*pnx+pny*pny+pnz*pnz);
                    if(pn2!=0u){
                        const std::int64_t cnx=static_cast<std::int64_t>(tip[0])-next_pos[0],cny=static_cast<std::int64_t>(tip[1])-next_pos[1],cnz=static_cast<std::int64_t>(tip[2])-next_pos[2];
                        const std::int64_t dot=cnx*pnx+cny*pny+cnz*pnz;
                        const std::int64_t du=static_cast<std::int64_t>(p_uv[0])-n_uv[0],dv=static_cast<std::int64_t>(p_uv[1])-n_uv[1];
                        const std::int64_t x_u=du*dot+static_cast<std::int64_t>(n_uv[0])*static_cast<std::int64_t>(pn2);
                        const std::int64_t x_v=dv*dot+static_cast<std::int64_t>(n_uv[1])*static_cast<std::int64_t>(pn2);
                        const std::int64_t dx_num=cnx*static_cast<std::int64_t>(pn2)-pnx*dot;
                        const std::int64_t dy_num=cny*static_cast<std::int64_t>(pn2)-pny*dot;
                        const std::int64_t dz_num=cnz*static_cast<std::int64_t>(pn2)-pnz*dot;
                        const long double dist_num=static_cast<long double>(dx_num)*dx_num+static_cast<long double>(dy_num)*dy_num+static_cast<long double>(dz_num)*dz_num;
                        std::uint64_t cxpn=0u;if(dist_num>0.0L){const long double scaled=dist_num/static_cast<long double>(pn2);cxpn=static_cast<std::uint64_t>(std::sqrt(scaled));}
                        const std::int64_t cx_u=dv*static_cast<std::int64_t>(cxpn),cx_v=-du*static_cast<std::int64_t>(cxpn);
                        if (orientation_pos == 0u) return fail(error, "Draco texcoord orientation underflow");
                        const bool orientation = orientations[--orientation_pos] != 0u;
                        const std::int64_t u_num=orientation?x_u+cx_u:x_u-cx_u,v_num=orientation?x_v+cx_v:x_v-cx_v;
                        pred[0]=static_cast<std::int32_t>(u_num/static_cast<std::int64_t>(pn2));pred[1]=static_cast<std::int32_t>(v_num/static_cast<std::int64_t>(pn2));
                    } else pred=previousValue(entry);
                }
            } else if(prev_entry<entry){pred[0]=original[static_cast<std::size_t>(prev_entry)*2u];pred[1]=original[static_cast<std::size_t>(prev_entry)*2u+1u];}
            else if(next_entry<entry){pred[0]=original[static_cast<std::size_t>(next_entry)*2u];pred[1]=original[static_cast<std::size_t>(next_entry)*2u+1u];}
            else pred=previousValue(entry);
            writeEntry(entry,applyWrap(pred,correctionAt(entry)));
        }
    } else if(prediction==6){
        if(portable_components!=2u || flip_bits.size()!=entries)return fail(error,"invalid Draco geometric normal stream");
        unsigned int bits=0u;std::uint32_t maxq=normal_max_q>0?static_cast<std::uint32_t>(normal_max_q):0u;while(maxq){++bits;maxq>>=1u;}if(bits==0u)return fail(error,"invalid Draco normal maximum quantized value");
        const std::int64_t max_quantized=(std::int64_t{1}<<bits)-1,max_value=max_quantized-1,center=max_value/2;
        for(std::size_t entry=0u;entry<entries;++entry){
            const int start=group.sequence_corners[entry];std::array<std::int64_t,3> normal{0,0,0};int corner=start;bool left=true;
            while(corner>=0){std::array<std::int32_t,3> center_pos{},next_pos{},prev_pos{};if(!quantizedPositionForCorner(*state,corner,&center_pos)||!quantizedPositionForCorner(*state,nextCorner(corner),&next_pos)||!quantizedPositionForCorner(*state,previousCorner(corner),&prev_pos))return fail(error,"Draco normal prediction requires decoded positions");
                const std::array<std::int64_t,3> a={static_cast<std::int64_t>(next_pos[0])-center_pos[0],static_cast<std::int64_t>(next_pos[1])-center_pos[1],static_cast<std::int64_t>(next_pos[2])-center_pos[2]};
                const std::array<std::int64_t,3> b={static_cast<std::int64_t>(prev_pos[0])-center_pos[0],static_cast<std::int64_t>(prev_pos[1])-center_pos[1],static_cast<std::int64_t>(prev_pos[2])-center_pos[2]};
                normal[0]+=a[1]*b[2]-a[2]*b[1];normal[1]+=a[2]*b[0]-a[0]*b[2];normal[2]+=a[0]*b[1]-a[1]*b[0];
                int next=-1;if(left){const int e=nextCorner(corner),o=groupOpposite(*state->topology,group,e);next=o<0?-1:nextCorner(o);if(next<0){left=false;const int re=previousCorner(start),ro=groupOpposite(*state->topology,group,re);next=ro<0?-1:previousCorner(ro);}else if(next==start)next=-1;}
                else{const int e=previousCorner(corner),o=groupOpposite(*state->topology,group,e);next=o<0?-1:previousCorner(o);}corner=next;
            }
            const std::int64_t abs_sum=std::llabs(normal[0])+std::llabs(normal[1])+std::llabs(normal[2]);if(abs_sum>(std::int64_t{1}<<29)){const std::int64_t quotient=abs_sum/(std::int64_t{1}<<29);if(quotient>0)for(auto&v:normal)v/=quotient;}
            canonicalizeIntegerVector(&normal,center);if(flip_bits[entry])for(auto&v:normal)v=-v;const auto pred64=integerVectorToOct(normal,center,max_value);
            const auto corr=correctionAt(entry);const auto out=normalTransform({static_cast<std::int32_t>(pred64[0]),static_cast<std::int32_t>(pred64[1])},{corr[0],corr[1]},normal_max_q);writeEntry(entry,{out[0],out[1]});
        }
    } else return fail(error,"unsupported Draco mesh prediction scheme");

    record->portable=original;
    if(record->decoder_type==2u){
        std::vector<float> minimum(record->output.components,0.0f);for(float& value:minimum)if(!reader.f32(&value))return fail(error,"truncated Draco quantization minimum");
        float range=0.0f;std::uint8_t bits=0u;if(!reader.f32(&range)||!reader.u8(&bits)||bits==0u||bits>31u)return fail(error,"invalid Draco quantization data");
        const double denominator=static_cast<double>((std::uint64_t{1}<<bits)-1u);record->output.values.resize(entries*record->output.components);
        for(std::size_t i=0u;i<record->output.values.size();++i)record->output.values[i]=minimum[i%record->output.components]+static_cast<double>(original[i])*static_cast<double>(range)/denominator;
    } else if(record->decoder_type==3u){
        if(record->output.components!=3u||record->output.data_type!=9u)return fail(error,"Draco normal decoder requires float32 vec3 output");
        std::uint8_t bits=0u;if(!reader.u8(&bits)||bits<2u||bits>30u)return fail(error,"invalid Draco normal quantization bits");record->output.values.resize(entries*3u);
        for(std::size_t i=0u;i<entries;++i){double x=0,y=0,z=0;octahedralToUnit(original[i*2u],original[i*2u+1u],bits,&x,&y,&z);record->output.values[i*3u]=x;record->output.values[i*3u+1u]=y;record->output.values[i*3u+2u]=z;}
    } else {
        record->output.values.resize(value_count);for(std::size_t i=0u;i<value_count;++i)record->output.values[i]=original[i];
    }
    return true;
}

struct VectorHash {
    std::size_t operator()(const std::vector<std::uint32_t>& value) const noexcept {
        std::size_t hash=1469598103934665603ull;for(const std::uint32_t v:value){hash^=static_cast<std::size_t>(v);hash*=1099511628211ull;}return hash;
    }
};

bool buildFinalPoints(DecodeState *state,DracoMesh *mesh,std::string *error)
{
    if(!state||!mesh||!state->topology)return false;
    const auto& topology=*state->topology;
    state->final_corner_points.assign(topology.corner_vertices.size(),UINT32_MAX);state->final_point_corner.clear();
    std::unordered_map<std::vector<std::uint32_t>,std::uint32_t,VectorHash> points;
    for(std::size_t c=0u;c<topology.corner_vertices.size();++c){
        std::vector<std::uint32_t> key;key.reserve(1u+state->groups.size());key.push_back(topology.corner_vertices[c]);
        for(const DecoderGroup& group:state->groups)if(groupUsesSeams(group)){if(c>=group.corner_vertex.size())return fail(error,"invalid Draco corner-domain mapping");key.push_back(group.corner_vertex[c]);}
        const auto found=points.find(key);std::uint32_t point=0u;if(found==points.end()){point=static_cast<std::uint32_t>(points.size());points.emplace(std::move(key),point);state->final_point_corner.push_back(static_cast<int>(c));}else point=found->second;state->final_corner_points[c]=point;
    }
    mesh->point_count=static_cast<std::uint32_t>(points.size());mesh->indices=state->final_corner_points;
    return mesh->point_count>0u;
}

bool expandAttributeToPoints(const DecodeState& state,const DecoderGroup& group,AttributeRecord *record,std::string *error)
{
    if(!record||!state.topology)return false;
    const std::size_t components=record->output.components;if(components==0u)return fail(error,"invalid Draco attribute components");
    const std::vector<double> encoded=record->output.values;record->output.values.assign(state.final_point_corner.size()*components,0.0);
    for(std::size_t point=0u;point<state.final_point_corner.size();++point){const int corner=state.final_point_corner[point];if(corner<0||static_cast<std::size_t>(corner)>=group.corner_vertex.size())return fail(error,"invalid Draco point representative");const std::uint32_t domain=group.corner_vertex[static_cast<std::size_t>(corner)];if(domain>=group.domain_to_entry.size())return fail(error,"invalid Draco attribute domain");const std::uint32_t entry=group.domain_to_entry[domain];if(entry==UINT32_MAX||(static_cast<std::size_t>(entry)+1u)*components>encoded.size())return fail(error,"invalid Draco attribute entry mapping");for(std::size_t c=0u;c<components;++c)record->output.values[point*components+c]=encoded[static_cast<std::size_t>(entry)*components+c];}
    return true;
}

} // namespace
