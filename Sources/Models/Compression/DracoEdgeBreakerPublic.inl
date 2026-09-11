bool dracoEdgeBreakerCornerDomains(const DracoEdgeBreakerTopology& topology,std::size_t seam_index,
                                   std::vector<std::uint32_t> *corner_domains,std::uint32_t *domain_count)
{
    if(!corner_domains||!domain_count||!validTopology(topology)||seam_index>=topology.seams.size()||topology.seams[seam_index].size()!=topology.corner_vertices.size())return false;
    const std::size_t corners=topology.corner_vertices.size();DisjointSet sets(corners);
    const auto& seams=topology.seams[seam_index];
    for(std::size_t c=0u;c<corners;++c){const int edge=previousCorner(static_cast<int>(c));if(edge<0||seams[static_cast<std::size_t>(edge)]!=0u)continue;const int opposite=oppositeCorner(topology,edge);if(opposite<0)continue;const int neighbor=previousCorner(opposite);if(neighbor<0||static_cast<std::size_t>(neighbor)>=corners)continue;if(topology.corner_vertices[c]!=topology.corner_vertices[static_cast<std::size_t>(neighbor)])continue;sets.unite(static_cast<std::uint32_t>(c),static_cast<std::uint32_t>(neighbor));}
    std::unordered_map<std::uint32_t,std::uint32_t> compact;corner_domains->resize(corners);std::uint32_t count=0u;
    for(std::size_t c=0u;c<corners;++c){const std::uint32_t root=sets.root(static_cast<std::uint32_t>(c));const auto [it,inserted]=compact.emplace(root,count);if(inserted)++count;(*corner_domains)[c]=it->second;}
    *domain_count=count;return true;
}

bool decodeDracoEdgeBreakerAttributes(const std::uint8_t *data,std::size_t size,const DracoEdgeBreakerTopology& topology,
                                      DracoMesh *mesh,std::string *error)
{
    if (error) error->clear();
    if (!data || !mesh || !validTopology(topology))
        return fail(error, "invalid Draco EdgeBreaker attribute input");
    Reader reader(data,size);std::uint8_t group_count=0u;if(!reader.u8(&group_count)||group_count==0u)return fail(error,"truncated Draco attribute decoder count");
    DecodeState state;state.topology=&topology;state.groups.resize(group_count);
    for(DecoderGroup& group:state.groups){if(!reader.i8(&group.data_id)||!reader.u8(&group.decoder_type)||!reader.u8(&group.traversal_method))return fail(error,"truncated Draco mesh attribute decoder header");if(group.traversal_method>1u)return fail(error,"invalid Draco traversal method");if(!buildGroupTopology(topology,&group,error)||!generateSequence(topology,&group,error))return false;}
    for(DecoderGroup& group:state.groups){std::uint32_t count=0u;if(!reader.var32(&count))return fail(error,"truncated Draco attribute count");group.attributes.resize(count);for(AttributeRecord& record:group.attributes){std::uint8_t normalized=0u;if(!reader.u8(&record.output.attribute_type)||!reader.u8(&record.output.data_type)||!reader.u8(&record.output.components)||!reader.u8(&normalized)||!reader.var32(&record.output.unique_id))return fail(error,"truncated Draco attribute descriptor");record.output.normalized=normalized!=0u;if(record.output.components==0u||dataTypeSize(record.output.data_type)==0u)return fail(error,"invalid Draco attribute layout");}for(AttributeRecord& record:group.attributes)if(!reader.u8(&record.decoder_type)||record.decoder_type>3u)return fail(error,"invalid Draco sequential attribute decoder type");}
    if(!buildFinalPoints(&state,mesh,error))return false;
    mesh->attributes.clear();
    for(DecoderGroup& group:state.groups){
        for(AttributeRecord& record:group.attributes){const std::size_t entries=group.sequence_corners.size();if(record.decoder_type==0u){if(entries>std::numeric_limits<std::size_t>::max()/record.output.components)return fail(error,"Draco generic attribute size overflow");record.output.values.resize(entries*record.output.components);for(double& value:record.output.values)if(!rawNumber(reader,record.output.data_type,&value))return fail(error,"truncated Draco generic attribute values");}
            else if(!decodeIntegerAttribute(reader,&state,group,&record,error))return false;
            if(record.output.attribute_type==0u&&record.output.components==3u&&!record.portable.empty()){state.position_record=&record;state.position_group=&group;}
            if(!expandAttributeToPoints(state,group,&record,error))return false;
            mesh->attributes.push_back(record.output);
        }
    }
    return true;
}
